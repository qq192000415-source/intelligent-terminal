// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once
#include "CommandData.h"

namespace winrt::TerminalApp::implementation
{
    inline constexpr CommandEntry kGrokLaunch[] = {
        { L"grok", L"启动", L"进入代码目录后启动 Grok Build", false },
        { L"grok -r", L"继续上次", L"恢复当前目录最近一次会话", false },
        { L"grok -c", L"接着聊", L"继续当前目录最近一次对话（不重开）", false },
        { L"grok -p \"", L"无界面跑一次", L"不进全屏界面，终端里直接做完一件事", false, true },
        { L"grok -w", L"隔离改动", L"新建 git worktree，避免改乱主目录", false },
    };

    inline constexpr CommandEntry kGrokAccount[] = {
        { L"grok login", L"重新登录", L"浏览器弹窗登录 xAI 账号", false },
        { L"grok login --device-auth", L"设备码登录", L"终端出短码，用手机浏览器完成授权", false },
        { L"$env:XAI_API_KEY = \"xai-\"", L"API Key 登录", L"跳过浏览器，用 console.x.ai 的密钥登录。不预填真实密钥", false, true },
        { L"grok logout", L"退出登录", L"清掉本机登录状态", false },
        { L"grok models", L"列出模型", L"看本机当前能用的模型", false },
        { L"grok update", L"更新版本", L"更新 CLI", false },
        { L"grok --version", L"查看版本", L"显示当前安装的 Grok Build 版本", false },
    };

    inline constexpr CommandEntry kGrokModel[] = {
        { L"/help", L"查看命令", L"列出当前界面的命令和快捷键", false },
        { L"/home", L"回欢迎页", L"回到启动欢迎屏", false },
        { L"/model Grok 4.6", L"切换模型", L"换成 Grok 4.6（不要用 4.5）", false },
        { L"/effort low", L"强度低", L"改名字、解释一小段、格式化，快且省额度", false },
        { L"/effort medium", L"强度中", L"日常小改、普通问答", false },
        { L"/effort high", L"强度高", L"修 bug、写功能（默认）", false },
        { L"/effort xhigh", L"强度极高", L"大重构、很难的架构；仅 4.6 有效", false },
        { L"/m Grok 4.6 xhigh", L"模型+强度", L"一行同时指定模型和推理级别", false },
        { L"/plan", L"任务规划", L"先出方案，确认后再改代码", false },
        { L"/view-plan", L"查看计划", L"打开当前这轮的计划", false },
    };

    inline constexpr CommandEntry kGrokConversation[] = {
        { L"/new", L"开新对话", L"清空当前会话重新开始", true },
        { L"/resume", L"恢复会话", L"打开这个目录下的历史会话列表", false },
        { L"/sessions", L"管理会话", L"切换、重命名或关掉进行中的会话", false },
        { L"/context", L"看上下文", L"看当前对话占了多少上下文", false },
        { L"/compact", L"压缩上下文", L"对话太长、token 快满时压缩历史", false },
        { L"/rewind", L"回退一轮", L"改坏了回到上一轮", false },
        { L"/always-approve", L"少问确认", L"改文件、跑命令少弹确认（仅自己电脑）", true },
        { L"/settings", L"打开设置", L"主题、权限、模型等设置", false },
        { L"/export", L"导出对话", L"把当前会话导出成文件", false },
        { L"/usage", L"查看用量", L"看剩余额度 / 账单", false },
        { L"/copy", L"复制回复", L"复制上一条回复；/copy 2 复制倒数第二条", false },
        { L"/btw", L"旁问一句", L"插一句不影响当前任务的问题", false },
    };

    inline constexpr CommandEntry kGrokExt[] = {
        { L"/plugins", L"插件页", L"打开扩展面板的插件页", false },
        { L"grok plugin list", L"已装插件", L"列出已安装插件", false },
        { L"grok plugin install ", L"安装插件", L"安装一个插件", false, true },
        { L"grok plugin update", L"更新插件", L"更新已安装插件", false },
        { L"/marketplace", L"插件市场", L"打开扩展面板的市场页", false },
        { L"grok plugin marketplace list", L"市场源", L"列出市场源", false },
        { L"/skills", L"技能页", L"打开扩展面板的 Skills 页", false },
        { L"/hooks", L"Hooks", L"打开扩展面板的 Hooks 页", false },
        { L"/mcps", L"MCP 页", L"打开扩展面板的 MCP 页", false },
        { L"grok mcp list", L"MCP 列表", L"列出已配置的 MCP 服务器", false },
        { L"grok mcp add ", L"添加 MCP", L"添加一个 MCP 服务器", false, true },
        { L"grok mcp doctor", L"MCP 诊断", L"检查 MCP 连接是否正常", false },
    };

    inline constexpr CommandEntry kGrokWorkflow[] = {
        { L"/create-workflow", L"新建工作流", L"写一份新工作流并保存", false },
        { L"/workflow ", L"运行工作流", L"运行已保存的工作流；也可暂停 / 继续 / 停止", false, true },
        { L"/workflows", L"工作流面板", L"打开全屏工作流运行面板", false },
        { L"/imagine ", L"生图", L"按文字生成一张图", false, true },
        { L"/imagine-video ", L"生视频", L"按文字生成一段视频", false, true },
        { L"/deep-research ", L"深度研究", L"后台跑一轮深度调研", false, true },
        { L"/loop 5m ", L"定时循环", L"按间隔反复跑同一条提示（例：每 5 分钟）", false, true },
    };

    inline constexpr CommandEntry kGrokAgent[] = {
        { L"/tasks", L"后台任务", L"查看后台任务、子 agent、定时任务", false },
        { L"/dashboard", L"Agent 面板", L"打开 Agent Dashboard", false },
        { L"grok dashboard", L"外开面板", L"在终端外打开 Agent Dashboard", false },
        { L"/personas", L"人设", L"管理人设 / 角色", false },
        { L"/agents", L"Agent 定义", L"管理 agent 定义（别名 /config-agents）", false },
        { L"/memory", L"记忆", L"浏览和管理跨会话记忆（别名 /mem）", false },
        { L"/remember ", L"记一条", L"存一条长期记忆", false, true },
        { L"grok memory clear --workspace", L"清仓库记忆", L"清掉当前仓库的跨会话记忆", true },
    };

    inline constexpr CommandGroup kGrokCommandGroups[] = {
        { L"启动", kGrokLaunch },
        { L"账号与安装", kGrokAccount },
        { L"模型与计划", kGrokModel },
        { L"对话", kGrokConversation },
        { L"扩展", kGrokExt },
        { L"工作流与生成", kGrokWorkflow },
        { L"Agent 与记忆", kGrokAgent },
    };
}
