// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "SkillScanner.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <til/u8u16convert.h>

using namespace std::filesystem;

namespace winrt::TerminalApp::implementation
{
    static std::filesystem::path _defaultSkillsDir()
    {
        wchar_t profile[MAX_PATH]{};
        if (GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH) > 0)
        {
            return std::filesystem::path{ profile } / L".claude" / L"skills";
        }
        return std::filesystem::path{ L".claude" } / L"skills";
    }

    // Trim ASCII whitespace (space / tab / CR) from both ends.
    static std::wstring_view _trim(std::wstring_view s) noexcept
    {
        constexpr std::wstring_view ws{ L" \t\r" };
        const auto b = s.find_first_not_of(ws);
        if (b == std::wstring_view::npos)
        {
            return {};
        }
        const auto e = s.find_last_not_of(ws);
        return s.substr(b, e - b + 1);
    }

    SkillScanner::SkillScanner(std::filesystem::path skillsDir)
    {
        _skillsDir = skillsDir.empty() ? _defaultSkillsDir() : std::move(skillsDir);
    }

    // Slice out the `---`-fenced frontmatter. No fence => whole text (some skill
    // files are pure frontmatter with no fences).
    std::wstring_view SkillScanner::Frontmatter(std::wstring_view markdown) noexcept
    {
        auto text = markdown;
        // Skip a leading BOM if present.
        if (!text.empty() && text.front() == L'﻿')
        {
            text.remove_prefix(1);
        }
        const auto lead = _trim(text.substr(0, text.find(L'\n')));
        if (lead != L"---")
        {
            return markdown; // no opening fence — treat the whole body as scannable
        }
        // Content starts after the first line.
        const auto afterOpen = text.find(L'\n');
        if (afterOpen == std::wstring_view::npos)
        {
            return {};
        }
        const auto body = text.substr(afterOpen + 1);
        // Find the closing `---` on its own line.
        size_t pos = 0;
        while (pos < body.size())
        {
            auto nl = body.find(L'\n', pos);
            const auto line = body.substr(pos, (nl == std::wstring_view::npos ? body.size() : nl) - pos);
            if (_trim(line) == L"---")
            {
                return body.substr(0, pos);
            }
            if (nl == std::wstring_view::npos)
            {
                break;
            }
            pos = nl + 1;
        }
        return body; // no closing fence — take everything after the opener
    }

    // Minimal single-line YAML scalar lookup. Matches the first line whose trimmed
    // form starts with `key:`, returns the value with quotes and inline comments
    // stripped. Deliberately shallow (no nesting / multi-line scalars).
    std::wstring SkillScanner::ExtractScalar(std::wstring_view yaml, std::wstring_view key) noexcept
    {
        size_t pos = 0;
        while (pos < yaml.size())
        {
            auto nl = yaml.find(L'\n', pos);
            const auto raw = yaml.substr(pos, (nl == std::wstring_view::npos ? yaml.size() : nl) - pos);
            pos = (nl == std::wstring_view::npos) ? yaml.size() : nl + 1;

            const auto line = _trim(raw);
            if (line.empty() || line.front() == L'#')
            {
                continue;
            }
            const auto colon = line.find(L':');
            if (colon == std::wstring_view::npos)
            {
                continue;
            }
            if (_trim(line.substr(0, colon)) != key)
            {
                continue;
            }

            auto value = _trim(line.substr(colon + 1));
            if (value.empty())
            {
                return {};
            }
            // Quoted value: strip the matching pair, keep the inside verbatim.
            if ((value.front() == L'"' && value.back() == L'"' && value.size() >= 2) ||
                (value.front() == L'\'' && value.back() == L'\'' && value.size() >= 2))
            {
                return std::wstring{ value.substr(1, value.size() - 2) };
            }
            // Unquoted: drop an inline comment (space + '#').
            const auto hash = value.find(L" #");
            if (hash != std::wstring_view::npos)
            {
                value = _trim(value.substr(0, hash));
            }
            return std::wstring{ value };
        }
        return {};
    }
    // Read one file fully as UTF-8 and transcode to UTF-16. Empty on any failure.
    static std::wstring _readUtf8(const std::filesystem::path& file) noexcept
    try
    {
        std::ifstream in{ file, std::ios::binary };
        if (!in)
        {
            return {};
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        return til::u8u16(ss.str());
    }
    catch (...)
    {
        return {};
    }

    // Read one skill directory: SKILL.md frontmatter (name/description), overridden
    // by agents/openai.yaml (display_name/short_description) when present. Falls back
    // to the directory name so a skill is never dropped just for a missing field.
    bool SkillScanner::ReadSkillDir(const std::filesystem::path& dir, SkillEntry& out) noexcept
    try
    {
        std::error_code ec;
        const auto skillMd = dir / L"SKILL.md";
        if (!exists(skillMd, ec) || ec)
        {
            return false; // no SKILL.md => not a skill directory
        }

        const auto id = dir.filename().wstring();
        const auto body = _readUtf8(skillMd);
        const auto fm = Frontmatter(body);

        std::wstring name{ ExtractScalar(fm, L"name") };
        std::wstring desc{ ExtractScalar(fm, L"description") };

        // Optional override: agents/openai.yaml display_name / short_description.
        const auto openaiYaml = dir / L"agents" / L"openai.yaml";
        if (exists(openaiYaml, ec) && !ec)
        {
            const auto y = _readUtf8(openaiYaml);
            if (auto dn = ExtractScalar(y, L"display_name"); !dn.empty())
            {
                name = std::move(dn);
            }
            if (auto sd = ExtractScalar(y, L"short_description"); !sd.empty())
            {
                desc = std::move(sd);
            }
        }

        if (name.empty())
        {
            name = id; // never drop a skill for a missing display name
        }

        out = SkillEntry{ id, std::move(name), std::move(desc) };
        return true;
    }
    catch (...)
    {
        return false; // best-effort; a bad directory is skipped, never fatal
    }

    // Scan every immediate subdirectory that has a SKILL.md, sorted by name.
    std::vector<SkillEntry> SkillScanner::Scan() const noexcept
    try
    {
        std::vector<SkillEntry> skills;

        std::error_code ec;
        if (!exists(_skillsDir, ec) || ec)
        {
            return skills; // missing skills dir => empty, silent (requirements §7)
        }

        for (const auto& entry : directory_iterator{ _skillsDir, ec })
        {
            if (ec || !entry.is_directory(ec))
            {
                continue;
            }
            SkillEntry se;
            if (ReadSkillDir(entry.path(), se))
            {
                skills.push_back(std::move(se));
            }
        }

        // Sort by directory id (english, alphabetical) — matches the old project.
        std::sort(skills.begin(), skills.end(), [](const SkillEntry& a, const SkillEntry& b) noexcept {
            return a.id < b.id;
        });
        return skills;
    }
    catch (...)
    {
        return {}; // scan must never disrupt the panel / terminal
    }
}
