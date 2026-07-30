// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "TerminalSink.h"

namespace winrt::TerminalApp::implementation
{
    TerminalSink::TerminalSink(std::function<void(winrt::hstring)> send,
                               std::function<void(winrt::hstring)> typeToTerminal,
                               std::function<void(winrt::hstring)> insert) :
        _send{ std::move(send) },
        _type{ std::move(typeToTerminal) },
        _insert{ std::move(insert) }
    {
    }

    void TerminalSink::Send(const winrt::hstring& text)
    {
        if (_send)
        {
            _send(text);
        }
    }

    void TerminalSink::TypeToTerminal(const winrt::hstring& text)
    {
        if (_type)
        {
            _type(text);
        }
    }

    void TerminalSink::Insert(const winrt::hstring& text)
    {
        if (_insert)
        {
            _insert(text);
        }
    }
}
