// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "precomp.h"

#include <algorithm>
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
        TEST_METHOD(ProbeGithubUserDoesNotThrow);
        TEST_METHOD(SanitizeRepoNameReplacesSpaces);
        TEST_METHOD(GoneRemoteMapsAndSetUrl);
        TEST_METHOD(CreateBranchAndDiffCounts);
        TEST_METHOD(TagAndResetHard);
        TEST_METHOD(ParseGithubUrlAndUploadRejects);
        TEST_METHOD(FindInstallersPicksReleaseZip);
        TEST_METHOD(LogOnelineResetAndDeleteTag);
        TEST_METHOD(NormalizeWorkDirFileUri);

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

    void GitArchiveTests::ProbeGithubUserDoesNotThrow()
    {
        const auto user = GitArchive::ProbeGithubUser();
        Log::Comment((L"probe user=[" + user + L"]").c_str());
    }

    void GitArchiveTests::SanitizeRepoNameReplacesSpaces()
    {
        VERIFY_ARE_EQUAL(std::wstring{ L"my-notes" }, GitArchive::SanitizeRepoName(L"my notes"));
        VERIFY_ARE_EQUAL(std::wstring{ L"archive" }, GitArchive::SanitizeRepoName(L"..."));
        VERIFY_ARE_EQUAL(std::wstring{ L"it-codex-accept" }, GitArchive::SanitizeRepoName(L"it-codex-accept"));
        VERIFY_ARE_EQUAL(std::wstring{ L"archive" }, GitArchive::SanitizeRepoName(L"一拖多动销二维码工具1.0员工版"));
        VERIFY_ARE_EQUAL(std::wstring{ L"archive" }, GitArchive::SanitizeRepoName(L"1.0-"));
        VERIFY_ARE_EQUAL(std::wstring{ L"archive" }, GitArchive::SanitizeRepoName(L"1.0"));
        VERIFY_ARE_EQUAL(std::wstring{ L"qr-tool-1.0" }, GitArchive::SanitizeRepoName(L"qr-tool-1.0"));
        VERIFY_ARE_EQUAL(std::wstring{ L"qr-staff" }, GitArchive::SanitizeRepoName(L"qr staff"));
    }

    void GitArchiveTests::GoneRemoteMapsAndSetUrl()
    {
        GitRun gone;
        gone.err = L"remote: Repository not found.\nfatal: repository 'https://github.com/x/1.0-.git/' not found\n";
        gone.message = L"网上仓库已经删了。可以重新建一个再推。";
        VERIFY_IS_TRUE(GitArchive::IsRemoteGone(gone));

        GitRun http404;
        http404.err = L"fatal: unable to access 'https://github.com/x/y.git/': The requested URL returned error: 404\n";
        VERIFY_IS_TRUE(GitArchive::IsRemoteGone(http404));

        GitRun net;
        net.message = L"连不上 GitHub，检查网络。";
        net.err = L"unable to access 'https://github.com/x/y.git/': Could not resolve host\n";
        VERIFY_IS_FALSE(GitArchive::IsRemoteGone(net));

        GitRun perm;
        perm.message = L"当前登录的 GitHub 账号没有推这个仓库的权限。";
        perm.err = L"remote: Permission denied to user.\n";
        VERIFY_IS_FALSE(GitArchive::IsRemoteGone(perm));

        GitArchive g;
        g.gitExe = GitArchive::FindGit();
        if (g.gitExe.empty())
        {
            Log::Comment(L"git.exe not on PATH; skip set-url");
            return;
        }
        g.cwd = _dir;
        {
            std::ofstream f((_dir / L"a.txt").string());
            f << "a\n";
        }
        VERIFY_IS_TRUE(g.Commit(L"init", true).ok());
        auto add = g.SetRemote(L"https://github.com/example/gone-repo.git");
        VERIFY_IS_TRUE(add.ok(), add.message.c_str());
        VERIFY_IS_TRUE(g.HasRemote());
        auto upd = g.SetRemote(L"https://github.com/example/new-repo.git");
        VERIFY_IS_TRUE(upd.ok(), upd.message.c_str());
        const auto url = g.RemoteUrl();
        VERIFY_IS_TRUE(url.find(L"new-repo") != std::wstring::npos, url.c_str());
        VERIFY_IS_TRUE(url.find(L"gone-repo") == std::wstring::npos, url.c_str());
    }

    void GitArchiveTests::CreateBranchAndDiffCounts()
    {
        GitArchive g;
        g.gitExe = GitArchive::FindGit();
        if (g.gitExe.empty())
        {
            Log::Comment(L"git.exe not on PATH; skip");
            return;
        }
        g.cwd = _dir;
        {
            std::ofstream f((_dir / L"a.txt").string());
            f << "one\ntwo\nthree\n";
        }
        VERIFY_IS_TRUE(g.Commit(L"init", true).ok());
        {
            std::ofstream f((_dir / L"a.txt").string());
            f << "one\ntwo\nthree\nfour\n";
        }
        int added = 0, deleted = 0;
        g.DiffCounts(true, added, deleted);
        VERIFY_IS_TRUE(added >= 1);
        auto r = g.CreateAndCheckoutBranch(L"feature test");
        VERIFY_IS_TRUE(r.ok(), r.message.c_str());
        VERIFY_ARE_EQUAL(std::wstring{ L"feature-test" }, g.CurrentBranch());
        VERIFY_ARE_EQUAL(std::wstring{ L"feat/x" }, GitArchive::SanitizeBranchName(L"feat/x"));
    }

    void GitArchiveTests::TagAndResetHard()
    {
        GitArchive g;
        g.gitExe = GitArchive::FindGit();
        if (g.gitExe.empty())
        {
            Log::Comment(L"git.exe not on PATH; skip");
            return;
        }
        g.cwd = _dir;
        {
            std::ofstream f((_dir / L"v.txt").string());
            f << "one\n";
        }
        VERIFY_IS_TRUE(g.Commit(L"v1", true).ok());
        auto t1 = g.Tag(L"stable 1.3.10");
        VERIFY_IS_TRUE(t1.ok(), t1.message.c_str());
        VERIFY_ARE_EQUAL(std::wstring{ L"stable-1.3.10" }, GitArchive::SanitizeTagName(L"stable 1.3.10"));
        VERIFY_ARE_EQUAL(std::wstring{ L"V0.78小旋风版" }, GitArchive::SanitizeTagName(L"V0.78小旋风版"));
        VERIFY_ARE_EQUAL(std::wstring{ L"V0.78-小旋风版" }, GitArchive::SanitizeTagName(L"V0.78 小旋风版"));
        VERIFY_ARE_EQUAL(std::wstring{ L"功能/登录" }, GitArchive::SanitizeBranchName(L"功能/登录"));
        VERIFY_IS_TRUE(g.TagExists(L"stable-1.3.10"));
        auto zh = g.Tag(L"V0.78小旋风版");
        VERIFY_IS_TRUE(zh.ok(), zh.message.c_str());
        VERIFY_IS_TRUE(zh.message.find(L"V0.78小旋风版") != std::wstring::npos, zh.message.c_str());
        VERIFY_IS_TRUE(g.TagExists(L"V0.78小旋风版"));
        const auto listed = g.LocalTags();
        bool hasZh = false;
        for (const auto& t : listed)
        {
            hasZh = hasZh || t == L"V0.78小旋风版";
        }
        VERIFY_IS_TRUE(hasZh);
        VERIFY_IS_TRUE(!listed.empty());
        VERIFY_ARE_EQUAL(listed.front(), std::wstring{ L"V0.78小旋风版" });
        {
            std::ofstream f((_dir / L"v.txt").string());
            f << "two\n";
        }
        auto dirty = g.Tag(L"stable-1.3.11");
        VERIFY_IS_FALSE(dirty.ok());
        VERIFY_IS_TRUE(dirty.message.find(L"没提交") != std::wstring::npos, dirty.message.c_str());
        VERIFY_IS_TRUE(g.Commit(L"v2", true).ok());
        auto t2 = g.Tag(L"stable-1.3.11");
        VERIFY_IS_TRUE(t2.ok(), t2.message.c_str());
        auto dup = g.Tag(L"stable-1.3.11");
        VERIFY_IS_FALSE(dup.ok());
        VERIFY_IS_TRUE(dup.message.find(L"已经有") != std::wstring::npos, dup.message.c_str());
        const auto tags = g.LocalTags();
        bool has10 = false, has11 = false;
        for (const auto& t : tags)
        {
            has10 = has10 || t == L"stable-1.3.10";
            has11 = has11 || t == L"stable-1.3.11";
        }
        VERIFY_IS_TRUE(has10 && has11);
        auto back = g.ResetHardToTag(L"stable-1.3.10");
        VERIFY_IS_TRUE(back.ok(), back.message.c_str());
        VERIFY_IS_TRUE(back.message.find(L"不要点") != std::wstring::npos);
        std::ifstream in((_dir / L"v.txt").string());
        std::string body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        VERIFY_IS_TRUE(body.find("one") != std::string::npos);
        VERIFY_IS_TRUE(body.find("two") == std::string::npos);
    }

    void GitArchiveTests::ParseGithubUrlAndUploadRejects()
    {
        std::wstring owner;
        std::wstring repo;
        VERIFY_IS_TRUE(GitArchive::ParseGithubUrl(L"https://github.com/88lin/gpt-image-studio.git", owner, repo));
        VERIFY_ARE_EQUAL(std::wstring{ L"88lin" }, owner);
        VERIFY_ARE_EQUAL(std::wstring{ L"gpt-image-studio" }, repo);
        VERIFY_IS_TRUE(GitArchive::ParseGithubUrl(L"git@github.com:qq192000415-source/intelligent-terminal.git", owner, repo));
        VERIFY_ARE_EQUAL(std::wstring{ L"qq192000415-source" }, owner);
        VERIFY_ARE_EQUAL(std::wstring{ L"intelligent-terminal" }, repo);
        VERIFY_IS_FALSE(GitArchive::ParseGithubUrl(L"https://gitlab.com/x/y.git", owner, repo));

        GitArchive g;
        g.gitExe = GitArchive::FindGit();
        if (g.gitExe.empty())
        {
            Log::Comment(L"git.exe not on PATH; skip remaining");
            return;
        }
        g.cwd = _dir;
        {
            std::ofstream f((_dir / L"a.txt").string());
            f << "a\n";
        }
        VERIFY_IS_TRUE(g.Commit(L"v1", true).ok());
        auto noTag = g.UploadReleaseAsset(L"stable-1.0.0", _dir / L"a.txt");
        VERIFY_IS_FALSE(noTag.ok());
        VERIFY_IS_TRUE(noTag.message.find(L"版本标签") != std::wstring::npos, noTag.message.c_str());
        VERIFY_IS_TRUE(g.Tag(L"stable-1.0.0").ok());
        auto missing = g.UploadReleaseAsset(L"stable-1.0.0", _dir / L"no-such.bin");
        VERIFY_IS_FALSE(missing.ok());
        VERIFY_IS_TRUE(missing.message.find(L"找不到") != std::wstring::npos, missing.message.c_str());
        auto noRemote = g.UploadReleaseAsset(L"stable-1.0.0", _dir / L"a.txt");
        VERIFY_IS_FALSE(noRemote.ok());
        VERIFY_IS_TRUE(noRemote.message.find(L"网上仓库") != std::wstring::npos, noRemote.message.c_str());
    }

    void GitArchiveTests::FindInstallersPicksReleaseZip()
    {
        std::filesystem::create_directories(_dir / L"release");
        std::filesystem::create_directories(_dir / L"node_modules");
        {
            std::ofstream big((_dir / L"release" / L"app.zip").string(), std::ios::binary);
            const std::string blob(70 * 1024, 'a');
            big.write(blob.data(), static_cast<std::streamsize>(blob.size()));
        }
        {
            std::ofstream tiny((_dir / L"stub.exe").string(), std::ios::binary);
            tiny << "tiny";
        }
        {
            std::ofstream junk((_dir / L"node_modules" / L"x.exe").string(), std::ios::binary);
            const std::string blob(80 * 1024, 'b');
            junk.write(blob.data(), static_cast<std::streamsize>(blob.size()));
        }
        const auto found = GitArchive::FindInstallers(_dir);
        VERIFY_ARE_EQUAL(static_cast<size_t>(1), found.size());
        VERIFY_ARE_EQUAL(std::wstring{ L"app.zip" }, found[0].filename().wstring());
    }

    void GitArchiveTests::LogOnelineResetAndDeleteTag()
    {
        GitArchive g;
        g.gitExe = GitArchive::FindGit();
        if (g.gitExe.empty())
        {
            Log::Comment(L"git.exe not on PATH; skip");
            return;
        }
        g.cwd = _dir;
        {
            std::ofstream f((_dir / L"n.txt").string());
            f << "one\n";
        }
        VERIFY_IS_TRUE(g.Commit(L"first-note", true).ok());
        {
            std::ofstream f((_dir / L"n.txt").string());
            f << "two\n";
        }
        VERIFY_IS_TRUE(g.Commit(L"second-note", true).ok());
        const auto log = g.LogOneline(10);
        VERIFY_IS_TRUE(log.size() >= 2);
        VERIFY_IS_TRUE(log[0].message.find(L"second-note") != std::wstring::npos, log[0].display.c_str());
        VERIFY_IS_TRUE(log[1].message.find(L"first-note") != std::wstring::npos, log[1].display.c_str());
        auto back = g.ResetHardToCommit(log[1].hash);
        VERIFY_IS_TRUE(back.ok(), back.message.c_str());
        std::ifstream in((_dir / L"n.txt").string());
        std::string body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        VERIFY_IS_TRUE(body.find("one") != std::string::npos);
        VERIFY_IS_TRUE(body.find("two") == std::string::npos);
        VERIFY_IS_TRUE(g.Tag(L"to-delete").ok());
        VERIFY_IS_TRUE(g.TagExists(L"to-delete"));
        auto del = g.DeleteTag(L"to-delete", false);
        VERIFY_IS_TRUE(del.ok(), del.message.c_str());
        VERIFY_IS_FALSE(g.TagExists(L"to-delete"));
    }

    void GitArchiveTests::NormalizeWorkDirFileUri()
    {
        const auto native = GitArchive::NormalizeWorkDir(_dir.wstring());
        VERIFY_IS_FALSE(native.empty());
        VERIFY_IS_TRUE(std::filesystem::equivalent(native, _dir));

        std::wstring slash = _dir.wstring();
        std::replace(slash.begin(), slash.end(), L'\\', L'/');
        const auto fromUri = GitArchive::NormalizeWorkDir(L"file:///" + slash);
        VERIFY_IS_FALSE(fromUri.empty());
        VERIFY_IS_TRUE(std::filesystem::equivalent(fromUri, _dir));

        const auto fromUri2 = GitArchive::NormalizeWorkDir(L"file://" + slash);
        VERIFY_IS_FALSE(fromUri2.empty());
        VERIFY_IS_TRUE(std::filesystem::equivalent(fromUri2, _dir));

        const auto missing = GitArchive::NormalizeWorkDir(L"E:\\this-dir-should-not-exist-it-plugin");
        VERIFY_IS_TRUE(missing.empty());
    }
}
