// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "pch.h"
#include "GitArchive.h"

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <sstream>
#include <vector>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

using winrt::TerminalApp::implementation::GitArchive;
using winrt::TerminalApp::implementation::GitRun;

namespace
{
    std::wstring Utf8ToWide(const std::string& s)
    {
        if (s.empty())
        {
            return {};
        }
        const auto n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
        std::wstring w(n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
        return w;
    }

    std::wstring Quote(const std::wstring& s)
    {
        std::wstring o = L"\"";
        for (const auto c : s)
        {
            if (c == L'"')
            {
                o += L"\\\"";
            }
            else
            {
                o += c;
            }
        }
        o += L'"';
        return o;
    }

    std::wstring MapFail(int code, const std::wstring& err, const std::wstring& out)
    {
        const auto blob = err + L"\n" + out;
        if (code == -2)
        {
            return L"没找到 Git。请先安装 Git for Windows。";
        }
        if (blob.find(L"not a git repository") != std::wstring::npos)
        {
            return L"这个文件夹还不能存档。";
        }
        if (blob.find(L"nothing to commit") != std::wstring::npos)
        {
            return L"没有要保存的改动。";
        }
        if (blob.find(L"Authentication failed") != std::wstring::npos ||
            blob.find(L"Permission denied") != std::wstring::npos ||
            blob.find(L"denied to") != std::wstring::npos ||
            blob.find(L"403") != std::wstring::npos)
        {
            return L"当前登录的 GitHub 账号没有推这个仓库的权限。";
        }
        if (blob.find(L"Repository not found") != std::wstring::npos ||
            blob.find(L"repository not found") != std::wstring::npos ||
            blob.find(L"returned error: 404") != std::wstring::npos)
        {
            return L"网上仓库已经删了。可以重新建一个再推。";
        }
        if (blob.find(L"Could not resolve host") != std::wstring::npos ||
            blob.find(L"Failed to connect") != std::wstring::npos ||
            blob.find(L"unable to access") != std::wstring::npos)
        {
            return L"连不上 GitHub，检查网络。";
        }
        if (blob.find(L"rejected") != std::wstring::npos ||
            blob.find(L"failed to push") != std::wstring::npos ||
            blob.find(L"non-fast-forward") != std::wstring::npos)
        {
            return L"网上比这台电脑新。如果刚回滚过，不要点推送，也不要点「从 GitHub上取回」（会把回滚撤掉）。";
        }
        if (blob.find(L"already exists") != std::wstring::npos)
        {
            return L"已经有这个标签名。请换一个。";
        }
        if (blob.find(L"unknown revision") != std::wstring::npos ||
            blob.find(L"ambiguous argument") != std::wstring::npos ||
            blob.find(L"Needed a single revision") != std::wstring::npos)
        {
            return L"没有这个版本标签。";
        }
        if (blob.find(L"Please tell me who you are") != std::wstring::npos ||
            blob.find(L"user.email") != std::wstring::npos)
        {
            return L"还没设置保存用的名字。请先登录 GitHub。";
        }
        return L"没做成。请稍后再试。";
    }

    std::string DrainPipe(HANDLE h, DWORD budgetMs)
    {
        std::string s;
        if (!h)
        {
            return s;
        }
        const auto start = GetTickCount64();
        char buf[4096];
        while (GetTickCount64() - start < budgetMs)
        {
            DWORD avail = 0;
            if (!PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr))
            {
                break;
            }
            if (avail == 0)
            {
                Sleep(10);
                continue;
            }
            const DWORD want = (std::min)(avail, static_cast<DWORD>(sizeof(buf)));
            DWORD n = 0;
            if (!ReadFile(h, buf, want, &n, nullptr) || n == 0)
            {
                break;
            }
            s.append(buf, n);
        }
        return s;
    }

    std::wstring GitChildEnv()
    {
        std::wstring env;
        if (const auto block = GetEnvironmentStringsW())
        {
            size_t n = 0;
            while (!(block[n] == 0 && block[n + 1] == 0))
            {
                ++n;
            }
            env.assign(block, n);
            FreeEnvironmentStringsW(block);
        }
        auto add = [&](const wchar_t* kv) {
            env.push_back(L'\0');
            env += kv;
        };
        add(L"GIT_TERMINAL_PROMPT=0");
        add(L"GCM_INTERACTIVE=never");
        add(L"GIT_PAGER=");
        env.push_back(L'\0');
        env.push_back(L'\0');
        return env;
    }
}

std::filesystem::path GitArchive::FindGit()
{
    wchar_t buf[MAX_PATH]{};
    if (SearchPathW(nullptr, L"git.exe", nullptr, MAX_PATH, buf, nullptr) > 0)
    {
        return buf;
    }
    static const wchar_t* kGuess[] = {
        L"C:\\Program Files\\Git\\cmd\\git.exe",
        L"C:\\Program Files (x86)\\Git\\cmd\\git.exe",
    };
    for (const auto* p : kGuess)
    {
        if (std::filesystem::exists(p))
        {
            return p;
        }
    }
    return {};
}

