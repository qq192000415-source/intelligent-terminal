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

## Build environment

**Requires Visual Studio 2026** (installed at `C:\Program Files\Microsoft Visual Studio\18\Community\`).
VS2022 can compile C++ but the SDK 26100 XAML compiler is incompatible with VS2022 — use VS2026.

Required Windows SDK: **10.0.26100.0** (selected automatically when VS2026 is detected via
`src/common.build.pre.props`'s `VisualStudioVersion >= 18.0` condition).

vcpkg triplets are pinned to toolset `v143` (compatible with both VS2022 and VS2026) in
`dep/vcpkg-overlay-triplets/`. Do not change these to `v145` unless intentionally breaking
compatibility with VS2022.

## Build

Two independent build systems; **both** are needed for a working app. Build Rust first (the
C++ package build copies `wta.exe` into the package layout).

### Fast incremental build (daily development)

Use the scripts in the **parent directory** `E:\terminal-dev\` — not MSBuild directly:

```bash
# bash (Git Bash) — preferred for incremental dev builds
bash E:/terminal-dev/build-phase3.sh
```
```powershell
# PowerShell equivalent
E:\terminal-dev\build-phase.ps1
```

These build in a fixed order: wta (cargo) → `Host.Proxy` → `TerminalSettingsModel` →
`TerminalSettingsEditor` → `CascadiaPackage.wapproj` (produces the MSIX). Don't clear
`obj\x64\Release` entirely — incremental builds rely on it.

### Full solution build

```powershell
Import-Module tools/OpenConsole.psm1
Set-MsbuildDevEnvironment          # imports VSSetup, runs Enter-VsDevShell
Invoke-OpenConsoleBuild            # nuget restore + msbuild OpenConsole.slnx; extra args pass through
```
Or: `cmd.exe //c "tools\razzle.cmd && bcz no_clean"` (`bcz rel no_clean` for Release).

### 1. WTA (Rust)
```bash
taskkill //f //im wta.exe 2>/dev/null; true   # kill stale processes that lock the exe
cargo build --target x86_64-pc-windows-msvc --manifest-path tools/wta/Cargo.toml
```
Always pass `--target` explicitly — the wapproj prefers `target/<triple>/<profile>/wta.exe` over
the bare `target/<profile>` output, so a stale explicit-target binary silently shadows a fresh
bare build.

### 2. Terminal (C++ / MSBuild) — single project

When iterating on one project (e.g. `src/cascadia/TerminalApp/TerminalAppLib.vcxproj`), build that
`.vcxproj` directly — but you **must** pass `/p:SolutionDir=<repo-root>\` (trailing backslash):
```
msbuild src/cascadia/TerminalApp/TerminalAppLib.vcxproj \
  /p:Configuration=Release /p:Platform=x64 \
  /p:SolutionDir=<repo-root>\ /p:BuildProjectReferences=false
```

**Git Bash gotcha:** use `-` prefix instead of `/` for MSBuild flags — MSYS rewrites `/m` to `M:/`:
`-p:Configuration=Release -p:Platform=x64 -m -nologo`.

### Build-order and clean gotchas
- Build `Microsoft.Terminal.Settings.ModelLib` **before** consumer projects. Its `.winmd` is the
  source of truth for the Profile/Globals WinRT projection; a stale winmd makes cppwinrt generate
  projections missing newer members → `C2039` in `TerminalSettingsAppAdapterLib`.
- **Don't wipe the whole `obj/x64/Release`.** For a clean MSIX build, only the wapproj's
  intermediates are wiped.

## Install / run

```powershell
# Must close all running IntelligentTerminal windows first (file lock on DLLs)
Add-AppxPackage -Register `
  "E:\terminal-dev\intelligent-terminal\src\cascadia\CascadiaPackage\bin\x64\Release\AppxManifest.xml"
```

**Note:** `AppxManifest.xml` lives under `src\cascadia\CascadiaPackage\bin\…`, NOT under
`bin\x64\Release\CascadiaPackage\`. This is the loose-layout (development mode) registration path.

App identity: `IntelligentTerminal_rd9vj3e6a2mbr`, AUMID: `IntelligentTerminal_rd9vj3e6a2mbr!App`.

### Mtime trap after git reset/checkout

`git reset`/`checkout` sets restored source files' mtime to **now**, which may be newer than
existing `obj/` artifacts — MSBuild then skips recompilation ("source is older than output").
After any git operation that restores source files, `touch` the affected `.cpp`/`.h` files before
rebuilding, then verify `TerminalApp.dll`'s `LastWriteTime` updated:
```powershell
(Get-Item "bin\x64\Release\TerminalApp\TerminalApp.dll").LastWriteTime
```

## The DLL-layout / deploy trap (important)

`TerminalAppLib.vcxproj` builds a **static lib + winmd**, not the runnable DLL. The DLL comes from
`src/cascadia/TerminalApp/dll/TerminalApp.vcxproj`. `TerminalApp.dll` exists in **three** places:

| Path | Role |
|------|------|
| `bin/x64/Release/TerminalApp/TerminalApp.dll` | linker primary output |
| `bin/x64/Release/WindowsTerminal/TerminalApp.dll` | loose run layout |
| `src/cascadia/CascadiaPackage/bin/x64/Release/TerminalApp.dll` | **what the installed MSIX loads** |

A `TerminalAppLib` build alone verifies compilation only — to see a change at runtime, rebuild the
full package via `CascadiaPackage.wapproj` and re-register.

The app runs as a sideloaded MSIX (`IsDevelopmentMode=True`), so `Add-AppxPackage -Register`
reads DLLs directly from the `CascadiaPackage\bin\x64\Release\` loose layout at runtime. Rolling
back with `git reset` + rebuilding is the only reliable way to change what the running app sees.

### XAML / resources.pri trap

`resources.pri` is only rebuilt by the full `CascadiaPackage.wapproj`. Incremental single-project
builds do **not** update it. Override XAML properties programmatically in the C++ constructor
instead of relying on XAML source for fast iteration:
```cpp
InitializeComponent();
Composer().AcceptsReturn(false);
```

### Package identity & COM

The COM server (`TerminalProtocolComServer`) is registered under the terminal's package identity.
`wta.exe` must run **with package identity** to activate it. Running the unpackaged binary from
`tools/wta/target/…` directly fails with `0x80073D54` (APPMODEL_ERROR_NO_PACKAGE).

## Test

```powershell
Import-Module tools/OpenConsole.psm1
Invoke-OpenConsoleTests                       # unit tests
Invoke-OpenConsoleTests -Test terminalApp     # single suite
```
Valid `-Test` names: `host interactivityWin32 terminal adapter feature uia textbuffer til
types terminalCore terminalApp localTerminalApp unitSettingsModel unitControl winconpty`.

```bash
cargo test --target x86_64-pc-windows-msvc --manifest-path tools/wta/Cargo.toml
cargo test --manifest-path tools/wta/Cargo.toml <name>   # single test
```

Enhanced input panel unit tests are in `src/cascadia/ut_app/` (suite `terminalApp`), hooking into
the existing `UnitTests_TerminalApp` project via `ProjectReference` to `TerminalAppLib`.

## Branch structure and upstream sync

This fork maintains a clean separation between upstream and local changes:

| Branch | Content |
|--------|---------|
| `main` | Pure upstream mirror — never contains fork-specific commits |
| `feat/enhanced-input-pane` | upstream + 3 themed commits (see below) |

The 3 commits on `feat/enhanced-input-pane`:
1. **New files** — `EnhancedInput/`, `EnhancedInputContent.*`, unit tests (zero conflict with upstream)
2. **Integration** — `TerminalPage`, `Tab`, `TerminalWindow`, `AppHost`, `TerminalAppLib.vcxproj`
3. **VS2022 compat** — vcpkg triplet v145→v143, SDK version conditional, `#ifdef NTDDI_WIN11_GE` guards

### Syncing upstream changes

```bash
# 1. Pull latest upstream into main
git checkout main
git -c http.proxy=socks5h://127.0.0.1:10808 pull origin main   # proxy required for GitHub

# 2. Rebase our 3 commits onto new upstream
git checkout feat/enhanced-input-pane
git rebase main
# conflicts (if any) concentrate in TerminalPage.cpp and Tab.cpp

# 3. Rebuild and reinstall
cd E:/terminal-dev && bash build-phase3.sh
Add-AppxPackage -Register "...CascadiaPackage\bin\x64\Release\AppxManifest.xml"
```

If upstream adds new SDK-gated APIs, wrap them in `#ifdef NTDDI_WIN11_GE` with an `else` fallback
(see `src/cascadia/TerminalSettingsModel/CascadiaSettingsSerialization.cpp` for the pattern).

## Enhanced Input pane — current status

**All 7 phases complete and validated.** Branch `feat/enhanced-input-pane`. Alt+E to toggle.

The panel is a **right-docked, full-height pane** distinct from the Agent pane (Ctrl+Shift+A) —
they are intentionally separate and must not be merged.

Design/requirements/progress docs live **outside this repo** in `E:\terminal-dev\docs\`:
`requirements.md`, `design.md`, `architecture.md`, `implementation-plan.md`, `progress.md`,
`findings.md`, `upstream-touchpoints.md`, plus `layout-options.html` UI prototype.

Key conventions:
- `progress.md` is the cross-session source of truth for phase status.
- Per-phase completion = compiles + manual on-device acceptance per `requirements.md §7` + commit.
- Command data for the quick-commands palette is authored in `layout-options.html`'s `CMD_GROUPS`
  and mirrored into `src/cascadia/TerminalApp/EnhancedInput/CommandData.h`.

### Send channel

All sends go through a single exit point `ITerminalSink` (three methods):
- `Send(text)` — appends `\r`, executes immediately (quick commands, composer)
- `TypeToTerminal(text)` — inserts into terminal input line without Enter (skills, let user edit)
- `Insert(text)` — fills composer field (reserved)

### Tab tracking

`Tab.cpp`'s `WalkTree` must have a `try_as<EnhancedInputContent>` branch calling
`SetLastActiveControl`; without it, switching tabs breaks the send target.

### SplitPaneAtRoot behaviour

The enhanced input panel docks as the **outermost right column** via `SplitPaneAtRoot(Right)`.
`Tab::SplitPaneAtRoot` detects an existing `EnhancedInputContent` direct child and routes the
new pane (e.g. agent pane) into the terminal region, not around the enhanced-input column.

## C++ / WinRT patterns in this fork

### Keyboard state in XAML Island code

`CoreWindow::GetForCurrentThread().GetKeyState()` is **unreliable** inside `TerminalApp` —
the Island's `CoreWindow` does not receive raw input, so modifier queries always return "up".
Use Win32 `::GetKeyState(VK_SHIFT)` etc. instead.

### Code formatting

```cmd
runformat          # clang-format all C++ files (from a razzle environment)
```
XAML uses XamlStyler; config in `XamlStyler.json` at repo root.

## Upstream-fork discipline

When touching inherited files (anything under `src/` outside the fork's agent/enhanced-input
additions), keep changes minimal and record them in `upstream-touchpoints.md` (in
`E:\terminal-dev\docs\`). Prefer adding new files over editing inherited ones.

New C++ / WinRT projection classes (`.idl`, `.xaml`, `.h`, `.cpp`) go in the **`TerminalApp`
root directory** following the `MarkdownPaneContent` pattern. XAML/IDL under subdirectories has
no build precedent and is high-risk — don't try it.
