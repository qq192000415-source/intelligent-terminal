// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.
//
// SkillScannerTests.cpp
//
// Bridge-layer tests for SkillScanner (src/cascadia/TerminalApp/EnhancedInput/
// SkillScanner.{h,cpp}) — the runtime scan of ~/.claude/skills/*/SKILL.md
// (requirements §3.3 / §7 scenarios 5,12). Each test builds a sample skills
// tree in a temp dir (the ctor takes skillsDir), so nothing touches ~/.claude.
//
// File CONTENT here is ASCII on purpose: it keeps the tests free of MSVC
// narrow-literal source-charset fragility. The UTF-8 -> UTF-16 path for
// non-ASCII names is covered by real-machine acceptance (Chinese skill names
// render correctly); these tests target the parse / override / sort / skip logic.

#include "precomp.h"

#include <filesystem>
#include <fstream>

#include "../TerminalApp/EnhancedInput/SkillScanner.h"

using namespace WEX::Logging;
using namespace WEX::TestExecution;
using namespace WEX::Common;
using namespace winrt::TerminalApp::implementation;

namespace TerminalAppUnitTests
{
    class SkillScannerTests
    {
        TEST_CLASS(SkillScannerTests);

        TEST_METHOD(ParsesFrontmatterNameAndDescription);
        TEST_METHOD(OpenaiYamlOverridesNameAndDescription);
        TEST_METHOD(SortedById);
        TEST_METHOD(DirectoryWithoutSkillMdIsSkipped);
        TEST_METHOD(NameFallsBackToIdWhenNoName);
        TEST_METHOD(MissingSkillsDirReturnsEmpty);

        TEST_METHOD_SETUP(Setup);
        TEST_METHOD_CLEANUP(Cleanup);

        std::filesystem::path _dir;

        // Write UTF-8 content to <_dir>/<relPath>, creating parent dirs.
        void _write(const std::wstring& relPath, const std::string& content)
        {
            const auto full = _dir / relPath;
            std::error_code ec;
            std::filesystem::create_directories(full.parent_path(), ec);
            std::ofstream out{ full, std::ios::binary | std::ios::trunc };
            out << content;
            out.close();
            VERIFY_IS_TRUE(std::filesystem::exists(full), L"_write failed to create the file");
        }
    };

    bool SkillScannerTests::Setup()
    {
        static int counter = 0;
        _dir = std::filesystem::temp_directory_path() / (L"ut_skills_" + std::to_wstring(++counter));
        std::error_code ec;
        std::filesystem::remove_all(_dir, ec);
        std::filesystem::create_directories(_dir, ec);
        return true;
    }

    bool SkillScannerTests::Cleanup()
    {
        std::error_code ec;
        std::filesystem::remove_all(_dir, ec);
        return true;
    }

    void SkillScannerTests::ParsesFrontmatterNameAndDescription()
    {
        _write(L"alpha\\SKILL.md",
               "---\nname: Alpha Skill\ndescription: does alpha things\n---\n# body\n");
        const auto skills = SkillScanner{ _dir }.Scan();
        VERIFY_ARE_EQUAL(size_t{ 1 }, skills.size());
        VERIFY_ARE_EQUAL(std::wstring{ L"alpha" }, skills[0].id);
        VERIFY_ARE_EQUAL(std::wstring{ L"Alpha Skill" }, skills[0].name);
        VERIFY_ARE_EQUAL(std::wstring{ L"does alpha things" }, skills[0].description);
    }

    void SkillScannerTests::OpenaiYamlOverridesNameAndDescription()
    {
        // SKILL.md provides baseline; agents/openai.yaml (nested under interface:)
        // overrides display_name / short_description.
        _write(L"beta\\SKILL.md",
               "---\nname: Baseline Name\ndescription: baseline desc\n---\n");
        _write(L"beta\\agents\\openai.yaml",
               "interface:\n  display_name: Override Name\n  short_description: override desc\n");
        const auto skills = SkillScanner{ _dir }.Scan();
        VERIFY_ARE_EQUAL(size_t{ 1 }, skills.size());
        VERIFY_ARE_EQUAL(std::wstring{ L"Override Name" }, skills[0].name);
        VERIFY_ARE_EQUAL(std::wstring{ L"override desc" }, skills[0].description);
    }

    void SkillScannerTests::SortedById()
    {
        // Created out of order; Scan must return them alphabetically by dir id.
        _write(L"zeta\\SKILL.md", "---\nname: Z\ndescription: z\n---\n");
        _write(L"alpha\\SKILL.md", "---\nname: A\ndescription: a\n---\n");
        _write(L"mu\\SKILL.md", "---\nname: M\ndescription: m\n---\n");
        const auto skills = SkillScanner{ _dir }.Scan();
        VERIFY_ARE_EQUAL(size_t{ 3 }, skills.size());
        VERIFY_ARE_EQUAL(std::wstring{ L"alpha" }, skills[0].id);
        VERIFY_ARE_EQUAL(std::wstring{ L"mu" }, skills[1].id);
        VERIFY_ARE_EQUAL(std::wstring{ L"zeta" }, skills[2].id);
    }

    void SkillScannerTests::DirectoryWithoutSkillMdIsSkipped()
    {
        _write(L"real\\SKILL.md", "---\nname: Real\ndescription: r\n---\n");
        // A sibling dir with no SKILL.md (just some other file) must be ignored.
        _write(L"notaskill\\README.txt", "not a skill");
        const auto skills = SkillScanner{ _dir }.Scan();
        VERIFY_ARE_EQUAL(size_t{ 1 }, skills.size());
        VERIFY_ARE_EQUAL(std::wstring{ L"real" }, skills[0].id);
    }

    void SkillScannerTests::NameFallsBackToIdWhenNoName()
    {
        // No name key in frontmatter — the skill must still surface, named by its id.
        _write(L"gamma\\SKILL.md", "---\ndescription: only a desc\n---\n");
        const auto skills = SkillScanner{ _dir }.Scan();
        VERIFY_ARE_EQUAL(size_t{ 1 }, skills.size());
        VERIFY_ARE_EQUAL(std::wstring{ L"gamma" }, skills[0].name);
        VERIFY_ARE_EQUAL(std::wstring{ L"only a desc" }, skills[0].description);
    }

    void SkillScannerTests::MissingSkillsDirReturnsEmpty()
    {
        // Point at a path that doesn't exist — silent empty, never throws.
        const auto skills = SkillScanner{ _dir / L"does_not_exist" }.Scan();
        VERIFY_IS_TRUE(skills.empty());
    }
}
