// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "AttachmentStore.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <vector>

using namespace std::filesystem;

namespace winrt::TerminalApp::implementation
{
    static std::filesystem::path _defaultClaudeDir()
    {
        wchar_t profile[MAX_PATH]{};
        if (GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH) > 0)
        {
            return std::filesystem::path{ profile } / L".claude";
        }
        return std::filesystem::path{ L".claude" };
    }

    AttachmentStore::AttachmentStore(std::filesystem::path claudeDir)
    {
        if (claudeDir.empty())
        {
            claudeDir = _defaultClaudeDir();
        }
        _shotsDir = claudeDir / L"shots";
    }

    // Compare the leading bytes against each format's signature. WEBP needs the
    // "WEBP" tag at offset 8 (the RIFF header alone isn't enough).
    ImageFormat AttachmentStore::DetectFormat(std::span<const std::byte> b) noexcept
    {
        const auto has = [&](std::initializer_list<uint8_t> sig, size_t off = 0) noexcept {
            if (b.size() < off + sig.size())
            {
                return false;
            }
            size_t i = off;
            for (const auto x : sig)
            {
                if (std::to_integer<uint8_t>(b[i++]) != x)
                {
                    return false;
                }
            }
            return true;
        };

        if (has({ 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A }))
        {
            return ImageFormat::Png;
        }
        if (has({ 0xFF, 0xD8, 0xFF }))
        {
            return ImageFormat::Jpeg;
        }
        if (has({ 0x47, 0x49, 0x46, 0x38 }))
        {
            return ImageFormat::Gif;
        }
        if (has({ 0x52, 0x49, 0x46, 0x46 }) && has({ 0x57, 0x45, 0x42, 0x50 }, 8))
        {
            return ImageFormat::Webp;
        }
        if (has({ 0x42, 0x4D }))
        {
            return ImageFormat::Bmp;
        }
        return ImageFormat::Unknown;
    }

    std::wstring_view AttachmentStore::ExtensionFor(ImageFormat fmt) noexcept
    {
        switch (fmt)
        {
        case ImageFormat::Png:
            return L"png";
        case ImageFormat::Jpeg:
            return L"jpg";
        case ImageFormat::Gif:
            return L"gif";
        case ImageFormat::Webp:
            return L"webp";
        case ImageFormat::Bmp:
            return L"bmp";
        default:
            return L"";
        }
    }

    std::optional<std::wstring> AttachmentStore::SaveImageBytes(std::span<const std::byte> bytes) const
    {
        // Reject by size and by magic byte before touching the disk.
        if (bytes.empty() || bytes.size() > MaxImageBytes)
        {
            return std::nullopt;
        }
        const auto fmt = DetectFormat(bytes);
        if (fmt == ImageFormat::Unknown)
        {
            return std::nullopt;
        }

        std::error_code ec;
        create_directories(_shotsDir, ec);
        if (ec)
        {
            return std::nullopt;
        }

        GUID guid{};
        if (FAILED(CoCreateGuid(&guid)))
        {
            return std::nullopt;
        }
        wchar_t uuid[40]{};
        swprintf_s(uuid, L"%08lx%04hx%04hx%02x%02x%02x%02x%02x%02x%02x%02x",
                   guid.Data1, guid.Data2, guid.Data3,
                   guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
                   guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);

        auto dest = _shotsDir / (std::wstring{ L"shot_" } + uuid + L"." + std::wstring{ ExtensionFor(fmt) });

        std::ofstream out{ dest, std::ios::binary | std::ios::trunc };
        if (!out)
        {
            return std::nullopt;
        }
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        out.close();
        if (!out)
        {
            std::filesystem::remove(dest, ec);
            return std::nullopt;
        }
        return dest.wstring();
    }

    void AttachmentStore::CleanupShots() const noexcept
    try
    {
        std::error_code ec;
        if (!exists(_shotsDir, ec))
        {
            return;
        }

        struct Shot
        {
            std::filesystem::path path;
            file_time_type mtime;
        };
        std::vector<Shot> shots;

        for (const auto& entry : directory_iterator{ _shotsDir, ec })
        {
            if (ec || !entry.is_regular_file(ec))
            {
                continue;
            }
            // Only our own shot_<uuid>.<ext> files — never touch anything else.
            const auto name = entry.path().filename().wstring();
            if (!name.starts_with(L"shot_"))
            {
                continue;
            }
            const auto t = entry.last_write_time(ec);
            if (ec)
            {
                continue;
            }
            shots.push_back({ entry.path(), t });
        }

        // Newest first, so anything past the count cap is the oldest tail.
        std::sort(shots.begin(), shots.end(), [](const Shot& a, const Shot& b) noexcept {
            return a.mtime > b.mtime;
        });

        const auto now = file_time_type::clock::now();
        const auto maxAge = std::chrono::hours{ 24 * MaxShotAgeDays };
        for (size_t i = 0; i < shots.size(); ++i)
        {
            const bool tooMany = i >= MaxShotCount;
            const bool tooOld = (now - shots[i].mtime) > maxAge;
            if (tooMany || tooOld)
            {
                std::error_code delEc;
                std::filesystem::remove(shots[i].path, delEc); // best-effort; ignore failures
            }
        }
    }
    catch (...)
    {
        // Cleanup is best-effort and must never disrupt the panel / terminal.
    }

    size_t AttachmentStore::PurgeAllShots() const noexcept
    try
    {
        std::error_code ec;
        if (!exists(_shotsDir, ec))
        {
            return 0;
        }

        size_t removed = 0;
        for (const auto& entry : directory_iterator{ _shotsDir, ec })
        {
            if (ec || !entry.is_regular_file(ec))
            {
                continue;
            }
            // Same guard as CleanupShots: only ever delete our own shot_ files.
            if (!entry.path().filename().wstring().starts_with(L"shot_"))
            {
                continue;
            }
            std::error_code delEc;
            if (std::filesystem::remove(entry.path(), delEc) && !delEc)
            {
                ++removed;
            }
        }
        return removed;
    }
    catch (...)
    {
        return 0; // best-effort; must never disrupt the panel / terminal
    }
}
