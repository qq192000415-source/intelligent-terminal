// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "precomp.h"

#include <fstream>
#include "../TerminalApp/EnhancedInput/GitArchive.h"

using namespace WEX::Logging;
using namespace WEX::Common;
using namespace winrt::TerminalApp::implementation;

namespace TerminalAppUnitTests
{
    class GitArchiveTests
    {
        TEST_CLASS(GitArchiveTests);
        TEST_METHOD(AutoMessageContainsFolderName);
        TEST_METHOD(CommitInTempDirWhenGitPresent);
        TEST_METHOD(MissingGitReportsChinese);
        TEST_METHOD(EmptyCwdAsksToEnterFolder);

        TEST_METHOD_SETUP(Setup);
        TEST_METHOD_CLEANUP(Cleanup);
        std::filesystem::path _dir;
    };

    bool GitArchiveTests::Setup()
    {
        _dir = std::filesystem::temp_directory_path() / L"it-gitarchive-tests";
        std::error_code ec;
        std::filesystem::remove_all(_dir, ec);
        std::filesystem::create_directories(_dir, ec);
        return true;
    }

    bool GitArchiveTests::Cleanup()
    {
        std::error_code ec;
        std::filesystem::remove_all(_dir, ec);
        return true;
    }

    void GitArchiveTests::AutoMessageContainsFolderName()
    {
        const auto msg = GitArchive::AutoMessage(std::filesystem::path{ L"D:\\work\\my-notes" });
        VERIFY_IS_TRUE(msg.find(L"存档") != std::wstring::npos);
        VERIFY_IS_TRUE(msg.find(L"my-notes") != std::wstring::npos);
    }

    void GitArchiveTests::MissingGitReportsChinese()
    {
        GitArchive g;
        g.gitExe = L"C:\\definitely-no-git-here\\git.exe";
        g.cwd = _dir;
        const auto r = g.Commit(L"x", true);
        VERIFY_IS_FALSE(r.ok());
        VERIFY_IS_TRUE(r.message.find(L"Git") != std::wstring::npos);
    }

    void GitArchiveTests::EmptyCwdAsksToEnterFolder()
    {
        GitArchive g;
        g.gitExe = GitArchive::FindGit();
        if (g.gitExe.empty())
        {
            g.gitExe = L"C:\\Program Files\\Git\\cmd\\git.exe";
        }
        g.cwd.clear();
        const auto r = g.Run({ L"status" });
        VERIFY_IS_FALSE(r.ok());
        VERIFY_IS_TRUE(r.message.find(L"项目文件夹") != std::wstring::npos);
    }

    void GitArchiveTests::CommitInTempDirWhenGitPresent()
    {
        GitArchive g;
        g.gitExe = GitArchive::FindGit();
        if (g.gitExe.empty())
        {
            Log::Comment(L"git.exe not on PATH; skip live commit");
            return;
        }
        g.cwd = _dir;
        {
            std::ofstream f((_dir / L"hello.txt").string());
            f << "hi\n";
        }
        const auto r = g.Commit(L"", true);
        VERIFY_IS_TRUE(r.ok(), r.message.c_str());
        VERIFY_IS_TRUE(g.IsRepo());
        VERIFY_IS_FALSE(g.HasUncommitted());
        const auto log = g.Run({ L"log", L"-1", L"--pretty=%s" });
        VERIFY_IS_TRUE(log.ok());
        VERIFY_IS_TRUE(log.out.find(L"存档") != std::wstring::npos);
        VERIFY_IS_TRUE(log.out.find(_dir.filename().wstring()) != std::wstring::npos);
    }
}
