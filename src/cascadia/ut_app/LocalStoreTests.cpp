// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.
//
// LocalStoreTests.cpp
//
// Bridge-layer tests for LocalStore (src/cascadia/TerminalApp/EnhancedInput/
// LocalStore.{h,cpp}) — the JSON persistence for the panel's custom commands
// (requirements §3.4 / §7 scenario 13). Each test runs against an injected
// temp directory (the ctor takes claudeDir), so nothing touches ~/.claude.
// Implementation symbols come from TerminalAppLib (linked via ProjectReference).

#include "precomp.h"

#include <filesystem>
#include <fstream>

#include "../TerminalApp/EnhancedInput/LocalStore.h"

using namespace WEX::Logging;
using namespace WEX::TestExecution;
using namespace WEX::Common;
using namespace winrt::TerminalApp::implementation;

namespace TerminalAppUnitTests
{
    class LocalStoreTests
    {
        TEST_CLASS(LocalStoreTests);

        TEST_METHOD(RoundTripPreservesAllFields);
        TEST_METHOD(RoundTripOfEmptyListIsEmpty);
        TEST_METHOD(LoadMissingFileReturnsEmpty);
        TEST_METHOD(LoadMalformedJsonReturnsEmpty);
        TEST_METHOD(LoadDropsRowsWithEmptyCmd);
        TEST_METHOD(SaveCreatesMissingParentDirs);
        TEST_METHOD(CommandsAndLayoutCanLiveInDifferentDirs);

        TEST_METHOD_SETUP(Setup);
        TEST_METHOD_CLEANUP(Cleanup);

        std::filesystem::path _dir;

        // Write raw bytes to the store's target file (for malformed-input tests).
        // Creates the dir first — Setup only clears it, so without this the write
        // would silently no-op and Load would pass for the wrong reason (missing
        // file rather than the content under test).
        void _writeRaw(const std::string& content)
        {
            std::error_code ec;
            std::filesystem::create_directories(_dir, ec);
            const auto path = LocalStore{ _dir }.FilePath();
            std::ofstream out{ path, std::ios::binary | std::ios::trunc };
            out << content;
            out.close();
            VERIFY_IS_TRUE(std::filesystem::exists(path), L"_writeRaw failed to create the test file");
        }
    };

    bool LocalStoreTests::Setup()
    {
        // Unique per-test dir under the OS temp root; removed in Cleanup.
        static int counter = 0;
        _dir = std::filesystem::temp_directory_path() / (L"ut_localstore_" + std::to_wstring(++counter));
        std::error_code ec;
        std::filesystem::remove_all(_dir, ec);
        return true;
    }

    bool LocalStoreTests::Cleanup()
    {
        std::error_code ec;
        std::filesystem::remove_all(_dir, ec);
        return true;
    }

    void LocalStoreTests::RoundTripPreservesAllFields()
    {
        LocalStore store{ _dir };
        std::vector<CustomCommand> in{
            { L"/deploy", L"部署", L"把当前分支部署到预览环境" },
            { L"claude --dangerously-skip-permissions", L"", L"跳过权限确认启动" }, // empty tag is allowed
        };
        VERIFY_IS_TRUE(store.Save(in));

        const auto out = store.Load();
        VERIFY_ARE_EQUAL(size_t{ 2 }, out.size());
        VERIFY_ARE_EQUAL(std::wstring{ L"/deploy" }, out[0].cmd);
        VERIFY_ARE_EQUAL(std::wstring{ L"部署" }, out[0].tag);
        VERIFY_ARE_EQUAL(std::wstring{ L"把当前分支部署到预览环境" }, out[0].desc);
        VERIFY_ARE_EQUAL(std::wstring{ L"claude --dangerously-skip-permissions" }, out[1].cmd);
        VERIFY_IS_TRUE(out[1].tag.empty());
        VERIFY_ARE_EQUAL(std::wstring{ L"跳过权限确认启动" }, out[1].desc);
    }

    void LocalStoreTests::RoundTripOfEmptyListIsEmpty()
    {
        LocalStore store{ _dir };
        VERIFY_IS_TRUE(store.Save({}));
        VERIFY_IS_TRUE(store.Load().empty());
    }

    void LocalStoreTests::LoadMissingFileReturnsEmpty()
    {
        // Fresh dir, nothing written yet — a first run has no file.
        LocalStore store{ _dir };
        VERIFY_IS_TRUE(store.Load().empty());
    }

    void LocalStoreTests::LoadMalformedJsonReturnsEmpty()
    {
        // A corrupt file must degrade to empty, never throw (requirements §7 #9).
        _writeRaw("this is not { valid json ]");
        VERIFY_IS_TRUE(LocalStore{ _dir }.Load().empty());
    }

    void LocalStoreTests::LoadDropsRowsWithEmptyCmd()
    {
        // cmd is the only required field; a row without it would render as a
        // blank card, so Load drops it and keeps the valid one.
        _writeRaw(R"([{"cmd":"","tag":"x","desc":"y"},{"cmd":"/ok","tag":"","desc":""}])");
        const auto out = LocalStore{ _dir }.Load();
        VERIFY_ARE_EQUAL(size_t{ 1 }, out.size());
        VERIFY_ARE_EQUAL(std::wstring{ L"/ok" }, out[0].cmd);
    }

    void LocalStoreTests::SaveCreatesMissingParentDirs()
    {
        // claudeDir may not exist yet (first-ever run before ~/.claude is made).
        const auto nested = _dir / L"deeper" / L"claude";
        LocalStore store{ nested };
        VERIFY_IS_TRUE(store.Save({ { L"/x", L"", L"" } }));
        VERIFY_IS_TRUE(std::filesystem::exists(store.FilePath()));
        VERIFY_ARE_EQUAL(size_t{ 1 }, store.Load().size());
    }

    void LocalStoreTests::CommandsAndLayoutCanLiveInDifferentDirs()
    {
        const auto cmdDir = _dir / L"grokroot";
        const auto layoutDir = _dir / L"clauderoot";
        LocalStore store{ cmdDir, layoutDir };
        VERIFY_IS_TRUE(store.Save({ { L"/x", L"标签", L"说明" } }));
        VERIFY_IS_TRUE(store.SavePanelWidth(400.0f));

        VERIFY_IS_TRUE(std::filesystem::exists(cmdDir / L"custom_commands.json"));
        VERIFY_IS_FALSE(std::filesystem::exists(cmdDir / L"enhanced_input_layout.json"));
        VERIFY_IS_TRUE(std::filesystem::exists(layoutDir / L"enhanced_input_layout.json"));
        VERIFY_IS_FALSE(std::filesystem::exists(layoutDir / L"custom_commands.json"));

        const auto loaded = LocalStore{ cmdDir, layoutDir }.Load();
        VERIFY_ARE_EQUAL(size_t{ 1 }, loaded.size());
        VERIFY_ARE_EQUAL(std::wstring{ L"/x" }, loaded[0].cmd);
        // Extra parens: TAEF VERIFY_ARE_EQUAL is a macro and would split on the ctor comma.
        VERIFY_ARE_EQUAL(400.0f, (LocalStore{ cmdDir, layoutDir }.LoadPanelWidth()));
    }
}
