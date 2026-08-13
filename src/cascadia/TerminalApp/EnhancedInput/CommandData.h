// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once
#include <array>
#include <span>
#include <string_view>

namespace winrt::TerminalApp::implementation
{
    struct CommandEntry
    {
        std::wstring_view cmd;
        std::wstring_view tag;
        std::wstring_view desc;
        bool danger;
    };

    struct CommandGroup
    {
        std::wstring_view title;
        std::span<const CommandEntry> entries;
    };

    // 23 built-in commands verbatim from docs/layout-options.html CMD_GROUPS.
    // danger=true => left-click inserts into composer (confirm gate);
    // danger=false => left-click sends directly to terminal.

    inline constexpr CommandEntry kGroupConversation[] = {
        { L"/clear",   L"清空对话", L"清空当前所有对话历史，从零开始新一轮对话，不影响项目记忆文件",                              true  },
        { L"/compact", L"压缩对话", L"将对话历史压缩摘要，在对话过长时节省 token，保留关键上下文继续工作",                         false },
        { L"/cost",    L"查看费用", L"显示本次会话消耗的 token 数量和对应的 API 费用统计",                                         false },
        { L"/status",  L"查看状态", L"显示当前会话的状态信息，包括使用的模型、上下文长度、工具配置等",                               false },
        { L"/resume",  L"恢复对话", L"打开当前目录对话历史列表中，相同的对话",                                                   false },
    };

    inline constexpr CommandEntry kGroupModel[] = {
        { L"/model",    L"切换模型",  L"切换当前使用的 AI 模型，可选 Opus（最强）、Sonnet（平衡）、Haiku（快速）等", false },
        { L"/fast",     L"快速模式",  L"开关快速模式，启用后使用 Opus 输出更快，适合追求速度的场景",                 false },
        { L"/vim",      L"Vim 模式",  L"开关 Vim 键位模式，启用后可以用 Vim 快捷键操作输入框，适合 Vim 用户",        false },
        { L"/settings", L"打开设置",  L"打开交互式设置面板，可修改 API Key、主题、默认模型、工具权限等配置",          false },
        { L"/help",     L"查看帮助",  L"显示所有可用的 slash 命令和键盘快捷键的完整帮助文档",                         false },
    };

    inline constexpr CommandEntry kGroupProject[] = {
        { L"/init",      L"初始化",  L"在当前目录生成 CLAUDE.md 文件，记录项目背景、技术栈和规范，让 Claude 了解你的项目",            false },
        { L"/code init", L"代码智能", L"开启 LSP 代码智能（kiro 专有），支持语义搜索、跳转定义、查找引用、重命名符号等",              false },
        { L"/review",    L"代码审查", L"对当前代码改动进行审查，提出 bug、安全漏洞、性能问题和代码风格的改进建议",                    false },
        { L"/doctor",    L"环境诊断", L"检查当前运行环境，验证 API Key、依赖版本、工具配置是否正常，排查常见问题",                     false },
    };

    inline constexpr CommandEntry kGroupMemory[] = {
        { L"/memory",         L"记忆管理",   L"查看和编辑 Claude 的长期记忆文件，这些记忆会在每次对话开始时自动加载",                     false },
        { L"/permissions",    L"权限管理",   L"管理 Claude 使用各类工具的权限，控制哪些操作（文件读写、命令执行等）被允许",               false },
        { L"/mcp",            L"MCP 服务器", L"管理 MCP（Model Context Protocol）服务器，连接数据库、浏览器等外部工具扩展能力",           false },
        { L"/approved-tools", L"已授权工具", L"查看和管理已批准使用的工具列表，可以精细控制 Claude 能调用哪些工具",                        false },
        { L"/terminal-setup", L"终端配置",   L"配置终端集成选项，优化 Claude 在当前终端的使用体验",                                       false },
        { L"/release-notes",  L"版本更新",   L"查看 Claude CLI 最新版本的更新内容、新功能和重要变更说明",                                  false },
        { L"/bug",            L"报告问题",   L"向 Anthropic 报告 Claude 的 bug、异常行为或功能问题，附带当前会话信息",                      false },
    };

    inline constexpr CommandEntry kGroupAccount[] = {
        { L"/login",  L"登录", L"登录 Anthropic 账号或重新配置 API Key，支持 OAuth 和 API Key 两种方式", false },
        { L"/logout", L"退出", L"退出当前登录状态，清除本地保存的认证信息",                               false },
    };

    // 6 CLI session-restore commands (from user XLS 2026-08-12).
    inline constexpr CommandEntry kGroupSessionRestore[] = {
        { L"claude --continue",        L"续接对话",   L"恢复当前目录中的最后一次会话记录",                                        false },
        { L"claude --resume",          L"会话列表",   L"打开会话选择列表",                                                        false },
        { L"claude --resume <name>",   L"指定会话",   L"直接恢复指定的会话",                                                      false },
        { L"claude --from-pr <number>",L"PR关联会话", L"打开会话选择列表，筛选提取与指定 PR 相关代码关联的历史会话",                false },
        { L"claude -c",                L"续接(简)",   L"打开当前目录最后一次对话记录",                                            false },
        { L"claude -r",                L"历史列表",   L"打开当前目录对话历史列表中，相同的对话记录",                               false },
    };

    inline constexpr CommandGroup kCommandGroups[] = {
        { L"对话管理",   kGroupConversation   },
        { L"会话恢复",   kGroupSessionRestore },
        { L"模型与设置", kGroupModel          },
        { L"项目",       kGroupProject        },
        { L"记忆与工具", kGroupMemory         },
        { L"账号",       kGroupAccount        },
    };
}
