# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repo is

Intelligent Terminal is an **experimental fork of Windows Terminal** with native AI-agent
integration. It keeps the entire upstream Windows Terminal / OpenConsole codebase and adds an
agent layer on top. There are effectively **two codebases in one repo**:

- **C++ / WinRT (MSBuild)** — the terminal itself, under `src/cascadia/` (the fork's code) and
  `src/` (inherited OpenConsole: conhost, buffer, VT parser, etc.).
- **Rust (Cargo)** — `tools/wta/`, the "WTA" agent orchestrator (`wta-master`/`wta-helper`/`wtcli`)
  that talks to agent CLIs over ACP.

**Read `AGENTS.md` first** — it is the authoritative, current description of the agent
architecture (WTA helper+master model, the COM `IProtocolServer` integration surface, `wtcli`,
per-tab pre-warmed agent panes, autofix pipeline, hooks auto-upgrade, and the runtime log layout).
`ARCHITECTURE.md` is a pointer to it and to the feature specs under `doc/specs/`. Do not restate
that architecture here — go there.

## Build

Two independent build systems; **both** are needed for a working app. Build Rust first (the
C++ package build copies `wta.exe` into the package layout).

### 1. WTA (Rust)
```bash
taskkill //f //im wta.exe 2>/dev/null; true   # kill stale processes that lock the exe
cargo build --target x86_64-pc-windows-msvc --manifest-path tools/wta/Cargo.toml
```
Always pass `--target` explicitly — the wapproj prefers `target/<triple>/<profile>/wta.exe` over
the bare `target/<profile>` output, so a stale explicit-target binary silently shadows a fresh
bare build.

### 2. Terminal (C++ / MSBuild)
The full solution is **`OpenConsole.slnx`** (~960 projects). Note: `Scratch.sln` at the root is
**not** the real solution — it has a handful of projects and does not include TerminalApp; don't
build against it.

MSBuild is not on PATH by default. Set up the VS dev environment via the repo's module, then build:
```powershell
Import-Module tools/OpenConsole.psm1
Set-MsbuildDevEnvironment          # imports VSSetup, runs Enter-VsDevShell
Invoke-OpenConsoleBuild            # nuget restore + msbuild OpenConsole.slnx; extra args pass through
```
Or the razzle/bcz flow (see `AGENTS.md` → Build): `cmd.exe //c "tools\razzle.cmd && bcz no_clean"`
(`bcz rel no_clean` for Release).

### Building a single project incrementally
When iterating on one project (e.g. `src/cascadia/TerminalApp/TerminalAppLib.vcxproj`), build that
`.vcxproj` directly — but you **must** pass `/p:SolutionDir=<repo-root>\` (trailing backslash) or the
build fails with `MSB4019` (can't find `build\rules\CollectWildcardResources.targets`), because
`$(SolutionDir)` otherwise resolves to the project folder instead of the repo root:
```
msbuild src/cascadia/TerminalApp/TerminalAppLib.vcxproj \
  /p:Configuration=Release /p:Platform=x64 \
  /p:SolutionDir=<repo-root>\ /p:BuildProjectReferences=false
```

**Git Bash / MSYS path conversion gotcha:** when running `msbuild` from Git Bash (not CMD), the
`/p:` prefix triggers MSYS path rewriting — `/m` becomes `M:/`, `/nologo` becomes a drive path, and
the build fails with `MSB1008: only one project can be specified`. Use the `-` prefix instead; MSBuild
accepts both and Git Bash leaves it alone: `-p:Configuration=Release -p:Platform=x64 -m -nologo`.

### Build-order and clean gotchas
- Build `Microsoft.Terminal.Settings.ModelLib` **before** consumer projects. Its `.winmd` is the
  source of truth for the Profile/Globals WinRT projection; a stale winmd makes cppwinrt generate
  projections missing newer members → `C2039` in `TerminalSettingsAppAdapterLib`. The MSIX scripts
  (`_build_msix_x64.cmd`) enforce Settings Model → Settings Editor → package order.
- **Don't wipe the whole `obj/x64/Release`.** For a clean MSIX build, only the wapproj's
  intermediates (`src/cascadia/CascadiaPackage/obj` + `.../bin/.../AppX`) are wiped so glob-based
  Content items (`wt-agent-hooks/**`) get re-evaluated.

## The DLL-layout / deploy trap (important)

`TerminalAppLib.vcxproj` builds a **static lib + winmd**, not the runnable DLL. The DLL comes from a
**separate** project, `src/cascadia/TerminalApp/dll/TerminalApp.vcxproj`. And `TerminalApp.dll`
exists in **three** places:

| Path | Role |
|------|------|
| `bin/x64/Release/TerminalApp/TerminalApp.dll` | linker primary output |
| `bin/x64/Release/WindowsTerminal/TerminalApp.dll` | loose run layout |
| `src/cascadia/CascadiaPackage/bin/x64/Release/TerminalApp.dll` | **what the installed MSIX actually loads** |

So "I built the lib, why doesn't the app change?" has three causes: the DLL is a different project;
a single-project build doesn't propagate the DLL into the package layout; and the running app loads
from the CascadiaPackage layout, not the linker output. To see a change at runtime you must rebuild
the package (or the `CascadiaPackage.wapproj`) and re-register/restart the app — a `TerminalAppLib`
build alone verifies compilation only.

The app runs as a **sideloaded MSIX** (dev family `IntelligentTerminal_rd9vj3e6a2mbr`,
AUMID `IntelligentTerminal_rd9vj3e6a2mbr!App`). Install/refresh: unregister the old package, then
`Add-AppxPackage -Register <CascadiaPackage bin>\AppxManifest.xml`.

### XAML / resources.pri trap

`resources.pri` (the 1.9 MB merged resource file in the CascadiaPackage layout) is only rebuilt by
the **full** `CascadiaPackage.wapproj` build. Incremental single-project builds (`TerminalAppLib`,
`dll/TerminalApp`) do **not** update it. So XAML property values set in `.xaml` source (e.g.
`AcceptsReturn`, `PlaceholderText`) will appear to have no effect at runtime even after a rebuild and
redeploy of `TerminalApp.dll`, because the app is still loading the XBF embedded in the stale
`resources.pri`.

The reliable workaround: **override XAML properties programmatically in the C++ constructor**, right
after `InitializeComponent()`:
```cpp
InitializeComponent();
Composer().AcceptsReturn(false);                         // wins over whatever XAML/XBF says
Composer().PlaceholderText(L"Enter 发送，Shift+Enter 换行…");
```
This approach requires only a C++ change and works with the fast incremental pipeline. Only run the
full wapproj build if you have many XAML changes that aren't practical to mirror in C++.

The package layout also contains a `TerminalApp/` subdirectory with loose `.xaml` source files — but
these are for design-time tooling (Blend, designer) and are **not** loaded at runtime. Copying an
updated `.xaml` there has no effect on the running app.

### Package identity & COM
The COM server (`TerminalProtocolComServer`) is registered under the terminal's package identity, so
`wta.exe` / `wtcli.exe` must also run **with package identity** to activate it via `CoCreateInstance`.
That's why `wta.exe` is deployed inside the package next to `WindowsTerminal.exe`. Running the
unpackaged `tools/wta/target/.../wta.exe` directly fails COM calls with `0x80073D54`
(APPMODEL_ERROR_NO_PACKAGE). If autofix/agent panes break after a debug launch, check for that error.

## Test

C++ tests are TAEF-based, driven through the module:
```powershell
Import-Module tools/OpenConsole.psm1
Invoke-OpenConsoleTests                 # unit tests by default
Invoke-OpenConsoleTests -FTOnly         # feature tests only
Invoke-OpenConsoleTests -Test terminalApp   # a single suite
```
Valid `-Test` names include: `host interactivityWin32 terminal adapter feature uia textbuffer til
types terminalCore terminalApp localTerminalApp unitSettingsModel unitControl winconpty`. The suite
list lives in `tools/tests.xml`; `uia`/feature runs launch WinAppDriver from `dep/WinAppDriver`.

Rust tests:
```bash
cargo test --target x86_64-pc-windows-msvc --manifest-path tools/wta/Cargo.toml
cargo test --manifest-path tools/wta/Cargo.toml <name>   # single test by name filter
```

## Active feature work: Enhanced Input pane

Current branch `feat/enhanced-input-pane` adds a **second** dockable pane (Alt+E) that is distinct
from the Agent pane (Ctrl+Shift+A) — they are intentionally separate and not merged. It is being
built in phases (space panel → send channel → M1 quick commands → composer → skills → custom
commands → full acceptance).

**Its design/requirements/progress docs live OUTSIDE this repo**, in `E:\terminal-dev\docs\`
(`requirements.md`, `design.md`, `architecture.md`, `implementation-plan.md`, `progress.md`,
`findings.md`, `upstream-touchpoints.md`, plus `layout-options.html` UI prototype). This is
non-obvious and not discoverable from the repo. Key conventions from those docs:
- `progress.md` tracks phase status and is the cross-session source of truth; read it before
  resuming this work.
- Per-phase completion means: compiles → **manual on-device acceptance** of the relevant scenarios
  in `requirements.md §7` → update `upstream-touchpoints.md` (which upstream files changed and why)
  → one commit on the feature branch. "Compiles" alone is not "done."
- Command data for the quick-commands palette is authored in `layout-options.html`'s `CMD_GROUPS`
  and mirrored into `src/cascadia/TerminalApp/EnhancedInput/CommandData.h`.

## C++ / WinRT patterns in this fork

### Keyboard state in XAML Island code

`CoreWindow::GetForCurrentThread().GetKeyState()` is **unreliable** inside `TerminalApp` because
`TerminalApp` runs inside a XAML Island — the Island's `CoreWindow` does not receive raw input, so
modifier key queries always return "up". Use Win32 `::GetKeyState(VK_SHIFT)` etc. instead; it reads
the OS keyboard-state table and is reliable regardless of the hosting model. This affects any code
in `TerminalApp` that checks modifier keys in a `KeyDown` handler (e.g. `EnhancedInputContent`,
`CommandPalette`).

### Code formatting

```cmd
runformat          # clang-format all C++ files (from a razzle environment)
```

XAML uses XamlStyler; config in `XamlStyler.json` at repo root.

## Upstream-fork discipline

This is a fork that tracks upstream Windows Terminal. When you touch inherited files (anything under
`src/` outside the fork's `src/cascadia/TerminalApp` agent/enhanced-input additions), keep changes
minimal and record them — the fork tracks its upstream deltas in `doc/specs/` and, for the
enhanced-input work, in the external `upstream-touchpoints.md`. Prefer adding new files over editing
inherited ones where practical.
