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
        // OSC7 file:// URI or a Windows path → absolute directory. Empty if unusable.
        static std::filesystem::path NormalizeWorkDir(std::wstring wd);
        static std::wstring AutoMessage(const std::filesystem::path& folder);
        // Non-empty when Git Credential Manager already has a github.com user.
        // Never prints or returns the secret. Times out instead of hanging the UI.
        static std::wstring ProbeGithubUser();
        // Opens the browser login UI (GCM). Does not wait.
        static bool StartGithubLogin();
        static std::wstring SanitizeRepoName(const std::wstring& folderLeaf);
        static bool IsRemoteGone(const GitRun& r);
        // Creates github.com/user/<name>. Does not log the token. On success, `out` is the clone URL.
        GitRun CreateGithubRepo(const std::wstring& name, bool isPrivate) const;

        bool Installed() const;
        GitRun Run(const std::vector<std::wstring>& args) const;

        bool IsRepo() const;
        GitRun InitIfNeeded() const;
        bool HasRemote() const;
        std::wstring RemoteName() const; // empty if none; "github" preferred then "origin"
        bool HasUncommitted() const;
        bool HasUnpushed() const;

        std::wstring CurrentBranch() const;
        std::vector<std::wstring> LocalBranches() const;
        GitRun Checkout(const std::wstring& branch) const;
        GitRun CreateAndCheckoutBranch(const std::wstring& branch) const;
        static std::wstring SanitizeBranchName(const std::wstring& raw);
        // added/deleted vs HEAD (includeUnstaged) or index only.
        void DiffCounts(bool includeUnstaged, int& added, int& deleted) const;

        GitRun Commit(const std::wstring& message, bool addAll) const;
        GitRun Push() const;
        GitRun Pull() const;
        GitRun SetRemote(const std::wstring& url) const;

        bool HasHead() const;
        bool TagExists(const std::wstring& name) const;
        std::vector<std::wstring> LocalTags() const;
        GitRun FetchTags() const;
        GitRun Tag(const std::wstring& raw) const; // lightweight; refuses dirty / duplicate
        GitRun PushTag(const std::wstring& name) const;
        GitRun ResetHardToTag(const std::wstring& name) const;
        static std::wstring SanitizeTagName(const std::wstring& raw);

        struct LogLine
        {
            std::wstring hash;
            std::wstring message;
            std::wstring display; // "hash message"，和 git log --oneline 一行一样
        };
        std::vector<LogLine> LogOneline(int maxN = 50) const;
        GitRun ResetHardToCommit(const std::wstring& hash) const;
        // Local git tag -d. alsoRemote: git push <remote> --delete refs/tags/<name>
        GitRun DeleteTag(const std::wstring& name, bool alsoRemote) const;

        // owner/repo from github.com remote URL. False if not GitHub.
        static bool ParseGithubUrl(const std::wstring& url, std::wstring& owner, std::wstring& repo);
        std::wstring RemoteUrl() const;
        // Create/update GitHub Release for tag and upload a local installer. Does not git-add the file.
        GitRun UploadReleaseAsset(const std::wstring& tag, const std::filesystem::path& file) const;
        // Newest first. Skips node_modules/.git and tiny stub launchers.
        static std::vector<std::filesystem::path> FindInstallers(const std::filesystem::path& root);
    };
}