std::filesystem::path GitArchive::NormalizeWorkDir(std::wstring wd)
{
    while (!wd.empty() && (wd.front() == L' ' || wd.front() == L'"'))
    {
        wd.erase(wd.begin());
    }
    while (!wd.empty() && (wd.back() == L' ' || wd.back() == L'"' || wd.back() == L'\0'))
    {
        wd.pop_back();
    }
    if (wd.empty())
    {
        return {};
    }

    if (wd.rfind(L"file:", 0) == 0)
    {
        wd.erase(0, 5);
        while (!wd.empty() && (wd.front() == L'/' || wd.front() == L'\\'))
        {
            wd.erase(wd.begin());
        }
        if (wd.rfind(L"localhost/", 0) == 0 || wd.rfind(L"localhost\\", 0) == 0)
        {
            wd.erase(0, 10);
        }
        // Percent-decode UTF-8 bytes (OSC 7 often encodes 中文).
        std::string utf8;
        for (size_t i = 0; i < wd.size(); ++i)
        {
            if (wd[i] == L'%' && i + 2 < wd.size())
            {
                const auto hex = [](wchar_t c) -> int {
                    if (c >= L'0' && c <= L'9')
                        return c - L'0';
                    if (c >= L'a' && c <= L'f')
                        return c - L'a' + 10;
                    if (c >= L'A' && c <= L'F')
                        return c - L'A' + 10;
                    return -1;
                };
                const int hi = hex(wd[i + 1]);
                const int lo = hex(wd[i + 2]);
                if (hi >= 0 && lo >= 0)
                {
                    utf8.push_back(static_cast<char>((hi << 4) | lo));
                    i += 2;
                    continue;
                }
            }
            if (wd[i] < 128)
            {
                utf8.push_back(static_cast<char>(wd[i]));
            }
            else
            {
                const wchar_t one = wd[i];
                WideCharToMultiByte(CP_UTF8, 0, &one, 1, nullptr, 0, nullptr, nullptr);
                char buf[8]{};
                const auto n = WideCharToMultiByte(CP_UTF8, 0, &one, 1, buf, 8, nullptr, nullptr);
                utf8.append(buf, n > 0 ? static_cast<size_t>(n) : 0);
            }
        }
        const auto n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
        std::wstring decoded(n > 0 ? static_cast<size_t>(n) : 0, L'\0');
        if (n > 0)
        {
            MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), decoded.data(), n);
        }
        wd = std::move(decoded);
    }

    std::replace(wd.begin(), wd.end(), L'/', L'\\');
    if (wd.size() >= 2 && wd[1] == L':' && (wd.size() == 2 || wd[2] != L'\\'))
    {
        // "E:foo" is drive-relative; make it "E:\foo"
        wd.insert(wd.begin() + 2, L'\\');
    }

    std::error_code ec;
    std::filesystem::path p{ wd };
    if (p.empty())
    {
        return {};
    }
    const auto canon = std::filesystem::weakly_canonical(p, ec);
    if (!ec && !canon.empty())
    {
        p = canon;
    }
    if (std::filesystem::is_directory(p, ec))
    {
        return p;
    }
    return {};
}

std::wstring GitArchive::AutoMessage(const std::filesystem::path& folder)
{
    SYSTEMTIME st{};
    GetLocalTime(&st);
    auto leaf = folder.empty() ? std::wstring{ L"项目" } : folder.filename().wstring();
    if (leaf.empty())
    {
        leaf = L"项目";
    }
    wchar_t msg[256]{};
    swprintf_s(msg, L"%u月%u日 存档（%s）", static_cast<unsigned>(st.wMonth), static_cast<unsigned>(st.wDay), leaf.c_str());
    return msg;
}

bool GitArchive::Installed() const
{
    return !gitExe.empty() && std::filesystem::exists(gitExe);
}

