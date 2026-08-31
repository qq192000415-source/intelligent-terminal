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
        Plugins,
    };

    struct InputPaneRoots
    {
        std::filesystem::path storeDir;
        std::filesystem::path skillsDir;
        std::span<const CommandGroup> groups;
    };

    inline const wchar_t* TypeFor(InputPaneMode mode) noexcept
    {
        switch (mode)
        {
        case InputPaneMode::Grok:
            return L"grokInput";
        case InputPaneMode::Plugins:
            return L"pluginMarketplace";
        default:
            return L"enhancedInput";
        }
    }

    inline InputPaneRoots RootsFor(InputPaneMode mode, const std::filesystem::path& userProfile)
    {
        if (mode == InputPaneMode::Plugins)
        {
            return { userProfile / L".claude", userProfile / L".claude" / L"skills", {} };
        }
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
