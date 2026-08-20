// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace winrt::TerminalApp::implementation
{
    // One user-defined quick command (requirements §3.4). Unlike the built-in
    // CommandEntry (static wstring_view data), these are created at runtime and
    // own their storage. `cmd` is required; `tag` / `desc` are optional.
    struct CustomCommand
    {
        std::wstring cmd;
        std::wstring tag;
        std::wstring desc;
    };

    // Shared bridge (architecture §3, §4.5). Persists the user's custom commands
    // to ~/.claude/custom_commands.json as a plain JSON array, read/written whole
    // (the data is tiny — a handful of entries). Uses jsoncpp, already linked into
    // TerminalApp, so no new dependency. Pure C++ (no XAML / TermControl) so the
    // read/write round-trip is unit-testable with an injected temp directory.
    //
    // Failure is silent by contract: a missing / unreadable / malformed file yields
    // an empty vector and never throws; a failed Save returns false (requirements
    // §7 scenario 9 — a panel fault must never disrupt the terminal). Concurrent
    // writes are avoided upstream by the panel being single-instance and Save being
    // called serially on the UI thread.
    struct LocalStore
    {
        // Docked width of the panel, in DIPs. Persisted so the panel reopens at
        // the size the user dragged it to. Stored as pixels rather than a split
        // fraction because the panel's natural width is set by its content (a
        // 2-column card grid), not by the window: at 0.5 the panel took half of
        // a wide monitor. kMaxWidthFraction keeps it from dominating a small
        // window, and kMinWidth matches EnhancedInputContent::MinimumSize().
        static constexpr float kDefaultWidth = 400.0f;
        static constexpr float kMinWidth = 280.0f;
        static constexpr float kMaxWidthFraction = 0.75f;

        // commandsDir 默认 %USERPROFILE%\.claude；layoutDir 为空则与 commandsDir 相同
        // （保持旧单测：只注入一个 temp 目录时两个文件都在那里）。
        // Grok 模式传入 (grokDir, claudeDir)，让宽度仍写 Claude 目录。
        explicit LocalStore(std::filesystem::path commandsDir = {},
                            std::filesystem::path layoutDir = {});

        // Read the whole file and parse the command array. Empty on any failure.
        std::vector<CustomCommand> Load() const noexcept;

        // Serialize the array and write it whole (create_directories first).
        // Returns false on any IO / encoding failure.
        bool Save(const std::vector<CustomCommand>& commands) const noexcept;

        // Persisted panel width in DIPs. Returns kDefaultWidth when the file is
        // missing / malformed / out of range, so callers always get a usable
        // width without having to special-case first run.
        float LoadPanelWidth() const noexcept;

        // Write the panel width. Values below kMinWidth are ignored (a collapsed
        // or not-yet-laid-out pane reports ~0 and must not overwrite a good
        // width). Returns false on any IO failure.
        bool SavePanelWidth(float width) const noexcept;

        const std::filesystem::path& FilePath() const noexcept { return _file; }
        const std::filesystem::path& LayoutFilePath() const noexcept { return _layoutFile; }

    private:
        std::filesystem::path _file;
        std::filesystem::path _layoutFile;
    };
}
