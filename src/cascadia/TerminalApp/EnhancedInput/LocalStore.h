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
        // claudeDir defaults to %USERPROFILE%\.claude; tests inject a temp dir.
        explicit LocalStore(std::filesystem::path claudeDir = {});

        // Read the whole file and parse the command array. Empty on any failure.
        std::vector<CustomCommand> Load() const noexcept;

        // Serialize the array and write it whole (create_directories first).
        // Returns false on any IO / encoding failure.
        bool Save(const std::vector<CustomCommand>& commands) const noexcept;

        const std::filesystem::path& FilePath() const noexcept { return _file; }

    private:
        std::filesystem::path _file;
    };
}
