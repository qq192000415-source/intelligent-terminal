// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "LocalStore.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <json/json.h>
#include <til/u8u16convert.h>

using namespace std::filesystem;

namespace winrt::TerminalApp::implementation
{
    static std::filesystem::path _defaultClaudeDir()
    {
        wchar_t profile[MAX_PATH]{};
        if (GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH) > 0)
        {
            return std::filesystem::path{ profile } / L".claude";
        }
        return std::filesystem::path{ L".claude" };
    }

    LocalStore::LocalStore(std::filesystem::path commandsDir, std::filesystem::path layoutDir)
    {
        if (commandsDir.empty())
        {
            commandsDir = _defaultClaudeDir();
        }
        if (layoutDir.empty())
        {
            layoutDir = commandsDir;
        }
        _file = commandsDir / L"custom_commands.json";
        _layoutFile = layoutDir / L"enhanced_input_layout.json";
    }

    // Read the whole file (UTF-8) and parse a JSON array of {cmd,tag,desc} objects.
    // Any failure — missing file, bad JSON, non-array root — yields an empty vector.
    // An entry with an empty "cmd" is skipped (cmd is the only required field).
    std::vector<CustomCommand> LocalStore::Load() const noexcept
    try
    {
        std::vector<CustomCommand> result;

        std::ifstream in{ _file, std::ios::binary };
        if (!in)
        {
            return result; // no file yet => no custom commands
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
            // jsoncpp hands back UTF-8; transcode each field to UTF-16 for the UI.
            CustomCommand entry;
            entry.cmd = til::u8u16(item.get("cmd", "").asString());
            if (entry.cmd.empty())
            {
                continue; // cmd is required — drop malformed rows rather than showing blanks
            }
            entry.tag = til::u8u16(item.get("tag", "").asString());
            entry.desc = til::u8u16(item.get("desc", "").asString());
            result.push_back(std::move(entry));
        }
        return result;
    }
    catch (...)
    {
        // Silent by contract — a corrupt file must not disrupt the panel / terminal.
        return {};
    }

    // Serialize the array and write it whole. Creates ~/.claude if needed. Returns
    // false on any IO / encoding failure (the caller keeps the in-memory list either
    // way — a failed persist just won't survive a restart, and never throws).
    bool LocalStore::Save(const std::vector<CustomCommand>& commands) const noexcept
    try
    {
        Json::Value root{ Json::arrayValue };
        for (const auto& c : commands)
        {
            Json::Value obj{ Json::objectValue };
            // Transcode UTF-16 UI strings back to UTF-8 for on-disk JSON.
            obj["cmd"] = til::u16u8(c.cmd);
            obj["tag"] = til::u16u8(c.tag);
            obj["desc"] = til::u16u8(c.desc);
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

    // Read the persisted panel width. Anything unusable — missing file, bad JSON,
    // non-finite or below the pane minimum — falls back to kDefaultWidth so the
    // caller never has to special-case first run. No upper clamp here: that
    // depends on the window size and is applied at open time.
    float LocalStore::LoadPanelWidth() const noexcept
    try
    {
        std::ifstream in{ _layoutFile, std::ios::binary };
        if (!in)
        {
            return kDefaultWidth;
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        const auto raw = ss.str();
        if (raw.empty())
        {
            return kDefaultWidth;
        }

        Json::Value root;
        Json::CharReaderBuilder rb;
        std::istringstream stream{ raw };
        std::string errs;
        if (!Json::parseFromStream(rb, stream, &root, &errs) || !root.isObject())
        {
            return kDefaultWidth;
        }

        const auto& node = root["width"];
        if (!node.isNumeric())
        {
            return kDefaultWidth;
        }

        const auto width = static_cast<float>(node.asDouble());
        if (!std::isfinite(width) || width < kMinWidth)
        {
            return kDefaultWidth;
        }
        return width;
    }
    catch (...)
    {
        // Silent by contract — a corrupt file must not disrupt the panel / terminal.
        return kDefaultWidth;
    }

    float LocalStore::SplitFraction(float savedPx, float totalPx) noexcept
    {
        if (!std::isfinite(savedPx) || !std::isfinite(totalPx) || totalPx <= kMinWidth * 2.0f)
        {
            return kDefaultWidth / 1200.0f;
        }
        const auto raw = savedPx / totalPx;
        if (!std::isfinite(raw))
        {
            return kDefaultWidth / 1200.0f;
        }
        return std::min(std::max(raw, 0.1f), kMaxWidthFraction);
    }

    // Persist the panel width. Rejects anything below kMinWidth: a pane that is
    // collapsed or hasn't been laid out yet reports ~0, and writing that would
    // make the next open start from the default instead of the user's size.
    bool LocalStore::SavePanelWidth(float width) const noexcept
    try
    {
        if (!std::isfinite(width) || width < kMinWidth)
        {
            return false;
        }

        std::error_code ec;
        create_directories(_layoutFile.parent_path(), ec);
        if (ec)
        {
            return false;
        }

        Json::Value root{ Json::objectValue };
        root["width"] = static_cast<double>(width);

        Json::StreamWriterBuilder wb;
        const auto text = Json::writeString(wb, root);

        std::ofstream out{ _layoutFile, std::ios::binary | std::ios::trunc };
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
