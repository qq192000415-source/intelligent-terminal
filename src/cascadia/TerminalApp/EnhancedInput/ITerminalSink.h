// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once
#include <winrt/Windows.Foundation.h>

namespace winrt::TerminalApp::implementation
{
    // Single send exit (architecture §3). Commands / skills / composer all depend
    // only on this abstraction, never touching TermControl directly. Pure C++ so
    // the logic layer can be unit-tested with a mock sink.
    struct ITerminalSink
    {
        virtual ~ITerminalSink() = default;

        // Send text to the active terminal and execute it (appends a carriage return).
        virtual void Send(const winrt::hstring& text) = 0;

        // Type text into the active terminal's input line WITHOUT executing (no CR).
        // The user can then edit / add args and press Enter themselves. Used by skills.
        virtual void TypeToTerminal(const winrt::hstring& text) = 0;

        // Fill the panel's own composer box with text without executing.
        virtual void Insert(const winrt::hstring& text) = 0;
    };
}
