// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace winrt::TerminalApp::implementation
{
    struct Note
    {
        std::wstring title;
        std::wstring body;
        std::int64_t updated{ 0 };
    };

    // Persists notes to ~/.claude/notes.json. Inject a directory for tests so
    // nothing touches the real profile. Failure is silent (empty list / false).
    struct NoteStore
    {
        explicit NoteStore(std::filesystem::path dir = {});

        std::vector<Note> Load() const noexcept;
        bool Save(const std::vector<Note>& notes) const noexcept;

        const std::filesystem::path& FilePath() const noexcept { return _file; }

    private:
        std::filesystem::path _file;
    };
}
