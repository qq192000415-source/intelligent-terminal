// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "precomp.h"
#include "../TerminalApp/EnhancedInput/PaneMode.h"

using namespace winrt::TerminalApp::implementation;

namespace TerminalAppUnitTests
{
    class PaneModeTests
    {
        TEST_CLASS(PaneModeTests);
        TEST_METHOD(ClaudeRootsUseDotClaude);
        TEST_METHOD(GrokRootsUseDotGrok);
        TEST_METHOD(GrokGroupsAreGrokTable);
        TEST_METHOD(ClaudeGroupsAreClaudeTable);
    };

    void PaneModeTests::ClaudeRootsUseDotClaude()
    {
        const auto r = RootsFor(InputPaneMode::Claude, L"D:\\fakehome");
        VERIFY_ARE_EQUAL(std::filesystem::path{ L"D:\\fakehome\\.claude" }, r.storeDir);
        VERIFY_ARE_EQUAL(std::filesystem::path{ L"D:\\fakehome\\.claude\\skills" }, r.skillsDir);
    }

    void PaneModeTests::GrokRootsUseDotGrok()
    {
        const auto r = RootsFor(InputPaneMode::Grok, L"D:\\fakehome");
        VERIFY_ARE_EQUAL(std::filesystem::path{ L"D:\\fakehome\\.grok" }, r.storeDir);
        VERIFY_ARE_EQUAL(std::filesystem::path{ L"D:\\fakehome\\.grok\\skills" }, r.skillsDir);
    }

    void PaneModeTests::GrokGroupsAreGrokTable()
    {
        const auto r = RootsFor(InputPaneMode::Grok, L"D:\\fakehome");
        VERIFY_ARE_EQUAL(std::size(kGrokCommandGroups), r.groups.size());
        VERIFY_ARE_EQUAL(std::wstring{ L"启动" }, std::wstring{ r.groups[0].title });
    }

    void PaneModeTests::ClaudeGroupsAreClaudeTable()
    {
        const auto r = RootsFor(InputPaneMode::Claude, L"D:\\fakehome");
        VERIFY_ARE_EQUAL(std::size(kCommandGroups), r.groups.size());
        VERIFY_ARE_EQUAL(std::wstring{ L"对话管理" }, std::wstring{ r.groups[0].title });
    }
}
