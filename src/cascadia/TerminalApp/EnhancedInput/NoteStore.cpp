// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "NoteStore.h"

#include <fstream>
#include <sstream>
#include <json/json.h>
#include <til/u8u16convert.h>

using namespace std::filesystem;

namespace winrt::TerminalApp::implementation
{
    static path _defaultClaudeDir()
    {
        wchar_t profile[MAX_PATH]{};
        if (GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH) > 0)
        {
            return path{ profile } / L".claude";
        }
        return path{ L".claude" };
    }

    NoteStore::NoteStore(path dir)
    {
        if (dir.empty())
        {
            dir = _defaultClaudeDir();
        }
        _file = dir / L"notes.json";
    }

    std::vector<Note> NoteStore::Load() const noexcept
    try
    {
        std::vector<Note> result;
        std::ifstream in{ _file, std::ios::binary };
        if (!in)
        {
            return result;
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        const auto raw = ss.str();
        if (raw.empty())
        {
            return result;
        }

        Json::Value root;
        Json::CharReaderBuilder rb;
        std::istringstream stream{ raw };
        std::string errs;
        if (!Json::parseFromStream(rb, stream, &root, &errs) || !root.isArray())
        {
            return result;
        }

        for (const auto& item : root)
        {
            if (!item.isObject())
            {
                continue;
            }
            Note n;
            n.title = til::u8u16(item.get("title", "").asString());
            n.body = til::u8u16(item.get("body", "").asString());
            if (n.body.empty())
            {
                continue;
            }
            if (item.isMember("updated"))
            {
                if (item["updated"].isInt64())
                {
                    n.updated = item["updated"].asInt64();
                }
                else if (item["updated"].isInt())
                {
                    n.updated = item["updated"].asInt();
                }
            }
            result.push_back(std::move(n));
        }
        return result;
    }
    catch (...)
    {
        return {};
    }

    bool NoteStore::Save(const std::vector<Note>& notes) const noexcept
    try
    {
        Json::Value root{ Json::arrayValue };
        for (const auto& n : notes)
        {
            if (n.body.empty())
            {
                continue;
            }
            Json::Value obj{ Json::objectValue };
            obj["title"] = til::u16u8(n.title);
            obj["body"] = til::u16u8(n.body);
            obj["updated"] = static_cast<Json::Int64>(n.updated);
            root.append(std::move(obj));
        }

        std::error_code ec;
        create_directories(_file.parent_path(), ec);
        if (ec)
        {
            return false;
        }

        Json::StreamWriterBuilder wb;
        const auto text = Json::writeString(wb, root);
        std::ofstream out{ _file, std::ios::binary | std::ios::trunc };
        if (!out)
        {
            return false;
        }
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.close();
        return static_cast<bool>(out);
    }
    catch (...)
    {
        return false;
    }
}
