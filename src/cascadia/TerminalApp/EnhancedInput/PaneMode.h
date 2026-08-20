// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once
#include "CommandData.h"
#include "GrokCommandData.h"
#include <filesystem>
#include <span>

namespace winrt::TerminalApp::implementation
{
    enum class InputPaneMode
    {
        Claude,
        Grok,
    };

    struct InputPaneRoots
    {
        std::filesystem::path storeDir;
        std::filesystem::path skillsDir;
        std::span<const CommandGroup> groups;
    };

    inline InputPaneRoots RootsFor(InputPaneMode mode, const std::filesystem::path& userProfile)
    {
        const auto dir = userProfile / (mode == InputPaneMode::Grok ? L".grok" : L".claude");
        if (mode == InputPaneMode::Grok)
        {
            return { dir, dir / L"skills", kGrokCommandGroups };
        }
        return { dir, dir / L"skills", kCommandGroups };
    }

    inline std::filesystem::path DefaultUserProfile()
    {
        wchar_t profile[MAX_PATH]{};
        if (GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH) > 0)
        {
            return std::filesystem::path{ profile };
        }
        return {};
    }
}
