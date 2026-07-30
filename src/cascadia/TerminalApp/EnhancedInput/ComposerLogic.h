// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace winrt::TerminalApp::implementation
{
    // Pure send-composition rules for the Composer (architecture §3, logic layer).
    // Header-only inline functions (following CommandData.h) with no XAML / WinRT
    // dependency, so the payload + enable rules are unit-testable on their own.
    namespace ComposerLogic
    {
        // Trim ASCII/Unicode whitespace from both ends. Used to decide "is there
        // real text" without mutating the draft the user still sees.
        inline std::wstring_view Trim(std::wstring_view s) noexcept
        {
            constexpr std::wstring_view ws{ L" \t\r\n\f\v　" };
            const auto first = s.find_first_not_of(ws);
            if (first == std::wstring_view::npos)
            {
                return {};
            }
            const auto last = s.find_last_not_of(ws);
            return s.substr(first, last - first + 1);
        }

        // Send is enabled when there is non-whitespace text OR at least one
        // attachment (requirements §3: "空且无附件时禁用").
        inline bool IsSendEnabled(std::wstring_view text, size_t attachmentCount) noexcept
        {
            return !Trim(text).empty() || attachmentCount > 0;
        }

        // Compose one message = trimmed text followed by each attachment path,
        // space-separated. Paths containing spaces get double-quoted, matching
        // TermControl's own drag-drop quoting (_DragDropHandler). Attachments are
        // sent as plain-text local paths because the PTY can't carry binaries.
        inline std::wstring BuildSendPayload(std::wstring_view text, const std::vector<std::wstring>& attachments)
        {
            std::wstring payload{ Trim(text) };
            for (const auto& path : attachments)
            {
                if (!payload.empty())
                {
                    payload.push_back(L' ');
                }
                const bool needsQuotes = path.find(L' ') != std::wstring::npos;
                if (needsQuotes)
                {
                    payload.push_back(L'"');
                    payload.append(path);
                    payload.push_back(L'"');
                }
                else
                {
                    payload.append(path);
                }
            }
            return payload;
        }
    }
}
