// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace winrt::TerminalApp::implementation
{
    struct GitRun
    {
        int exitCode{ -1 };
        std::wstring out;
        std::wstring err;
        std::wstring message; // 给人看的中文；成功也可填
        bool ok() const noexcept { return exitCode == 0; }
    };

    // Pure C++ git helper for the plugin marketplace "云端存档" pane.
    // Inject gitExe + cwd so tests never touch the user's real repos.
    struct GitArchive
    {
        std::filesystem::path gitExe;
        std::filesystem::path cwd;

        static std::filesystem::path FindGit();
        static std::wstring AutoMessage(const std::filesystem::path& folder);

        bool Installed() const;
        GitRun Run(const std::vector<std::wstring>& args) const;

        bool IsRepo() const;
        GitRun InitIfNeeded() const;
        bool HasRemote() const;
        std::wstring RemoteName() const; // empty if none; "github" preferred then "origin"
        bool HasUncommitted() const;
        bool HasUnpushed() const;

        GitRun Commit(const std::wstring& message, bool addAll) const;
        GitRun Push() const;
        GitRun Pull() const;
        GitRun SetRemote(const std::wstring& url) const;
    };
}
