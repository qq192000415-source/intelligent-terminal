// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "precomp.h"
#include "../TerminalApp/EnhancedInput/GrokCommandData.h"

using namespace WEX::Logging;
using namespace WEX::TestExecution;
using namespace WEX::Common;
using namespace winrt::TerminalApp::implementation;

namespace TerminalAppUnitTests
{
    class GrokCommandDataTests
    {
        TEST_CLASS(GrokCommandDataTests);
        TEST_METHOD(GroupCountIs7);
        TEST_METHOD(CommandCountIs61);
        TEST_METHOD(FillCountIs10);
        TEST_METHOD(DangerCommandsAreExactlyThree);
        TEST_METHOD(FillCommandsHaveNoAngleBrackets);
        TEST_METHOD(EveryEntryHasCmdAndTagAndDesc);
        TEST_METHOD(ImagineFillIsPrefixWithTrailingSpace);
    };

    static size_t TotalCommands()
    {
        size_t n = 0;
        for (const auto& g : kGrokCommandGroups) n += g.entries.size();
        return n;
    }

    void GrokCommandDataTests::GroupCountIs7()
    {
        VERIFY_ARE_EQUAL(size_t{ 7 }, std::size(kGrokCommandGroups));
    }

    void GrokCommandDataTests::CommandCountIs61()
    {
        VERIFY_ARE_EQUAL(size_t{ 61 }, TotalCommands());
    }

    void GrokCommandDataTests::FillCountIs10()
    {
        size_t n = 0;
        for (const auto& g : kGrokCommandGroups)
            for (const auto& e : g.entries)
                if (e.fill) ++n;
        VERIFY_ARE_EQUAL(size_t{ 10 }, n);
    }

    void GrokCommandDataTests::DangerCommandsAreExactlyThree()
    {
        std::vector<std::wstring> d;
        for (const auto& g : kGrokCommandGroups)
            for (const auto& e : g.entries)
                if (e.danger) d.emplace_back(e.cmd);
        VERIFY_ARE_EQUAL(size_t{ 3 }, d.size());
        VERIFY_IS_TRUE(std::find(d.begin(), d.end(), L"/new") != d.end());
        VERIFY_IS_TRUE(std::find(d.begin(), d.end(), L"/always-approve") != d.end());
        VERIFY_IS_TRUE(std::find(d.begin(), d.end(), L"grok memory clear --workspace") != d.end());
    }

    void GrokCommandDataTests::FillCommandsHaveNoAngleBrackets()
    {
        for (const auto& g : kGrokCommandGroups)
            for (const auto& e : g.entries)
                if (e.fill)
                {
                    VERIFY_ARE_EQUAL(std::wstring_view::npos, e.cmd.find(L'<'),
                        NoThrowString().Format(L"fill cmd has <: %s", std::wstring{ e.cmd }.c_str()));
                    VERIFY_ARE_EQUAL(std::wstring_view::npos, e.cmd.find(L'>'),
                        NoThrowString().Format(L"fill cmd has >: %s", std::wstring{ e.cmd }.c_str()));
                }
    }

    void GrokCommandDataTests::EveryEntryHasCmdAndTagAndDesc()
    {
        for (const auto& g : kGrokCommandGroups)
        {
            VERIFY_IS_FALSE(g.title.empty());
            for (const auto& e : g.entries)
            {
                VERIFY_IS_FALSE(e.cmd.empty());
                VERIFY_IS_FALSE(e.tag.empty());
                VERIFY_IS_FALSE(e.desc.empty());
            }
        }
    }

    void GrokCommandDataTests::ImagineFillIsPrefixWithTrailingSpace()
    {
        bool found = false;
        for (const auto& g : kGrokCommandGroups)
            for (const auto& e : g.entries)
                if (e.tag == L"生图")
                {
                    found = true;
                    VERIFY_IS_TRUE(e.fill);
                    VERIFY_ARE_EQUAL(std::wstring{ L"/imagine " }, std::wstring{ e.cmd });
                }
        VERIFY_IS_TRUE(found);
    }
}
