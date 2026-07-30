// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.
//
// EnhancedInputLogicTests.cpp
//
// Logic-layer unit tests for the enhanced-input panel's pure headers
// (no XAML / TermControl), so they run in CI (ut_app), not LocalTests:
//   - ComposerLogic.h : Trim / IsSendEnabled / BuildSendPayload
//   - CommandData.h   : the built-in command table invariants
// Both live in namespace winrt::TerminalApp::implementation.

#include "precomp.h"

#include "../TerminalApp/EnhancedInput/ComposerLogic.h"
#include "../TerminalApp/EnhancedInput/CommandData.h"

using namespace WEX::Logging;
using namespace WEX::TestExecution;
using namespace WEX::Common;
using namespace winrt::TerminalApp::implementation;

namespace TerminalAppUnitTests
{
    class EnhancedInputLogicTests
    {
        TEST_CLASS(EnhancedInputLogicTests);

        // ComposerLogic
        TEST_METHOD(TrimStripsSurroundingWhitespace);
        TEST_METHOD(TrimKeepsInnerWhitespace);
        TEST_METHOD(TrimAllWhitespaceIsEmpty);
        TEST_METHOD(SendEnabledOnlyWithTextOrAttachment);
        TEST_METHOD(PayloadTrimsTextAndAppendsPaths);
        TEST_METHOD(PayloadQuotesPathsWithSpaces);
        TEST_METHOD(PayloadAttachmentsOnlyHasNoLeadingSpace);

        // CommandData
        TEST_METHOD(BuiltInCommandCountIs23);
        TEST_METHOD(BuiltInGroupCountIs5);
        TEST_METHOD(OnlyClearIsMarkedDanger);
        TEST_METHOD(EveryEntryHasCmdAndTagAndDesc);
    };

    // ---- ComposerLogic ----

    void EnhancedInputLogicTests::TrimStripsSurroundingWhitespace()
    {
        // Spaces, tabs, CR, LF, and the fullwidth space (　) all count.
        VERIFY_ARE_EQUAL(std::wstring{ L"abc" }, std::wstring{ ComposerLogic::Trim(L"  \t abc \r\n") });
        VERIFY_ARE_EQUAL(std::wstring{ L"x" }, std::wstring{ ComposerLogic::Trim(L"　x　") });
    }

    void EnhancedInputLogicTests::TrimKeepsInnerWhitespace()
    {
        // Only the ends are trimmed — interior spacing is part of the draft.
        VERIFY_ARE_EQUAL(std::wstring{ L"a  b\tc" }, std::wstring{ ComposerLogic::Trim(L"  a  b\tc  ") });
    }

    void EnhancedInputLogicTests::TrimAllWhitespaceIsEmpty()
    {
        VERIFY_IS_TRUE(ComposerLogic::Trim(L"").empty());
        VERIFY_IS_TRUE(ComposerLogic::Trim(L"   \t\r\n　").empty());
    }

    void EnhancedInputLogicTests::SendEnabledOnlyWithTextOrAttachment()
    {
        // Empty / whitespace-only draft with no attachments => disabled.
        VERIFY_IS_FALSE(ComposerLogic::IsSendEnabled(L"", 0));
        VERIFY_IS_FALSE(ComposerLogic::IsSendEnabled(L"   \t　", 0));
        // Real text OR at least one attachment => enabled.
        VERIFY_IS_TRUE(ComposerLogic::IsSendEnabled(L"hi", 0));
        VERIFY_IS_TRUE(ComposerLogic::IsSendEnabled(L"", 1));
        VERIFY_IS_TRUE(ComposerLogic::IsSendEnabled(L"   ", 2));
    }

    void EnhancedInputLogicTests::PayloadTrimsTextAndAppendsPaths()
    {
        const std::vector<std::wstring> atts{ L"C:\\a.png", L"C:\\b.txt" };
        // Text is trimmed; each path is space-separated after it.
        VERIFY_ARE_EQUAL(std::wstring{ L"look C:\\a.png C:\\b.txt" },
                         ComposerLogic::BuildSendPayload(L"  look  ", atts));
    }

    void EnhancedInputLogicTests::PayloadQuotesPathsWithSpaces()
    {
        // A path containing a space is double-quoted (matches TermControl's
        // own drag-drop quoting) so the shell/agent sees one argument.
        const std::vector<std::wstring> atts{ L"C:\\my pics\\shot 1.png" };
        VERIFY_ARE_EQUAL(std::wstring{ L"hi \"C:\\my pics\\shot 1.png\"" },
                         ComposerLogic::BuildSendPayload(L"hi", atts));
    }

    void EnhancedInputLogicTests::PayloadAttachmentsOnlyHasNoLeadingSpace()
    {
        // No text, one attachment: the payload must not start with a stray space.
        const std::vector<std::wstring> atts{ L"C:\\a.png" };
        VERIFY_ARE_EQUAL(std::wstring{ L"C:\\a.png" },
                         ComposerLogic::BuildSendPayload(L"   ", atts));
    }

    // ---- CommandData ----

    void EnhancedInputLogicTests::BuiltInCommandCountIs23()
    {
        // The tab badge shows this count and the docs pin it at 23 (5 groups).
        size_t total = 0;
        for (const auto& group : kCommandGroups)
        {
            total += group.entries.size();
        }
        VERIFY_ARE_EQUAL(size_t{ 23 }, total);
    }

    void EnhancedInputLogicTests::BuiltInGroupCountIs5()
    {
        VERIFY_ARE_EQUAL(size_t{ 5 }, std::size(kCommandGroups));
    }

    void EnhancedInputLogicTests::OnlyClearIsMarkedDanger()
    {
        // danger is now visual-only (⚠), but the invariant "exactly /clear is
        // flagged" guards against accidentally marking/unmarking commands.
        size_t dangerCount = 0;
        std::wstring dangerCmd;
        for (const auto& group : kCommandGroups)
        {
            for (const auto& e : group.entries)
            {
                if (e.danger)
                {
                    ++dangerCount;
                    dangerCmd = e.cmd;
                }
            }
        }
        VERIFY_ARE_EQUAL(size_t{ 1 }, dangerCount);
        VERIFY_ARE_EQUAL(std::wstring{ L"/clear" }, dangerCmd);
    }

    void EnhancedInputLogicTests::EveryEntryHasCmdAndTagAndDesc()
    {
        // Cards render cmd + tag and the hover bar shows desc; a blank field
        // would surface as an empty card/bar. None should be empty.
        for (const auto& group : kCommandGroups)
        {
            VERIFY_IS_FALSE(group.title.empty());
            for (const auto& e : group.entries)
            {
                VERIFY_IS_FALSE(e.cmd.empty(), NoThrowString().Format(L"empty cmd in group '%s'", std::wstring{ group.title }.c_str()));
                VERIFY_IS_FALSE(e.tag.empty(), NoThrowString().Format(L"empty tag for '%s'", std::wstring{ e.cmd }.c_str()));
                VERIFY_IS_FALSE(e.desc.empty(), NoThrowString().Format(L"empty desc for '%s'", std::wstring{ e.cmd }.c_str()));
            }
        }
    }
}
