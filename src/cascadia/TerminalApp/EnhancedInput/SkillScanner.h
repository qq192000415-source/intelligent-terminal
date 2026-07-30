// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace winrt::TerminalApp::implementation
{
    // One discovered skill (architecture §4.3). Values are already UTF-16 and
    // display-ready: `name` / `description` come from the SKILL.md frontmatter,
    // overridden by agents/openai.yaml (display_name / short_description) when present.
    struct SkillEntry
    {
        std::wstring id; // skill directory name — stable identifier
        std::wstring name; // display name (sorted by this)
        std::wstring description; // one-line summary for the hover bar
    };

    // Shared bridge (architecture §3, §4.3). Scans ~/.claude/skills/*/SKILL.md at
    // runtime — no baked-in list. Pure C++ (no XAML / TermControl) so the scan +
    // frontmatter parse is unit-testable with an injected temp directory.
    //
    // Failure is silent by contract: a missing / unreadable skills dir returns an
    // empty vector and never throws (requirements §7 scenario 5 — must not disrupt
    // the terminal).
    struct SkillScanner
    {
        // skillsDir defaults to %USERPROFILE%\.claude\skills; tests inject a temp dir.
        explicit SkillScanner(std::filesystem::path skillsDir = {});

        // Scan every immediate subdirectory that has a SKILL.md, sorted by name.
        // Best-effort: skips entries it can't read, never throws.
        std::vector<SkillEntry> Scan() const noexcept;

        const std::filesystem::path& SkillsDir() const noexcept { return _skillsDir; }

        // --- Parsing helpers (static, pure — exposed for unit testing) ---

        // Read one skill directory into an entry. Returns false if there's no
        // readable SKILL.md (=> skip this directory).
        static bool ReadSkillDir(const std::filesystem::path& dir, SkillEntry& out) noexcept;

        // Minimal YAML: pull `key: value` scalars for the requested keys out of a
        // frontmatter/YAML text. Single-line scalars only; strips quotes and inline
        // comments; ignores nesting. Enough for name/description/display_name/
        // short_description — deliberately NOT a full YAML parser (zero new deps).
        static std::wstring ExtractScalar(std::wstring_view yaml, std::wstring_view key) noexcept;

        // Slice the `---`-fenced frontmatter block out of a SKILL.md body. If there
        // is no fence, returns the whole text (some files are frontmatter-only).
        static std::wstring_view Frontmatter(std::wstring_view markdown) noexcept;

    private:
        std::filesystem::path _skillsDir;
    };
}
