// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "pch.h"
#include "GitArchive.h"

#include <fstream>
#include <sstream>
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
        if (blob.find(L"Could not resolve host") != std::wstring::npos ||
            blob.find(L"unable to access") != std::wstring::npos ||
            blob.find(L"Failed to connect") != std::wstring::npos)
        {
            return L"连不上 GitHub，检查网络。";
        }
        if (blob.find(L"Authentication failed") != std::wstring::npos ||
            blob.find(L"Permission denied") != std::wstring::npos ||
            blob.find(L"403") != std::wstring::npos)
        {
            return L"GitHub 登录无效或没权限。请重新登录。";
        }
        if (blob.find(L"rejected") != std::wstring::npos ||
            blob.find(L"failed to push") != std::wstring::npos ||
            blob.find(L"non-fast-forward") != std::wstring::npos)
        {
            return L"网上和电脑内容不一样。请先选保留电脑还是保留网上。";
        }
        if (blob.find(L"Please tell me who you are") != std::wstring::npos ||
            blob.find(L"user.email") != std::wstring::npos)
        {
            return L"还没设置保存用的名字。请先登录 GitHub。";
        }
        return L"没做成。请稍后再试。";
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
    cmd += L" -c core.quotepath=false -c i18n.logOutputEncoding=utf-8";
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
    si.hStdOutput = outW;
    si.hStdError = errW;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi{};
    std::wstring cwdW = cwd.wstring();
    std::wstring cmdMut = cmd;
    const BOOL ok = CreateProcessW(
        nullptr,
        cmdMut.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        cwdW.c_str(),
        &si,
        &pi);
    CloseHandle(outW);
    CloseHandle(errW);
    if (!ok)
    {
        CloseHandle(outR);
        CloseHandle(errR);
        r.message = L"没找到 Git。请先安装 Git for Windows。";
        r.exitCode = -2;
        return r;
    }

    auto readAll = [](HANDLE h) {
        std::string s;
        char buf[4096];
        DWORD n = 0;
        while (ReadFile(h, buf, sizeof(buf), &n, nullptr) && n > 0)
        {
            s.append(buf, n);
        }
        return Utf8ToWide(s);
    };
    r.out = readAll(outR);
    r.err = readAll(errR);
    WaitForSingleObject(pi.hProcess, 120000);
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

std::wstring GitArchive::SanitizeBranchName(const std::wstring& raw)
{
    std::wstring n;
    n.reserve(raw.size());
    for (const auto c : raw)
    {
        const bool ok = (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') ||
                        c == L'.' || c == L'_' || c == L'-' || c == L'/';
        n.push_back(ok ? c : L'-');
    }
    while (!n.empty() && (n.front() == L'.' || n.front() == L'/' || n.front() == L'-'))
    {
        n.erase(n.begin());
    }
    while (n.find(L"..") != std::wstring::npos)
    {
        n.replace(n.find(L".."), 2, L".");
    }
    while (n.find(L"//") != std::wstring::npos)
    {
        n.replace(n.find(L"//"), 2, L"/");
    }
    if (n.size() > 100)
    {
        n.resize(100);
    }
    return n;
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
    env.push_back(L'\0');
    env += L"GIT_TERMINAL_PROMPT=0";
    env.push_back(L'\0');
    env += L"GCM_INTERACTIVE=never";
    env.push_back(L'\0');
    env.push_back(L'\0');

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
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        CloseHandle(outR);
        return cred;
    }

    std::string s;
    char buf[2048];
    DWORD n = 0;
    while (ReadFile(outR, buf, sizeof(buf), &n, nullptr) && n > 0)
    {
        s.append(buf, n);
    }
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
    if (n.size() > 100)
    {
        n.resize(100);
    }
    if (n.empty())
    {
        n = L"archive";
    }
    return n;
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