GitRun GitArchive::Run(const std::vector<std::wstring>& args) const
{
    GitRun r;
    if (!Installed())
    {
        r.exitCode = -2;
        r.message = MapFail(-2, {}, {});
        return r;
    }
    if (cwd.empty() || !std::filesystem::is_directory(cwd))
    {
        r.message = L"先在左边终端进到你的项目文件夹。";
        return r;
    }

    std::wstring cmd = Quote(gitExe.wstring());
    cmd += L" -c core.quotepath=false -c i18n.logOutputEncoding=utf-8 -c core.pager=";
    for (const auto& a : args)
    {
        cmd += L" ";
        cmd += Quote(a);
    }

    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE outR{}, outW{}, errR{}, errW{};
    if (!CreatePipe(&outR, &outW, &sa, 0) || !CreatePipe(&errR, &errW, &sa, 0))
    {
        r.message = L"没做成。请稍后再试。";
        return r;
    }
    SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(errR, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    si.hStdOutput = outW;
    si.hStdError = errW;
    si.hStdInput = (nul != INVALID_HANDLE_VALUE) ? nul : nullptr;

    PROCESS_INFORMATION pi{};
    std::wstring cwdW = cwd.wstring();
    std::wstring cmdMut = cmd;
    auto env = GitChildEnv();
    const BOOL ok = CreateProcessW(
        nullptr,
        cmdMut.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
        env.data(),
        cwdW.c_str(),
        &si,
        &pi);
    CloseHandle(outW);
    CloseHandle(errW);
    if (nul != INVALID_HANDLE_VALUE)
    {
        CloseHandle(nul);
    }
    if (!ok)
    {
        CloseHandle(outR);
        CloseHandle(errR);
        r.message = L"没找到 Git。请先安装 Git for Windows。";
        r.exitCode = -2;
        return r;
    }

    const auto wait = WaitForSingleObject(pi.hProcess, 15000);
    if (wait != WAIT_OBJECT_0)
    {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 500);
    }
    r.out = Utf8ToWide(DrainPipe(outR, 300));
    r.err = Utf8ToWide(DrainPipe(errR, 300));
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    r.exitCode = static_cast<int>(code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(outR);
    CloseHandle(errR);
    if (!r.ok())
    {
        r.message = MapFail(r.exitCode, r.err, r.out);
    }
    return r;
}

bool GitArchive::IsRepo() const
{
    const auto r = Run({ L"rev-parse", L"--is-inside-work-tree" });
    return r.ok() && r.out.find(L"true") != std::wstring::npos;
}

GitRun GitArchive::InitIfNeeded() const
{
    if (IsRepo())
    {
        GitRun r;
        r.exitCode = 0;
        return r;
    }
    auto r = Run({ L"init" });
    if (r.ok())
    {
        r.message = L"已在这个文件夹准备好存档。";
    }
    return r;
}

bool GitArchive::HasRemote() const
{
    return !RemoteName().empty();
}

std::wstring GitArchive::RemoteName() const
{
    const auto r = Run({ L"remote" });
    if (!r.ok())
    {
        return {};
    }
    std::wstringstream ss(r.out);
    std::wstring line;
    std::wstring first;
    while (std::getline(ss, line))
    {
        while (!line.empty() && (line.back() == L'\r' || line.back() == L'\n'))
        {
            line.pop_back();
        }
        if (line.empty())
        {
            continue;
        }
        if (first.empty())
        {
            first = line;
        }
        if (line == L"github" || line == L"origin")
        {
            return line;
        }
    }
    return first;
}

bool GitArchive::HasUncommitted() const
{
    const auto r = Run({ L"status", L"--porcelain" });
    if (!r.ok())
    {
        return false;
    }
    for (const auto c : r.out)
    {
        if (c != L' ' && c != L'\r' && c != L'\n' && c != L'\t')
        {
            return true;
        }
    }
    return false;
}

bool GitArchive::HasUnpushed() const
{
    if (!HasRemote())
    {
        return false;
    }
    const auto r = Run({ L"status", L"-sb" });
    if (!r.ok())
    {
        return false;
    }
    if (r.out.find(L"ahead") != std::wstring::npos)
    {
        return true;
    }
    // remote exists but no upstream yet → local commits still need a first push
    if (r.out.find(L"...") == std::wstring::npos)
    {
        const auto n = Run({ L"rev-list", L"--count", L"HEAD" });
        return n.ok() && n.out.find_first_of(L"123456789") != std::wstring::npos;
    }
    return false;
}

std::wstring GitArchive::CurrentBranch() const
{
    auto r = Run({ L"rev-parse", L"--abbrev-ref", L"HEAD" });
    if (!r.ok())
    {
        return {};
    }
    auto s = r.out;
    while (!s.empty() && (s.back() == L'\r' || s.back() == L'\n' || s.back() == L' '))
    {
        s.pop_back();
    }
    if (s == L"HEAD")
    {
        return {};
    }
    return s;
}

std::vector<std::wstring> GitArchive::LocalBranches() const
{
    std::vector<std::wstring> out;
    const auto r = Run({ L"branch", L"--format=%(refname:short)" });
    if (!r.ok())
    {
        return out;
    }
    std::wstringstream ss(r.out);
    std::wstring line;
    while (std::getline(ss, line))
    {
        while (!line.empty() && (line.back() == L'\r' || line.back() == L'\n'))
        {
            line.pop_back();
        }
        if (!line.empty())
        {
            out.push_back(line);
        }
    }
    return out;
}

GitRun GitArchive::Checkout(const std::wstring& branch) const
{
    auto r = Run({ L"checkout", branch });
    if (r.ok())
    {
        r.message = L"已切换到分支 " + branch + L"。";
    }
    else if (r.message.empty() || r.message == L"没做成。请稍后再试。")
    {
        r.message = L"切不过去。可能有未提交的改动，请先本地提交。";
    }
    return r;
}

GitRun GitArchive::CreateAndCheckoutBranch(const std::wstring& raw) const
{
    const auto name = SanitizeBranchName(raw);
    GitRun r;
    if (name.empty())
    {
        r.message = L"分支名无效。";
        return r;
    }
    r = Run({ L"checkout", L"-b", name });
    if (r.ok())
    {
        r.message = L"已创建并切换到 " + name + L"。";
    }
    return r;
}

namespace
{
    // git-check-ref-format: keep letters (including CJK), replace only truly illegal chars.
    std::wstring SanitizeGitRef(const std::wstring& raw)
    {
        std::wstring n;
        n.reserve(raw.size());
        for (const auto c : raw)
        {
            const bool forbidden = (c < 32) || c == 127 || iswspace(c) ||
                                   c == L'~' || c == L'^' || c == L':' || c == L'?' ||
                                   c == L'*' || c == L'[' || c == L'\\';
            n.push_back(forbidden ? L'-' : c);
        }
        while (true)
        {
            const auto at = n.find(L"@{");
            if (at == std::wstring::npos)
            {
                break;
            }
            n.replace(at, 2, L"-");
        }
        while (!n.empty() && (n.front() == L'.' || n.front() == L'/' || n.front() == L'-'))
        {
            n.erase(n.begin());
        }
        while (!n.empty() && (n.back() == L'.' || n.back() == L'/'))
        {
            n.pop_back();
        }
        while (n.find(L"..") != std::wstring::npos)
        {
            n.replace(n.find(L".."), 2, L".");
        }
        while (n.find(L"//") != std::wstring::npos)
        {
            n.replace(n.find(L"//"), 2, L"/");
        }
        if (n.size() >= 5 && n.compare(n.size() - 5, 5, L".lock") == 0)
        {
            n.resize(n.size() - 5);
        }
        if (n.size() > 100)
        {
            n.resize(100);
        }
        if (n == L"@")
        {
            n.clear();
        }
        return n;
    }
}

std::wstring GitArchive::SanitizeBranchName(const std::wstring& raw)
{
    return SanitizeGitRef(raw);
}

void GitArchive::DiffCounts(bool includeUnstaged, int& added, int& deleted) const
{
    added = 0;
    deleted = 0;
    auto parse = [&](const std::wstring& text) {
        std::wstringstream ss(text);
        std::wstring line;
        while (std::getline(ss, line))
        {
            if (line.empty() || line[0] == L'-')
            {
                continue;
            }
            const auto t1 = line.find(L'\t');
            if (t1 == std::wstring::npos)
            {
                continue;
            }
            const auto t2 = line.find(L'\t', t1 + 1);
            if (t2 == std::wstring::npos)
            {
                continue;
            }
            try
            {
                added += std::stoi(std::wstring(line.begin(), line.begin() + static_cast<std::ptrdiff_t>(t1)));
                deleted += std::stoi(std::wstring(line.begin() + static_cast<std::ptrdiff_t>(t1) + 1, line.begin() + static_cast<std::ptrdiff_t>(t2)));
            }
            catch (...)
            {
            }
        }
    };

    GitRun r;
    if (includeUnstaged)
    {
        r = Run({ L"diff", L"--numstat", L"HEAD" });
        if (!r.ok())
        {
            r = Run({ L"diff", L"--numstat" });
        }
    }
    else
    {
        r = Run({ L"diff", L"--numstat", L"--cached" });
    }
    parse(r.out);

    if (includeUnstaged)
    {
        const auto others = Run({ L"ls-files", L"--others", L"--exclude-standard" });
        std::wstringstream ss(others.out);
        std::wstring file;
        while (std::getline(ss, file))
        {
            while (!file.empty() && (file.back() == L'\r' || file.back() == L'\n'))
            {
                file.pop_back();
            }
            if (file.empty())
            {
                continue;
            }
            const auto path = cwd / file;
            std::error_code ec;
            if (!std::filesystem::is_regular_file(path, ec) || std::filesystem::file_size(path, ec) > 2 * 1024 * 1024)
            {
                continue;
            }
            std::ifstream in(path, std::ios::binary);
            if (!in)
            {
                continue;
            }
            char ch;
            bool binary = false;
            int lines = 0;
            bool nonempty = false;
            while (in.get(ch))
            {
                if (ch == '\0')
                {
                    binary = true;
                    break;
                }
                nonempty = true;
                if (ch == '\n')
                {
                    ++lines;
                }
            }
            if (!binary)
            {
                if (nonempty && (lines == 0 || ch != '\n'))
                {
                    ++lines; // last line without newline
                }
                added += lines;
            }
        }
    }
}

GitRun GitArchive::Commit(const std::wstring& message, bool addAll) const
{
    auto init = InitIfNeeded();
    if (!init.ok())
    {
        return init;
    }
    if (addAll)
    {
        auto add = Run({ L"add", L"-A" });
        if (!add.ok())
        {
            return add;
        }
    }
    auto msg = message;
    if (msg.empty())
    {
        msg = AutoMessage(cwd);
    }
    // Avoid "please tell me who you are" on a brand-new machine.
    auto r = Run({
        L"-c",
        L"user.name=云端存档",
        L"-c",
        L"user.email=archive@local",
        L"commit",
        L"-m",
        msg,
    });
    if (r.ok())
    {
        r.message = L"已完成本地提交。";
    }
    return r;
}

GitRun GitArchive::Push() const
{
    const auto remote = RemoteName();
    if (remote.empty())
    {
        GitRun r;
        r.message = L"还没有网上仓库。请先「本地提交并推送GitHub」。";
        return r;
    }
    auto r = Run({ L"push", L"-u", remote, L"HEAD" });
    if (r.ok())
    {
        r.message = L"已推送到 GitHub。";
    }
    return r;
}

GitRun GitArchive::Pull() const
{
    const auto remote = RemoteName();
    if (remote.empty())
    {
        GitRun r;
        r.message = L"还没有网上仓库。";
        return r;
    }
    auto r = Run({ L"pull", remote, L"HEAD" });
    if (r.ok())
    {
        r.message = L"已从 GitHub 取回。";
    }
    return r;
}

GitRun GitArchive::SetRemote(const std::wstring& url) const
{
    if (HasRemote())
    {
        return Run({ L"remote", L"set-url", RemoteName(), url });
    }
    auto r = Run({ L"remote", L"add", L"github", url });
    if (r.ok())
    {
        r.message = L"已连上网上仓库。";
    }
    return r;
}

bool GitArchive::HasHead() const
{
    return Run({ L"rev-parse", L"--verify", L"--quiet", L"HEAD" }).ok();
}

std::wstring GitArchive::SanitizeTagName(const std::wstring& raw)
{
    return SanitizeGitRef(raw);
}

bool GitArchive::TagExists(const std::wstring& name) const
{
    if (name.empty())
    {
        return false;
    }
    return Run({ L"rev-parse", L"--verify", L"--quiet", L"refs/tags/" + name }).ok();
}

std::vector<std::wstring> GitArchive::LocalTags() const
{
    std::vector<std::wstring> out;
    const auto r = Run({ L"tag", L"--list" });
    if (!r.ok())
    {
        return out;
    }
    std::wstringstream ss(r.out);
    std::wstring line;
    while (std::getline(ss, line))
    {
        while (!line.empty() && (line.back() == L'\r' || line.back() == L'\n'))
        {
            line.pop_back();
        }
        if (!line.empty())
        {
            out.push_back(line);
        }
    }
    return out;
}

GitRun GitArchive::FetchTags() const
{
    const auto remote = RemoteName();
    if (remote.empty())
    {
        GitRun r;
        r.exitCode = 0;
        r.message = L"还没有网上仓库。";
        return r;
    }
    return Run({ L"fetch", remote, L"--tags" });
}

GitRun GitArchive::Tag(const std::wstring& raw) const
{
    GitRun r;
    const auto name = SanitizeTagName(raw);
    if (name.empty())
    {
        r.message = L"标签名无效。";
        return r;
    }
    if (!IsRepo() || !HasHead())
    {
        r.message = L"还没有提交。请先点「本地提交」，再打标签。";
        return r;
    }
    if (HasUncommitted())
    {
        r.message = L"还有没提交的改动。请先点「本地提交」，再打标签。";
        return r;
    }
    if (TagExists(name))
    {
        r.message = L"已经有这个标签名。请换一个。";
        return r;
    }
    r = Run({ L"tag", L"--", name });
    if (r.ok())
    {
        r.message = L"已给当前这一版打上标签 " + name + L"。";
    }
    return r;
}

GitRun GitArchive::PushTag(const std::wstring& raw) const
{
    const auto name = SanitizeTagName(raw);
    GitRun r;
    if (name.empty())
    {
        r.message = L"标签名无效。";
        return r;
    }
    const auto remote = RemoteName();
    if (remote.empty())
    {
        r.exitCode = 0;
        r.message = L"标签只在这台电脑。还没有网上仓库。";
        return r;
    }
    r = Run({ L"push", remote, L"refs/tags/" + name });
    if (r.ok())
    {
        r.message = L"已打标签 " + name + L" 并传到 GitHub。";
    }
    return r;
}

GitRun GitArchive::ResetHardToTag(const std::wstring& raw) const
{
    const auto name = SanitizeTagName(raw);
    GitRun r;
    if (name.empty() || !TagExists(name))
    {
        r.message = L"没有这个版本标签。";
        return r;
    }
    r = Run({ L"reset", L"--hard", name });
    if (r.ok())
    {
        r.message = L"本机已回到 " + name +
                    L"。网上还是新的。不要点「从 GitHub上取回」，否则会把回滚撤掉。";
    }
    return r;
}

std::vector<GitArchive::LogLine> GitArchive::LogOneline(int maxN) const
{
    std::vector<LogLine> out;
    if (maxN < 1)
    {
        maxN = 50;
    }
    if (maxN > 200)
    {
        maxN = 200;
    }
    const auto r = Run({ L"log", L"--oneline", L"-n", std::to_wstring(maxN) });
    if (!r.ok())
    {
        return out;
    }
    std::wstringstream ss(r.out);
    std::wstring line;
    while (std::getline(ss, line))
    {
        while (!line.empty() && (line.back() == L'\r' || line.back() == L'\n'))
        {
            line.pop_back();
        }
        if (line.empty())
        {
            continue;
        }
        LogLine item;
        item.display = line;
        const auto sp = line.find(L' ');
        if (sp == std::wstring::npos)
        {
            item.hash = line;
        }
        else
        {
            item.hash = line.substr(0, sp);
            item.message = line.substr(sp + 1);
        }
        out.push_back(std::move(item));
    }
    return out;
}

GitRun GitArchive::ResetHardToCommit(const std::wstring& hash) const
{
    GitRun r;
    bool hex = !hash.empty() && hash.size() >= 7 && hash.size() <= 40;
    for (const auto c : hash)
    {
        const bool ok = (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f') || (c >= L'A' && c <= L'F');
        if (!ok)
        {
            hex = false;
            break;
        }
    }
    if (!hex)
    {
        r.message = L"存档编号无效。";
        return r;
    }
    auto verify = Run({ L"rev-parse", L"--verify", L"--quiet", hash });
    if (!verify.ok())
    {
        r.message = L"没有这一次存档。";
        return r;
    }
    r = Run({ L"reset", L"--hard", hash });
    if (r.ok())
    {
        r.message = L"本机已回到 " + hash +
                    L"。网上还是新的。不要点「从 GitHub上取回」，否则会把回滚撤掉。";
    }
    return r;
}

GitRun GitArchive::DeleteTag(const std::wstring& raw, bool alsoRemote) const
{
    GitRun r;
    const auto name = raw;
    if (name.empty() || !TagExists(name))
    {
        r.message = L"没有这个版本标签。";
        return r;
    }
    r = Run({ L"tag", L"-d", L"--", name });
    if (!r.ok())
    {
        return r;
    }
    if (!alsoRemote)
    {
        r.message = L"已从这台电脑去掉标签 " + name + L"。提交和文件都还在。";
        return r;
    }
    const auto remote = RemoteName();
    if (remote.empty())
    {
        r.exitCode = 0;
        r.message = L"已从这台电脑去掉标签 " + name + L"。没有网上仓库。";
        return r;
    }
    auto p = Run({ L"push", remote, L"--delete", L"refs/tags/" + name });
    if (p.ok())
    {
        r.exitCode = 0;
        r.message = L"已去掉标签 " + name + L"（本机和 GitHub）。提交和文件都还在。";
        return r;
    }
    r.exitCode = 0;
    r.message = L"本机已去掉标签 " + name + L"，但 GitHub 上没删掉。" +
                (p.message.empty() ? L"" : (L" " + p.message));
    return r;
}

namespace
{
    struct GithubCred
    {
        std::wstring user;
        std::wstring token;
    };

    GithubCred FillGithubCred()
    {
        GithubCred cred;
        const auto git = GitArchive::FindGit();
        if (git.empty())
        {
            return cred;
        }

    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE inR{}, inW{}, outR{}, outW{};
    if (!CreatePipe(&inR, &inW, &sa, 0) || !CreatePipe(&outR, &outW, &sa, 0))
    {
        return {};
    }
    SetHandleInformation(inW, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);

    auto env = GitChildEnv();

    std::wstring cmd = L"\"" + git.wstring() + L"\" credential fill";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = inR;
    si.hStdOutput = outW;
    si.hStdError = outW;
    PROCESS_INFORMATION pi{};
    const BOOL ok = CreateProcessW(
        nullptr,
        cmd.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
        env.data(),
        nullptr,
        &si,
        &pi);
    CloseHandle(inR);
    CloseHandle(outW);
    if (!ok)
    {
        CloseHandle(inW);
        CloseHandle(outR);
        return cred;
    }

    const char input[] = "protocol=https\nhost=github.com\n\n";
    DWORD written = 0;
    WriteFile(inW, input, sizeof(input) - 1, &written, nullptr);
    CloseHandle(inW);

    const auto wait = WaitForSingleObject(pi.hProcess, 3000);
    if (wait != WAIT_OBJECT_0)
    {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 400);
    }
    auto s = DrainPipe(outR, 400);
    CloseHandle(outR);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    auto take = [&](const char* key) {
        std::wstring w;
        const auto pos = s.find(key);
        if (pos == std::string::npos)
        {
            return w;
        }
        auto start = pos + strlen(key);
        auto end = s.find_first_of("\r\n", start);
        if (end == std::string::npos)
        {
            end = s.size();
        }
        const auto raw = s.substr(start, end - start);
        w.assign(raw.begin(), raw.end());
        return w;
    };
    cred.user = take("username=");
    cred.token = take("password=");
    SecureZeroMemory(s.data(), s.size());
    return cred;
    }
} // namespace

std::wstring GitArchive::ProbeGithubUser()
{
    auto cred = FillGithubCred();
    SecureZeroMemory(cred.token.data(), cred.token.size() * sizeof(wchar_t));
    return cred.user;
}

std::wstring GitArchive::SanitizeRepoName(const std::wstring& folderLeaf)
{
    std::wstring n;
    n.reserve(folderLeaf.size());
    for (const auto c : folderLeaf)
    {
        const bool ok = (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') || c == L'.' || c == L'_' || c == L'-';
        n.push_back(ok ? c : L'-');
    }
    while (!n.empty() && (n.front() == L'.' || n.front() == L'-'))
    {
        n.erase(n.begin());
    }
    while (n.find(L"--") != std::wstring::npos)
    {
        n.replace(n.find(L"--"), 2, L"-");
    }
    while (!n.empty() && (n.back() == L'-' || n.back() == L'.'))
    {
        n.pop_back();
    }
    if (n.size() > 100)
    {
        n.resize(100);
    }
    int letters = 0;
    for (const auto c : n)
    {
        if ((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z'))
        {
            ++letters;
        }
    }
    // 中文文件夹会变成 "1.0" 这种，GitHub 上不好认，换成可改的默认名。
    if (n.empty() || letters < 2)
    {
        n = L"archive";
    }
    return n;
}

bool GitArchive::IsRemoteGone(const GitRun& r)
{
    const auto blob = r.err + r.out + r.message;
    return blob.find(L"已经删了") != std::wstring::npos ||
           blob.find(L"Repository not found") != std::wstring::npos ||
           blob.find(L"repository not found") != std::wstring::npos ||
           blob.find(L"returned error: 404") != std::wstring::npos;
}

GitRun GitArchive::CreateGithubRepo(const std::wstring& name, bool isPrivate) const
{
    GitRun r;
    auto cred = FillGithubCred();
    if (cred.user.empty() || cred.token.empty())
    {
        SecureZeroMemory(cred.token.data(), cred.token.size() * sizeof(wchar_t));
        r.message = L"还没连上 GitHub。请先点「连接」。";
        return r;
    }

    const auto nameUtf8 = [&]() {
        const auto n = WideCharToMultiByte(CP_UTF8, 0, name.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string s(n > 0 ? static_cast<size_t>(n - 1) : 0, '\0');
        if (!s.empty())
        {
            WideCharToMultiByte(CP_UTF8, 0, name.c_str(), -1, s.data(), n, nullptr, nullptr);
        }
        return s;
    }();
    std::string body = std::string("{\"name\":\"") + nameUtf8 + "\",\"private\":" + (isPrivate ? "true" : "false") + "}";

    const auto session = WinHttpOpen(L"IntelligentTerminal/0.8", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session)
    {
        SecureZeroMemory(cred.token.data(), cred.token.size() * sizeof(wchar_t));
        r.message = L"连不上 GitHub，检查网络。";
        return r;
    }
    const auto connect = WinHttpConnect(session, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    const auto request = connect ? WinHttpOpenRequest(connect, L"POST", L"/user/repos", nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
    if (!request)
    {
        if (connect)
        {
            WinHttpCloseHandle(connect);
        }
        WinHttpCloseHandle(session);
        SecureZeroMemory(cred.token.data(), cred.token.size() * sizeof(wchar_t));
        r.message = L"连不上 GitHub，检查网络。";
        return r;
    }

    std::wstring auth = L"Authorization: Bearer " + cred.token + L"\r\nAccept: application/vnd.github+json\r\nUser-Agent: IntelligentTerminal\r\n";
    BOOL sent = WinHttpSendRequest(request, auth.c_str(), static_cast<DWORD>(-1), body.data(), static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0);
    SecureZeroMemory(cred.token.data(), cred.token.size() * sizeof(wchar_t));
    SecureZeroMemory(auth.data(), auth.size() * sizeof(wchar_t));
    if (!sent || !WinHttpReceiveResponse(request, nullptr))
    {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        r.message = L"连不上 GitHub，检查网络。";
        return r;
    }

    DWORD status = 0;
    DWORD slen = sizeof(status);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &slen, WINHTTP_NO_HEADER_INDEX);

    std::string resp;
    DWORD avail = 0;
    while (WinHttpQueryDataAvailable(request, &avail) && avail > 0)
    {
        std::string chunk(avail, '\0');
        DWORD got = 0;
        WinHttpReadData(request, chunk.data(), avail, &got);
        chunk.resize(got);
        resp += chunk;
    }
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);

    const auto urlFrom = [&]() {
        const auto key = std::string("\"clone_url\":\"");
        auto p = resp.find(key);
        if (p == std::string::npos)
        {
            return std::wstring{};
        }
        p += key.size();
        auto e = resp.find('"', p);
        if (e == std::string::npos)
        {
            return std::wstring{};
        }
        const auto u = resp.substr(p, e - p);
        return std::wstring(u.begin(), u.end());
    };

    if (status == 201)
    {
        r.exitCode = 0;
        r.out = urlFrom();
        r.message = isPrivate ? L"已创建私有仓库。" : L"已创建公开仓库。";
        return r;
    }
    if (status == 422)
    {
        r.exitCode = 0;
        r.out = L"https://github.com/" + cred.user + L"/" + name + L".git";
        r.message = L"仓库已存在，将连上它再推送。";
        return r;
    }
    if (status == 401 || status == 403)
    {
        r.message = L"GitHub 登录无效或没权限。请重新连接。";
        return r;
    }
    r.message = L"没建成 GitHub 仓库。请稍后再试。";
    return r;
}

namespace
{
    std::string WideToUtf8(const std::wstring& w)
    {
        if (w.empty())
        {
            return {};
        }
        const auto n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string s(n > 0 ? static_cast<size_t>(n - 1) : 0, '\0');
        if (!s.empty())
        {
            WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
        }
        return s;
    }

    std::string UrlEncode(const std::string& s)
    {
        std::string o;
        o.reserve(s.size() * 3);
        for (const unsigned char c : s)
        {
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
            {
                o += static_cast<char>(c);
            }
            else
            {
                char buf[4]{};
                sprintf_s(buf, "%%%02X", c);
                o += buf;
            }
        }
        return o;
    }

    struct GithubHttp
    {
        DWORD status{ 0 };
        std::string body;
        bool netOk{ false };
    };

    GithubHttp GithubRequest(const wchar_t* host,
                             const wchar_t* method,
                             const std::wstring& path,
                             const std::wstring& token,
                             const void* body,
                             DWORD bodyLen,
                             const wchar_t* extraHeaders)
    {
        GithubHttp r;
        const auto session = WinHttpOpen(L"IntelligentTerminal/0.8", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session)
        {
            return r;
        }
        const auto connect = WinHttpConnect(session, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
        const auto request = connect ? WinHttpOpenRequest(connect, method, path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
        if (!request)
        {
            if (connect)
            {
                WinHttpCloseHandle(connect);
            }
            WinHttpCloseHandle(session);
            return r;
        }
        std::wstring headers = L"Authorization: Bearer " + token + L"\r\nAccept: application/vnd.github+json\r\nUser-Agent: IntelligentTerminal\r\n";
        if (extraHeaders)
        {
            headers += extraHeaders;
        }
        BOOL sent = WinHttpSendRequest(request,
                                       headers.c_str(),
                                       static_cast<DWORD>(-1),
                                       body ? const_cast<void*>(body) : WINHTTP_NO_REQUEST_DATA,
                                       bodyLen,
                                       bodyLen,
                                       0);
        SecureZeroMemory(headers.data(), headers.size() * sizeof(wchar_t));
        if (!sent || !WinHttpReceiveResponse(request, nullptr))
        {
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connect);
            WinHttpCloseHandle(session);
            return r;
        }
        r.netOk = true;
        DWORD slen = sizeof(r.status);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &r.status, &slen, WINHTTP_NO_HEADER_INDEX);
        DWORD avail = 0;
        while (WinHttpQueryDataAvailable(request, &avail) && avail > 0)
        {
            std::string chunk(avail, '\0');
            DWORD got = 0;
            WinHttpReadData(request, chunk.data(), avail, &got);
            chunk.resize(got);
            r.body += chunk;
        }
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return r;
    }

    GithubHttp GithubUploadFile(const std::wstring& pathAndQuery, const std::wstring& token, HANDLE file, DWORD total)
    {
        GithubHttp r;
        const auto session = WinHttpOpen(L"IntelligentTerminal/0.8", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session)
        {
            return r;
        }
        const auto connect = WinHttpConnect(session, L"uploads.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
        const auto request = connect ? WinHttpOpenRequest(connect, L"POST", pathAndQuery.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
        if (!request)
        {
            if (connect)
            {
                WinHttpCloseHandle(connect);
            }
            WinHttpCloseHandle(session);
            return r;
        }
        std::wstring headers = L"Authorization: Bearer " + token +
                               L"\r\nAccept: application/vnd.github+json\r\nUser-Agent: IntelligentTerminal\r\nContent-Type: application/octet-stream\r\n";
        BOOL sent = WinHttpSendRequest(request, headers.c_str(), static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, total, 0);
        SecureZeroMemory(headers.data(), headers.size() * sizeof(wchar_t));
        if (!sent)
        {
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connect);
            WinHttpCloseHandle(session);
            return r;
        }
        std::vector<char> buf(64 * 1024);
        DWORD left = total;
        while (left > 0)
        {
            const DWORD chunk = (std::min)(left, static_cast<DWORD>(buf.size()));
            DWORD got = 0;
            if (!ReadFile(file, buf.data(), chunk, &got, nullptr) || got == 0)
            {
                WinHttpCloseHandle(request);
                WinHttpCloseHandle(connect);
                WinHttpCloseHandle(session);
                return r;
            }
            DWORD written = 0;
            if (!WinHttpWriteData(request, buf.data(), got, &written))
            {
                WinHttpCloseHandle(request);
                WinHttpCloseHandle(connect);
                WinHttpCloseHandle(session);
                return r;
            }
            left -= got;
        }
        if (!WinHttpReceiveResponse(request, nullptr))
        {
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connect);
            WinHttpCloseHandle(session);
            return r;
        }
        r.netOk = true;
        DWORD slen = sizeof(r.status);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &r.status, &slen, WINHTTP_NO_HEADER_INDEX);
        DWORD avail = 0;
        while (WinHttpQueryDataAvailable(request, &avail) && avail > 0)
        {
            std::string chunk(avail, '\0');
            DWORD got = 0;
            WinHttpReadData(request, chunk.data(), avail, &got);
            chunk.resize(got);
            r.body += chunk;
        }
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return r;
    }

    std::string JsonNumberAfter(const std::string& json, const std::string& key)
    {
        auto p = json.find(key);
        if (p == std::string::npos)
        {
            return {};
        }
        p += key.size();
        while (p < json.size() && (json[p] == ' ' || json[p] == '\t'))
        {
            ++p;
        }
        auto e = p;
        while (e < json.size() && json[e] >= '0' && json[e] <= '9')
        {
            ++e;
        }
        return json.substr(p, e - p);
    }
}

bool GitArchive::ParseGithubUrl(const std::wstring& url, std::wstring& owner, std::wstring& repo)
{
    owner.clear();
    repo.clear();
    auto s = url;
    while (!s.empty() && (s.back() == L'\r' || s.back() == L'\n' || s.back() == L'/' || s.back() == L' '))
    {
        s.pop_back();
    }
    auto p = s.find(L"github.com");
    if (p == std::wstring::npos)
    {
        return false;
    }
    p += 10;
    if (p < s.size() && (s[p] == L':' || s[p] == L'/'))
    {
        ++p;
    }
    auto rest = s.substr(p);
    if (rest.size() > 4 && rest.substr(rest.size() - 4) == L".git")
    {
        rest.resize(rest.size() - 4);
    }
    const auto slash = rest.find(L'/');
    if (slash == std::wstring::npos || slash == 0 || slash + 1 >= rest.size())
    {
        return false;
    }
    owner = rest.substr(0, slash);
    repo = rest.substr(slash + 1);
    const auto slash2 = repo.find(L'/');
    if (slash2 != std::wstring::npos)
    {
        repo.resize(slash2);
    }
    return !owner.empty() && !repo.empty();
}

std::wstring GitArchive::RemoteUrl() const
{
    const auto n = RemoteName();
    if (n.empty())
    {
        return {};
    }
    auto r = Run({ L"remote", L"get-url", n });
    if (!r.ok())
    {
        return {};
    }
    auto s = r.out;
    while (!s.empty() && (s.back() == L'\r' || s.back() == L'\n' || s.back() == L' '))
    {
        s.pop_back();
    }
    return s;
}

GitRun GitArchive::UploadReleaseAsset(const std::wstring& rawTag, const std::filesystem::path& file) const
{
    GitRun r;
    const auto tag = SanitizeTagName(rawTag);
    if (tag.empty() || !TagExists(tag))
    {
        r.message = L"请先选一个已经打好的版本标签。";
        return r;
    }
    if (file.empty() || !std::filesystem::exists(file))
    {
        r.message = L"找不到这个安装包。";
        return r;
    }
    std::error_code ec;
    const auto sz = std::filesystem::file_size(file, ec);
    if (ec || sz == 0)
    {
        r.message = L"安装包是空的，传不了。";
        return r;
    }
    if (sz > 2ull * 1024ull * 1024ull * 1024ull - 1ull)
    {
        r.message = L"安装包太大（GitHub 单文件不能超过 2GB）。";
        return r;
    }

    std::wstring owner;
    std::wstring repo;
    if (!ParseGithubUrl(RemoteUrl(), owner, repo))
    {
        r.message = L"还没有网上仓库。请先把源代码推到 GitHub。";
        return r;
    }

    auto cred = FillGithubCred();
    if (cred.user.empty() || cred.token.empty())
    {
        SecureZeroMemory(cred.token.data(), cred.token.size() * sizeof(wchar_t));
        r.message = L"还没连上 GitHub。请先点「连接」。";
        return r;
    }

    auto push = PushTag(tag);
    if (!push.ok() && push.message.find(L"already exists") == std::wstring::npos &&
        push.message.find(L"已经有") == std::wstring::npos)
    {
        // tag already on remote is fine; other push failures still try (release may exist)
    }

    const auto apiPath = L"/repos/" + owner + L"/" + repo;
    auto got = GithubRequest(L"api.github.com", L"GET", apiPath + L"/releases/tags/" + tag, cred.token, nullptr, 0, nullptr);
    if (!got.netOk)
    {
        SecureZeroMemory(cred.token.data(), cred.token.size() * sizeof(wchar_t));
        r.message = L"连不上 GitHub，检查网络。";
        return r;
    }
    if (got.status == 404)
    {
        const auto body = std::string("{\"tag_name\":\"") + WideToUtf8(tag) + "\",\"name\":\"" + WideToUtf8(tag) + "\"}";
        got = GithubRequest(L"api.github.com", L"POST", apiPath + L"/releases", cred.token, body.data(), static_cast<DWORD>(body.size()), L"Content-Type: application/json\r\n");
        if (!got.netOk)
        {
            SecureZeroMemory(cred.token.data(), cred.token.size() * sizeof(wchar_t));
            r.message = L"连不上 GitHub，检查网络。";
            return r;
        }
        if (got.status == 401 || got.status == 403)
        {
            SecureZeroMemory(cred.token.data(), cred.token.size() * sizeof(wchar_t));
            r.message = L"当前登录的 GitHub 账号没有上传这个仓库发行版的权限。";
            return r;
        }
        if (got.status != 201)
        {
            SecureZeroMemory(cred.token.data(), cred.token.size() * sizeof(wchar_t));
            r.message = L"没建成这一版的发行页。请先确认标签已传到 GitHub。";
            return r;
        }
    }
    else if (got.status == 401 || got.status == 403)
    {
        SecureZeroMemory(cred.token.data(), cred.token.size() * sizeof(wchar_t));
        r.message = L"当前登录的 GitHub 账号没有上传这个仓库发行版的权限。";
        return r;
    }
    else if (got.status != 200)
    {
        SecureZeroMemory(cred.token.data(), cred.token.size() * sizeof(wchar_t));
        r.message = L"没读到这一版的发行页。请稍后再试。";
        return r;
    }

    auto releaseId = JsonNumberAfter(got.body, "\"id\":");
    if (releaseId.empty())
    {
        SecureZeroMemory(cred.token.data(), cred.token.size() * sizeof(wchar_t));
        r.message = L"没读到这一版的发行页。请稍后再试。";
        return r;
    }

    const auto leaf = file.filename().wstring();
    const auto leafUtf8 = WideToUtf8(leaf);
    const auto nameKey = std::string("\"name\":\"") + leafUtf8 + "\"";
    const auto namePos = got.body.find(nameKey);
    if (namePos != std::string::npos)
    {
        const auto idKey = got.body.rfind("\"id\":", namePos);
        if (idKey != std::string::npos && namePos - idKey < 400)
        {
            auto assetId = JsonNumberAfter(got.body.substr(idKey), "\"id\":");
            if (!assetId.empty())
            {
                GithubRequest(L"api.github.com",
                              L"DELETE",
                              apiPath + L"/releases/assets/" + Utf8ToWide(assetId),
                              cred.token,
                              nullptr,
                              0,
                              nullptr);
            }
        }
    }

    const HANDLE h = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
    {
        SecureZeroMemory(cred.token.data(), cred.token.size() * sizeof(wchar_t));
        r.message = L"打不开这个安装包。";
        return r;
    }
    const auto upPath = L"/repos/" + owner + L"/" + repo + L"/releases/" + Utf8ToWide(releaseId) + L"/assets?name=" + Utf8ToWide(UrlEncode(leafUtf8));
    auto up = GithubUploadFile(upPath, cred.token, h, static_cast<DWORD>(sz));
    CloseHandle(h);
    SecureZeroMemory(cred.token.data(), cred.token.size() * sizeof(wchar_t));

    if (!up.netOk)
    {
        r.message = L"连不上 GitHub，检查网络。";
        return r;
    }
    if (up.status == 201)
    {
        r.exitCode = 0;
        r.message = L"已把安装包传到 GitHub（" + tag + L"）。源码仓库里没有这个文件。";
        r.out = L"https://github.com/" + owner + L"/" + repo + L"/releases/tag/" + tag;
        return r;
    }
    if (up.status == 401 || up.status == 403)
    {
        r.message = L"当前登录的 GitHub 账号没有上传这个仓库发行版的权限。";
        return r;
    }
    r.message = L"安装包没传上去。请稍后再试。";
    return r;
}

namespace
{
    bool SkipInstallerDir(const std::wstring& name)
    {
        std::wstring l = name;
        for (auto& c : l)
        {
            c = static_cast<wchar_t>(towlower(c));
        }
        return l == L"node_modules" || l == L".git" || l == L".grok" || l == L"venv" || l == L".venv" ||
               l == L"__pycache__" || l == L"obj" || l == L".vs" || l == L"vcpkg" || l == L"_scratch" ||
               l == L"_crash-dumps" || l == L"packages";
    }

    bool IsInstallerExt(const std::filesystem::path& p)
    {
        auto e = p.extension().wstring();
        for (auto& c : e)
        {
            c = static_cast<wchar_t>(towlower(c));
        }
        return e == L".exe" || e == L".msix" || e == L".msi" || e == L".zip" || e == L".appx" ||
               e == L".msixbundle" || e == L".appxbundle";
    }

    void ScanInstallers(const std::filesystem::path& dir, int depth, std::vector<std::filesystem::path>& out)
    {
        if (depth > 3)
        {
            return;
        }
        std::error_code ec;
        std::filesystem::directory_iterator it(dir, std::filesystem::directory_options::skip_permission_denied, ec);
        if (ec)
        {
            return;
        }
        const std::filesystem::directory_iterator end;
        for (; it != end; it.increment(ec))
        {
            if (ec)
            {
                break;
            }
            std::error_code st;
            if (it->is_directory(st))
            {
                if (!SkipInstallerDir(it->path().filename().wstring()))
                {
                    ScanInstallers(it->path(), depth + 1, out);
                }
            }
            else if (it->is_regular_file(st) && IsInstallerExt(it->path()))
            {
                const auto sz = std::filesystem::file_size(it->path(), st);
                if (!st && sz >= 64ull * 1024ull)
                {
                    out.push_back(it->path());
                }
            }
        }
    }
}

std::vector<std::filesystem::path> GitArchive::FindInstallers(const std::filesystem::path& root)
{
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    if (root.empty() || !std::filesystem::is_directory(root, ec))
    {
        return out;
    }
    ScanInstallers(root, 0, out);
    std::sort(out.begin(), out.end(), [](const std::filesystem::path& a, const std::filesystem::path& b) {
        std::error_code e1;
        std::error_code e2;
        const auto ta = std::filesystem::last_write_time(a, e1);
        const auto tb = std::filesystem::last_write_time(b, e2);
        if (e1 || e2)
        {
            return a.wstring() > b.wstring();
        }
        return ta > tb;
    });
    if (out.size() > 20)
    {
        out.resize(20);
    }
    return out;
}

bool GitArchive::StartGithubLogin()
{
    const auto git = FindGit();
    if (git.empty())
    {
        return false;
    }
    std::wstring cmd = L"\"" + git.wstring() + L"\" credential-manager github login --browser --force";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    const BOOL ok = CreateProcessW(
        nullptr,
        cmd.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &si,
        &pi);
    if (ok)
    {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    return ok;
}
