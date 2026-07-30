// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.
//
// AttachmentStoreTests.cpp
//
// Bridge-layer tests for AttachmentStore (src/cascadia/TerminalApp/EnhancedInput/
// AttachmentStore.{h,cpp}) — magic-byte validation of pasted/dropped images and
// the double-threshold shot cleanup (architecture §4.4 / §6). Each test runs
// against an injected temp claude dir, so nothing touches ~/.claude/shots.

#include "precomp.h"

#include <filesystem>
#include <fstream>
#include <chrono>
#include <vector>

#include "../TerminalApp/EnhancedInput/AttachmentStore.h"

using namespace WEX::Logging;
using namespace WEX::TestExecution;
using namespace WEX::Common;
using namespace winrt::TerminalApp::implementation;

namespace TerminalAppUnitTests
{
    class AttachmentStoreTests
    {
        TEST_CLASS(AttachmentStoreTests);

        TEST_METHOD(DetectFormatRecognizesEachFormat);
        TEST_METHOD(DetectFormatRejectsUnknownAndEmpty);
        TEST_METHOD(SaveRejectsUnknownFormat);
        TEST_METHOD(SaveRejectsOversize);
        TEST_METHOD(SaveWritesValidPngToShots);
        TEST_METHOD(CleanupRemovesExcessByCount);
        TEST_METHOD(CleanupRemovesAgedShots);
        TEST_METHOD(CleanupOnlyTouchesShotFiles);
        TEST_METHOD(PurgeRemovesAllShotsAndReturnsCount);
        TEST_METHOD(PurgeSparesNonShotFiles);

        TEST_METHOD_SETUP(Setup);
        TEST_METHOD_CLEANUP(Cleanup);

        std::filesystem::path _dir;

        static std::vector<std::byte> _bytes(std::initializer_list<uint8_t> vals)
        {
            std::vector<std::byte> v;
            v.reserve(vals.size());
            for (auto b : vals)
            {
                v.push_back(static_cast<std::byte>(b));
            }
            return v;
        }

        // Minimal valid magic-byte prefixes for each accepted format.
        static std::vector<std::byte> _png() { return _bytes({ 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A }); }
        static std::vector<std::byte> _jpeg() { return _bytes({ 0xFF, 0xD8, 0xFF, 0xE0 }); }
        static std::vector<std::byte> _gif() { return _bytes({ 0x47, 0x49, 0x46, 0x38, 0x39, 0x61 }); }
        static std::vector<std::byte> _webp() { return _bytes({ 0x52, 0x49, 0x46, 0x46, 0, 0, 0, 0, 0x57, 0x45, 0x42, 0x50 }); }
        static std::vector<std::byte> _bmp() { return _bytes({ 0x42, 0x4D, 0x00, 0x00 }); }

        // Create <shotsDir>/<name> with the given byte size, then optionally age it.
        void _makeShot(const std::wstring& name, int daysOld = 0)
        {
            const auto store = AttachmentStore{ _dir };
            std::error_code ec;
            std::filesystem::create_directories(store.ShotsDir(), ec);
            const auto p = store.ShotsDir() / name;
            std::ofstream{ p, std::ios::binary } << "x";
            if (daysOld > 0)
            {
                std::filesystem::last_write_time(
                    p, std::filesystem::file_time_type::clock::now() - std::chrono::hours(24 * daysOld), ec);
            }
        }
    };

    bool AttachmentStoreTests::Setup()
    {
        static int counter = 0;
        _dir = std::filesystem::temp_directory_path() / (L"ut_attach_" + std::to_wstring(++counter));
        std::error_code ec;
        std::filesystem::remove_all(_dir, ec);
        std::filesystem::create_directories(_dir, ec);
        return true;
    }

    bool AttachmentStoreTests::Cleanup()
    {
        std::error_code ec;
        std::filesystem::remove_all(_dir, ec);
        return true;
    }

    void AttachmentStoreTests::DetectFormatRecognizesEachFormat()
    {
        VERIFY_IS_TRUE(ImageFormat::Png == AttachmentStore::DetectFormat(_png()));
        VERIFY_IS_TRUE(ImageFormat::Jpeg == AttachmentStore::DetectFormat(_jpeg()));
        VERIFY_IS_TRUE(ImageFormat::Gif == AttachmentStore::DetectFormat(_gif()));
        VERIFY_IS_TRUE(ImageFormat::Webp == AttachmentStore::DetectFormat(_webp()));
        VERIFY_IS_TRUE(ImageFormat::Bmp == AttachmentStore::DetectFormat(_bmp()));
        // Extension mapping tracks the detected format.
        VERIFY_ARE_EQUAL(std::wstring{ L"png" }, std::wstring{ AttachmentStore::ExtensionFor(ImageFormat::Png) });
        VERIFY_ARE_EQUAL(std::wstring{ L"jpg" }, std::wstring{ AttachmentStore::ExtensionFor(ImageFormat::Jpeg) });
    }

    void AttachmentStoreTests::DetectFormatRejectsUnknownAndEmpty()
    {
        VERIFY_IS_TRUE(ImageFormat::Unknown == AttachmentStore::DetectFormat(_bytes({ 0x00, 0x01, 0x02, 0x03 })));
        // RIFF header but NOT WebP (e.g. a WAV) must not be taken for an image.
        VERIFY_IS_TRUE(ImageFormat::Unknown ==
                       AttachmentStore::DetectFormat(_bytes({ 0x52, 0x49, 0x46, 0x46, 0, 0, 0, 0, 0x57, 0x41, 0x56, 0x45 })));
        VERIFY_IS_TRUE(ImageFormat::Unknown == AttachmentStore::DetectFormat({}));
    }

