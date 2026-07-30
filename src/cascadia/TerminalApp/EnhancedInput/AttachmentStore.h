// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace winrt::TerminalApp::implementation
{
    // Recognised image formats (architecture §4.4). Unknown => rejected.
    enum class ImageFormat
    {
        Unknown,
        Png,
        Jpeg,
        Gif,
        Webp,
        Bmp,
    };

    // Shared bridge (architecture §3). Persists pasted / dropped images to
    // ~/.claude/shots/ and hands back a local path, because the PTY can't carry
    // binary attachments — we send the path as plain text instead. Pure C++ (no
    // XAML / TermControl) so the magic-byte + cleanup logic is unit-testable with
    // an injected temp directory.
    struct AttachmentStore
    {
        // Reject anything larger than this (mirrors the old project's 20 MB cap).
        static constexpr size_t MaxImageBytes = 20ull * 1024ull * 1024ull;
        // Double-threshold retention for shot cleanup (architecture §6).
        static constexpr size_t MaxShotCount = 200;
        static constexpr int MaxShotAgeDays = 30;

        // claudeDir defaults to %USERPROFILE%\.claude; tests inject a temp dir.
        explicit AttachmentStore(std::filesystem::path claudeDir = {});

        // Detect format from the leading magic bytes. Unknown => reject.
        static ImageFormat DetectFormat(std::span<const std::byte> bytes) noexcept;
        static std::wstring_view ExtensionFor(ImageFormat fmt) noexcept;

        // Validate (magic byte + size) then persist to shots\shot_<uuid>.<ext>.
        // Returns the saved absolute path, or nullopt on rejection / IO failure.
        std::optional<std::wstring> SaveImageBytes(std::span<const std::byte> bytes) const;

        // Drop shots older than MaxShotAgeDays or beyond MaxShotCount (whichever
        // hits first). Only touches shot_<uuid>.<ext> we created; never throws.
        // Runs automatically on panel startup + after each new shot lands.
        void CleanupShots() const noexcept;

        // Delete EVERY shot_<uuid>.<ext> now, ignoring the age/count thresholds —
        // the manual "clear screenshot cache" button (architecture §6 fallback).
        // Only touches our own shot_ files; returns how many were removed; never throws.
        size_t PurgeAllShots() const noexcept;

        const std::filesystem::path& ShotsDir() const noexcept { return _shotsDir; }

    private:
        std::filesystem::path _shotsDir;
    };
}
