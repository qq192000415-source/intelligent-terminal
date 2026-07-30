// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once
#include "ITerminalSink.h"
#include <functional>

namespace winrt::TerminalApp::implementation
{
    // ITerminalSink implementation: delegates Send/Insert to two host-injected
    // callbacks. "How the text actually reaches the terminal" (raise a SendInput
    // action / fill the composer) is supplied by EnhancedInputContent at
    // construction, keeping this class free of any XAML / TermControl dependency.
    struct TerminalSink : ITerminalSink
    {
        TerminalSink(std::function<void(winrt::hstring)> send,
                     std::function<void(winrt::hstring)> typeToTerminal,
                     std::function<void(winrt::hstring)> insert);

        void Send(const winrt::hstring& text) override;
        void TypeToTerminal(const winrt::hstring& text) override;
        void Insert(const winrt::hstring& text) override;

    private:
        std::function<void(winrt::hstring)> _send;
        std::function<void(winrt::hstring)> _type;
        std::function<void(winrt::hstring)> _insert;
    };
}