    void AttachmentStoreTests::SaveRejectsUnknownFormat()
    {
        // A forged/unknown header is rejected before touching disk.
        const AttachmentStore store{ _dir };
        const auto bad = _bytes({ 0xDE, 0xAD, 0xBE, 0xEF });
        VERIFY_IS_FALSE(store.SaveImageBytes(bad).has_value());
    }

    void AttachmentStoreTests::SaveRejectsOversize()
    {
        // Valid PNG magic but over the 20 MB cap => rejected (no partial file).
        const AttachmentStore store{ _dir };
        std::vector<std::byte> big(AttachmentStore::MaxImageBytes + 1, std::byte{ 0 });
        const auto png = _png();
        std::copy(png.begin(), png.end(), big.begin());
        VERIFY_IS_FALSE(store.SaveImageBytes(big).has_value());
    }

    void AttachmentStoreTests::SaveWritesValidPngToShots()
    {
        const AttachmentStore store{ _dir };
        const auto saved = store.SaveImageBytes(_png());
        VERIFY_IS_TRUE(saved.has_value());
        // Path is inside shots\, named shot_*.png, and the file exists on disk.
        const std::filesystem::path p{ *saved };
        VERIFY_IS_TRUE(std::filesystem::exists(p));
        VERIFY_ARE_EQUAL(store.ShotsDir(), p.parent_path());
        VERIFY_IS_TRUE(p.filename().wstring().starts_with(L"shot_"));
        VERIFY_ARE_EQUAL(std::wstring{ L".png" }, p.extension().wstring());
    }

    void AttachmentStoreTests::CleanupRemovesExcessByCount()
    {
        // More than the count cap => cleanup trims down to exactly the cap.
        const size_t over = AttachmentStore::MaxShotCount + 5;
        for (size_t i = 0; i < over; ++i)
        {
            _makeShot(L"shot_" + std::to_wstring(i) + L".png");
        }
        AttachmentStore{ _dir }.CleanupShots();

        size_t remaining = 0;
        for (const auto& e : std::filesystem::directory_iterator{ AttachmentStore{ _dir }.ShotsDir() })
        {
            if (e.path().filename().wstring().starts_with(L"shot_"))
            {
                ++remaining;
            }
        }
        VERIFY_ARE_EQUAL(AttachmentStore::MaxShotCount, remaining);
    }

    void AttachmentStoreTests::CleanupRemovesAgedShots()
    {
        // A shot older than the age cap is removed; a fresh one is kept.
        _makeShot(L"shot_old.png", AttachmentStore::MaxShotAgeDays + 10);
        _makeShot(L"shot_new.png", 0);
        AttachmentStore{ _dir }.CleanupShots();

        const auto shots = AttachmentStore{ _dir }.ShotsDir();
        VERIFY_IS_FALSE(std::filesystem::exists(shots / L"shot_old.png"));
        VERIFY_IS_TRUE(std::filesystem::exists(shots / L"shot_new.png"));
    }

    void AttachmentStoreTests::CleanupOnlyTouchesShotFiles()
    {
        // A non-shot file, even if aged, must never be deleted by cleanup.
        _makeShot(L"keep_me.txt", AttachmentStore::MaxShotAgeDays + 10);
        _makeShot(L"shot_gone.png", AttachmentStore::MaxShotAgeDays + 10);
        AttachmentStore{ _dir }.CleanupShots();

        const auto shots = AttachmentStore{ _dir }.ShotsDir();
        VERIFY_IS_TRUE(std::filesystem::exists(shots / L"keep_me.txt"), L"non-shot file must be preserved");
        VERIFY_IS_FALSE(std::filesystem::exists(shots / L"shot_gone.png"));
    }

    void AttachmentStoreTests::PurgeRemovesAllShotsAndReturnsCount()
    {
        // The manual "clear cache" button: delete every shot NOW, ignoring the
        // age/count thresholds (all three here are fresh — CleanupShots would keep them).
        _makeShot(L"shot_1.png");
        _makeShot(L"shot_2.png");
        _makeShot(L"shot_3.bmp");
        const auto removed = AttachmentStore{ _dir }.PurgeAllShots();
        VERIFY_ARE_EQUAL(size_t{ 3 }, removed);

        size_t left = 0;
        for (const auto& e : std::filesystem::directory_iterator{ AttachmentStore{ _dir }.ShotsDir() })
        {
            if (e.path().filename().wstring().starts_with(L"shot_"))
            {
                ++left;
            }
        }
        VERIFY_ARE_EQUAL(size_t{ 0 }, left);
    }

    void AttachmentStoreTests::PurgeSparesNonShotFiles()
    {
        // Purge must only ever touch our own shot_ files.
        _makeShot(L"keep_me.txt");
        _makeShot(L"shot_x.png");
        const auto removed = AttachmentStore{ _dir }.PurgeAllShots();
        VERIFY_ARE_EQUAL(size_t{ 1 }, removed);

        const auto shots = AttachmentStore{ _dir }.ShotsDir();
        VERIFY_IS_TRUE(std::filesystem::exists(shots / L"keep_me.txt"), L"non-shot file must survive purge");
        VERIFY_IS_FALSE(std::filesystem::exists(shots / L"shot_x.png"));
    }
}
