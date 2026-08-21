// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "precomp.h"

#include <filesystem>
#include <fstream>

#include "../TerminalApp/EnhancedInput/NoteStore.h"

using namespace WEX::Logging;
using namespace WEX::TestExecution;
using namespace WEX::Common;
using namespace winrt::TerminalApp::implementation;

namespace TerminalAppUnitTests
{
    class NoteStoreTests
    {
        TEST_CLASS(NoteStoreTests);

        TEST_METHOD(RoundTripPreservesTitleBodyUpdated);
        TEST_METHOD(RoundTripEmptyTitle);
        TEST_METHOD(LoadMissingFileReturnsEmpty);
        TEST_METHOD(LoadMalformedJsonReturnsEmpty);
        TEST_METHOD(LoadDropsRowsWithEmptyBody);
        TEST_METHOD(SaveSkipsEmptyBody);
        TEST_METHOD(SaveCreatesMissingParentDirs);

        TEST_METHOD_SETUP(Setup);
        TEST_METHOD_CLEANUP(Cleanup);

        std::filesystem::path _dir;

        void _writeRaw(const std::string& content)
        {
            std::error_code ec;
            std::filesystem::create_directories(_dir, ec);
            const auto path = NoteStore{ _dir }.FilePath();
            std::ofstream out{ path, std::ios::binary | std::ios::trunc };
            out << content;
            out.close();
            VERIFY_IS_TRUE(std::filesystem::exists(path));
        }
    };

    bool NoteStoreTests::Setup()
    {
        static int counter = 0;
        _dir = std::filesystem::temp_directory_path() / (L"ut_notestore_" + std::to_wstring(++counter));
        std::error_code ec;
        std::filesystem::remove_all(_dir, ec);
        return true;
    }

    bool NoteStoreTests::Cleanup()
    {
        std::error_code ec;
        std::filesystem::remove_all(_dir, ec);
        return true;
    }

    void NoteStoreTests::RoundTripPreservesTitleBodyUpdated()
    {
        NoteStore store{ _dir };
        std::vector<Note> in{ { L"代码审查", L"先指出缺陷。", 1700000000 } };
        VERIFY_IS_TRUE(store.Save(in));
        const auto out = store.Load();
        VERIFY_ARE_EQUAL(static_cast<size_t>(1), out.size());
        VERIFY_ARE_EQUAL(L"代码审查", out[0].title);
        VERIFY_ARE_EQUAL(L"先指出缺陷。", out[0].body);
        VERIFY_ARE_EQUAL(static_cast<int64_t>(1700000000), out[0].updated);
    }

    void NoteStoreTests::RoundTripEmptyTitle()
    {
        NoteStore store{ _dir };
        VERIFY_IS_TRUE(store.Save({ { L"", L"只有正文", 1 } }));
        const auto out = store.Load();
        VERIFY_ARE_EQUAL(static_cast<size_t>(1), out.size());
        VERIFY_ARE_EQUAL(L"", out[0].title);
        VERIFY_ARE_EQUAL(L"只有正文", out[0].body);
    }

    void NoteStoreTests::LoadMissingFileReturnsEmpty()
    {
        VERIFY_ARE_EQUAL(static_cast<size_t>(0), NoteStore{ _dir }.Load().size());
    }

    void NoteStoreTests::LoadMalformedJsonReturnsEmpty()
    {
        _writeRaw("{not json");
        VERIFY_ARE_EQUAL(static_cast<size_t>(0), NoteStore{ _dir }.Load().size());
    }

    void NoteStoreTests::LoadDropsRowsWithEmptyBody()
    {
        _writeRaw(R"([{"title":"x","body":""},{"title":"y","body":"ok"}])");
        const auto out = NoteStore{ _dir }.Load();
        VERIFY_ARE_EQUAL(static_cast<size_t>(1), out.size());
        VERIFY_ARE_EQUAL(L"ok", out[0].body);
    }

    void NoteStoreTests::SaveSkipsEmptyBody()
    {
        NoteStore store{ _dir };
        VERIFY_IS_TRUE(store.Save({ { L"t", L"", 1 }, { L"", L"keep", 2 } }));
        const auto out = store.Load();
        VERIFY_ARE_EQUAL(static_cast<size_t>(1), out.size());
        VERIFY_ARE_EQUAL(L"keep", out[0].body);
    }

    void NoteStoreTests::SaveCreatesMissingParentDirs()
    {
        const auto nested = _dir / L"a" / L"b";
        NoteStore store{ nested };
        VERIFY_IS_TRUE(store.Save({ { L"", L"hi", 3 } }));
        VERIFY_IS_TRUE(std::filesystem::exists(store.FilePath()));
    }
}
