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
        bool fill{ false }; // true => TypeToTerminal（不回车）；false => Send
    };

    struct CommandGroup
    {
        std::wstring_view title;
        std::span<const CommandEntry> entries;
    };

    // Claude 内置命令表：7 组 39 条（2026-10 按 Claude Code v2.1.291 官方命令参考更新）。
    // danger=true 只渲染 ⚠，不拦截点击；fill=true => TypeToTerminal（填入不回车，用户接着输入参数）。

    inline constexpr CommandEntry kGroupConversation[] = {
        { L"/clear",   L"清空对话", L"开始一段全新对话，之前的聊天不再带入；项目记忆文件 CLAUDE.md 不受影响，旧对话可用 /resume 找回", true },
        { L"/compact", L"压缩对话", L"把前面的对话压缩成摘要，对话太长、快满时用，保留关键上下文继续干活",                       false },
        { L"/context", L"看上下文", L"用彩色格子显示上下文占了多少，帮你判断该 /compact 还是 /clear",                             false },
        { L"/rewind",  L"撤销回退", L"把代码和对话一起退回到之前某一步；Claude 改坏了或聊偏了就用它",                             false },
        { L"/btw ",    L"旁问一句", L"插一个小问题，回答不会混进当前对话。点击后接着输入问题再回车",                             false, true },
        { L"/copy",    L"复制回复", L"复制 Claude 上一条回复到剪贴板；回复里有代码块时可以只选其中一块",                         false },
        { L"/export",  L"导出对话", L"把当前整段对话导出成文本，可以复制或存成文件",                                             false },
        { L"/rename ", L"会话起名", L"给当前会话起个名字，以后按名字恢复。点击后输入名字再回车；直接回车会自动起名",             false, true },
    };

    inline constexpr CommandEntry kGroupSessionRestore[] = {
        { L"/resume",           L"恢复对话",   L"在 Claude 里打开历史会话列表，选一个接着聊",                                   false },
        { L"claude --continue", L"续接上次",   L"在普通命令行（还没进 Claude 时）用：直接接着当前目录最近一次对话",             false },
        { L"claude --resume",   L"会话列表",   L"在普通命令行（还没进 Claude 时）用：打开历史会话列表再选",                     false },
        { L"claude --resume ",  L"按名字恢复", L"在普通命令行用：点击后输入会话名（/rename 起的名字）再回车",                   false, true },
    };

    inline constexpr CommandEntry kGroupModel[] = {
        { L"/model",  L"切换模型", L"切换 AI 模型（如 Opus、Sonnet、Haiku）并保存为以后的默认；列表里左右方向键可同时调思考强度", false },
        { L"/effort", L"思考强度", L"调整思考强度：简单的事用 low，又快又省额度；难的事调高",                                     false },
        { L"/fast",   L"快速模式", L"点了会先问你，确认后才打开快速模式，更费额度",                                             false },
        { L"/config", L"打开设置", L"打开设置面板：主题、默认模型、编辑模式等（/settings 是同一个命令）",                         false },
        { L"/usage",  L"用量额度", L"看本次会话花费、套餐剩余额度和使用统计（旧命令 /cost 是它的别名）",                         false },
        { L"/status", L"查看状态", L"看版本、当前模型、账号和连接状态",                                                           false },
        { L"/voice",  L"语音输入", L"开关语音听写，需要 claude.ai 账号登录；目前听写语言不含中文",                               false },
    };

    inline constexpr CommandEntry kGroupProject[] = {
        { L"/init",            L"初始化",   L"在当前项目生成 CLAUDE.md，记录项目背景和规范，让 Claude 了解你的项目",                   false },
        { L"/plan ",           L"先出方案", L"进入计划模式：先出方案，你确认后才改代码。点击后输入任务再回车；直接回车只进入计划模式", false, true },
        { L"/diff",            L"查看改动", L"看改了哪些文件、哪些地方，包括 Claude 刚做的修改",                                       false },
        { L"/review",          L"代码审查", L"检查当前改动里的 bug（/code-review 的别名）",                                            false },
        { L"/simplify",        L"精简代码", L"清理刚改的代码：能复用的复用、能简化的简化；不查 bug",                                   false },
        { L"/security-review", L"安全检查", L"检查当前分支改动里的安全漏洞（需要 git 仓库并配置 origin 远程）",                        false },
        { L"/tasks",           L"后台任务", L"查看和管理当前会话在后台跑的任务",                                                       false },
        { L"/doctor",          L"环境诊断", L"体检安装和配置问题：先报告，你确认后才修",                                               false },
    };

    inline constexpr CommandEntry kGroupMemory[] = {
        { L"/memory",      L"记忆管理",   L"编辑 CLAUDE.md 记忆文件，开关自动记忆",                                  false },
        { L"/permissions", L"权限管理",   L"管理哪些操作直接允许、哪些要问、哪些禁止（/allowed-tools 是同一个命令）", false },
        { L"/mcp",         L"MCP 服务器", L"查看、连接、重连 MCP 服务器（如 Playwright 浏览器）",                   false },
        { L"/skills",      L"技能列表",   L"查看已安装的技能",                                                       false },
        { L"/plugin",      L"插件管理",   L"安装、卸载、启用、停用插件",                                             false },
    };

    inline constexpr CommandEntry kGroupHelp[] = {
        { L"/help",          L"查看帮助", L"显示可用命令和快捷键",                                       false },
        { L"/powerup",       L"互动教程", L"官方互动小教程，带动画演示，适合慢慢熟悉功能",               false },
        { L"/insights",      L"使用分析", L"生成最近使用情况的报告，推荐你没用过的功能（会消耗一些额度）", false },
        { L"/release-notes", L"版本更新", L"查看 Claude Code 各版本的更新内容",                           false },
        { L"/bug",           L"报告问题", L"向 Anthropic 报告问题；发送前会让你确认附带多少对话内容",     false },
    };

    inline constexpr CommandEntry kGroupAccount[] = {
        { L"/login",  L"登录", L"登录 Anthropic / claude.ai 账号", false },
        { L"/logout", L"退出", L"退出当前账号登录",                 false },
    };

    inline constexpr CommandGroup kCommandGroups[] = {
        { L"对话管理",   kGroupConversation   },
        { L"会话恢复",   kGroupSessionRestore },
        { L"模型与设置", kGroupModel          },
        { L"项目",       kGroupProject        },
        { L"记忆与工具", kGroupMemory         },
        { L"学习与帮助", kGroupHelp           },
        { L"账号",       kGroupAccount        },
    };
}
