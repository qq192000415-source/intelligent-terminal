// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "pch.h"
#include "GitArchive.h"

#include <array>
#include <sstream>

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
        r.message = L"已保存到这台电脑。";
    }
    return r;
}

GitRun GitArchive::Push() const
{
    const auto remote = RemoteName();
    if (remote.empty())
    {
        GitRun r;
        r.message = L"还没有网上仓库。请先「存档并上传到网上」。";
        return r;
    }
    auto r = Run({ L"push", L"-u", remote, L"HEAD" });
    if (r.ok())
    {
        r.message = L"已上传到网上。";
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
        r.message = L"已从网上取回。";
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
