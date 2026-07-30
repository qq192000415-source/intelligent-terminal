// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "EnhancedInputContent.h"
#include "EnhancedInputContent.g.cpp"

#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.UI.Xaml.Media.Imaging.h>

#include <fstream>

#include "fzf/fzf.h"

using namespace winrt::Windows::Foundation;
using namespace winrt::Microsoft::Terminal;
using namespace winrt::Microsoft::Terminal::Settings::Model;
using namespace winrt::Windows::ApplicationModel::DataTransfer;
using namespace winrt::Windows::Storage;
using namespace winrt::Windows::Storage::Streams;
using namespace winrt::Windows::Graphics::Imaging;

namespace winrt
{
    namespace WUX = Windows::UI::Xaml;
    namespace WUXI = Windows::UI::Xaml::Input;
    namespace WUXC = Windows::UI::Xaml::Controls;
}

namespace winrt::TerminalApp::implementation
{
    // Upper bound on the raw clipboard bitmap we're willing to decode+transcode.
    // The clipboard DIB is uncompressed and much larger than the PNG it becomes,
    // so this is generous (an 8K frame is ~132 MB raw); the real attachment cap
    // is enforced by AttachmentStore::SaveImageBytes on the encoded PNG.
    static constexpr uint64_t kMaxDecodeInputBytes = 160ull * 1024ull * 1024ull;

    EnhancedInputContent::EnhancedInputContent()
    {
        InitializeComponent();
        // AcceptsReturn is set to False in the XAML source, but resources.pri (built
        // by the full package build) may carry an older XBF that still has True.
        // Override it here so the C++ increment is sufficient — no full rebuild needed.
        Composer().AcceptsReturn(false);
        // PlaceholderText in resources.pri may carry the old "Enter 换行" hint; override it.
        Composer().PlaceholderText(L"输入内容，Enter 发送，Shift+Enter 换行…");

        _sink = std::make_shared<TerminalSink>(
            [this](winrt::hstring text) {
                _dispatchSendAsync(std::move(text));
            },
            [this](winrt::hstring text) {
                // Type into the active terminal's input line without executing (no CR):
                // one SendInput of just the text, then focus the terminal so the user
                // can add args / press Enter themselves. Used by skill cards.
                auto control{ _control.get() };
                if (control)
                {
                    ActionAndArgs typeAction{ ShortcutAction::SendInput, SendInputArgs{ text } };
                    DispatchActionRequested.raise(control, typeAction);
                    control.Focus(WUX::FocusState::Programmatic);
                }
            },
            [this](winrt::hstring text) {
                Composer().Text(text);
                Composer().Focus(WUX::FocusState::Programmatic);
            });

        _buildCommandCards();
        // Load persisted custom commands (Phase 6) and render the 自定义 group.
        // Failure is silent (empty list) — never blocks the panel.
        _customCommands = _localStore.Load();
        _buildCustomCards();
        // Scan skills eagerly so the tab count badge is populated on first open
        // (18 small files — negligible cost). Refresh / filter re-render from this.
        _ensureSkillsScanned();
        _updateTargetPill();

        // Prune old screenshots on startup (architecture §6 double-threshold).
        _attachmentStore.CleanupShots();
        _updateSendState();
    }

    // Build command group titles + 2-column card grids, appended to CommandGroupsPanel.
    void EnhancedInputContent::_buildCommandCards()
    {
        auto panel{ CommandGroupsPanel() };

        int total = 0;
        for (const auto& group : kCommandGroups)
        {
            total += static_cast<int>(group.entries.size());

            // Group title
            WUX::Controls::TextBlock title{};
            title.Text(winrt::hstring{ group.title });
            title.FontSize(10.5);
            title.FontWeight({ 700 }); // Bold
            title.Opacity(0.5);
            title.Margin({ 0, 0, 0, 4 });

            // 2-column grid for cards
            WUX::Controls::Grid grid{};
            WUX::Controls::ColumnDefinition col0{};
            WUX::Controls::ColumnDefinition col1{};
            col0.Width(WUX::GridLengthHelper::FromValueAndType(1, WUX::GridUnitType::Star));
            col1.Width(WUX::GridLengthHelper::FromValueAndType(1, WUX::GridUnitType::Star));
            grid.ColumnDefinitions().Append(col0);
            grid.ColumnDefinitions().Append(col1);
            grid.ColumnSpacing(6);
            grid.RowSpacing(6);

            int row = 0;
            int col = 0;
            for (const auto& entry : group.entries)
            {
                if (col == 0)
                {
                    WUX::Controls::RowDefinition rowDef{};
                    rowDef.Height(WUX::GridLengthHelper::Auto());
                    grid.RowDefinitions().Append(rowDef);
                }

                // Card content: cmd name + short tag
                WUX::Controls::StackPanel cardContent{};
                cardContent.Spacing(1);

                WUX::Controls::TextBlock cmdText{};
                cmdText.Text(winrt::hstring{ entry.cmd });
                cmdText.FontFamily(WUX::Media::FontFamily{ L"Cascadia Code, Consolas, Courier New" });
                cmdText.FontSize(12);
                // Wrap long commands (/approved-tools, /terminal-setup) instead of
                // clipping them when the panel is narrow — mirrors the skill cards.
                cmdText.TextWrapping(WUX::TextWrapping::Wrap);

                WUX::Controls::TextBlock tagText{};
                tagText.Text(winrt::hstring{ entry.tag });
                tagText.FontSize(10.5);
                tagText.Opacity(0.55);

                // Danger marker (shield dot) on command text
                if (entry.danger)
                {
                    WUX::Controls::StackPanel row2{};
                    row2.Orientation(WUX::Controls::Orientation::Horizontal);
                    row2.Spacing(4);

                    WUX::Controls::TextBlock shield{};
                    shield.Text(L"⚠");
                    shield.FontSize(11);
                    shield.Foreground(WUX::Media::SolidColorBrush{ { 0xFF, 0xFF, 0xB0, 0x2E } });
                    shield.VerticalAlignment(WUX::VerticalAlignment::Center);

                    row2.Children().Append(shield);
                    row2.Children().Append(cmdText);
                    cardContent.Children().Append(row2);
                }
                else
                {
                    cardContent.Children().Append(cmdText);
                }
                cardContent.Children().Append(tagText);

                WUX::Controls::Button btn{};
                btn.Content(cardContent);
                btn.HorizontalAlignment(WUX::HorizontalAlignment::Stretch);
                btn.HorizontalContentAlignment(WUX::HorizontalAlignment::Left);
                btn.Padding({ 10, 6, 10, 6 });

                // Store a pointer to the static entry in the Tag so event handlers can retrieve it.
                // The pointer is stable (static data), so casting via uintptr_t through IInspectable is safe.
                btn.Tag(winrt::box_value(reinterpret_cast<uint64_t>(&entry)));

                btn.Click({ this, &EnhancedInputContent::_onCmdCardClick });
                btn.RightTapped({ this, &EnhancedInputContent::_onCmdCardRightTapped });
                btn.PointerEntered({ this, &EnhancedInputContent::_onCmdCardPointerEntered });
                btn.PointerExited({ this, &EnhancedInputContent::_onCmdCardPointerExited });

                WUX::Controls::Grid::SetRow(btn, row);
                WUX::Controls::Grid::SetColumn(btn, col);
                grid.Children().Append(btn);

                col++;
                if (col >= 2) { col = 0; row++; }
            }

            // Wrap group title + grid in a vertical StackPanel
            WUX::Controls::StackPanel groupPanel{};
            groupPanel.Spacing(0);
            groupPanel.Children().Append(title);
            groupPanel.Children().Append(grid);
            panel.Children().Append(groupPanel);
        }

        // Keep the tab-strip badge in sync with the actual command count.
        CmdCountRun().Text(L"  " + winrt::to_hstring(total));
    }

    // Retrieve CommandEntry* from a Button's Tag (stored as boxed uint64_t pointer).
    const CommandEntry* EnhancedInputContent::_entryFromTag(const IInspectable& tag)
    {
        if (!tag) return nullptr;
        const auto boxed{ tag.try_as<winrt::Windows::Foundation::IPropertyValue>() };
        if (!boxed) return nullptr;
        const auto addr{ boxed.GetUInt64() };
        return reinterpret_cast<const CommandEntry*>(addr);
    }

    // Left click: always Send the command directly. The danger marker (⚠) is now
    // visual-only — it warns the user but no longer gates the click into Insert.
    void EnhancedInputContent::_onCmdCardClick(const IInspectable& sender, const WUX::RoutedEventArgs&)
    {
        const auto btn{ sender.try_as<WUX::Controls::Button>() };
        if (!btn) return;
        if (const auto* entry{ _entryFromTag(btn.Tag()) })
        {
            _sink->Send(winrt::hstring{ entry->cmd });
        }
    }

    // Right click: copy command text to clipboard, then flash a confirmation in the description bar.
    void EnhancedInputContent::_onCmdCardRightTapped(const IInspectable& sender, const WUXI::RightTappedRoutedEventArgs&)
    {
        const auto btn{ sender.try_as<WUX::Controls::Button>() };
        if (!btn) return;
        const auto* entry{ _entryFromTag(btn.Tag()) };
        if (!entry) return;

        DataPackage pkg{};
        pkg.SetText(winrt::hstring{ entry->cmd });
        Clipboard::SetContent(pkg);

        // Reuse the hover bar for the confirmation so the feedback lands where the eye already is.
        HoverDescCmd().Text(L"✓");
        HoverDescText().Text(L"已复制到剪贴板");
        HoverDescBar().Opacity(1.0);

        // Lazily create the one-shot revert timer on first use.
        if (!_copyFeedbackTimer)
        {
            _copyFeedbackTimer = WUX::DispatcherTimer{};
            _copyFeedbackTimer.Interval(std::chrono::milliseconds(1200));
            _copyFeedbackTimer.Tick({ this, &EnhancedInputContent::_onCopyFeedbackTick });
        }
        // Stop+Start guarantees a fresh 1.2s countdown even on rapid successive copies.
        _copyFeedbackTimer.Stop();
        _copyFeedbackTimer.Start();
    }

    // Hover: show description bar.
    void EnhancedInputContent::_onCmdCardPointerEntered(const IInspectable& sender, const WUXI::PointerRoutedEventArgs&)
    {
        const auto btn{ sender.try_as<WUX::Controls::Button>() };
        if (!btn) return;
        if (const auto* entry{ _entryFromTag(btn.Tag()) })
        {
            // Moving onto a card cancels any pending copy-confirmation revert so it can't
            // later clobber this card's description.
            if (_copyFeedbackTimer)
            {
                _copyFeedbackTimer.Stop();
            }
            _hoveredEntry = entry;
            HoverDescCmd().Text(winrt::hstring{ entry->cmd });
            HoverDescText().Text(winrt::hstring{ entry->desc });
            HoverDescBar().Opacity(1.0);
        }
    }

    // Hover exit: hide description bar.
    void EnhancedInputContent::_onCmdCardPointerExited(const IInspectable& sender, const WUXI::PointerRoutedEventArgs&)
    {
        sender; // unused
        _hoveredEntry = nullptr;
        if (_copyFeedbackTimer)
        {
            _copyFeedbackTimer.Stop();
        }
        HoverDescBar().Opacity(0.0);
    }

    // One-shot: the "已复制" confirmation has had its moment; restore the hovered card's
    // description if the pointer is still on a card, otherwise hide the bar.
    void EnhancedInputContent::_onCopyFeedbackTick(const IInspectable&, const IInspectable&)
    {
        if (_copyFeedbackTimer)
        {
            _copyFeedbackTimer.Stop();
        }
        // At most one of these is set (a card exit clears its own; only one tab's
        // cards are hoverable at a time). Restore whichever the pointer sits on.
        if (_hoveredSkill)
        {
            HoverDescCmd().Text(winrt::hstring{ L"/" + _hoveredSkill->id });
            HoverDescText().Text(winrt::hstring{ _hoveredSkill->description });
            HoverDescBar().Opacity(1.0);
        }
        else if (_hoveredEntry)
        {
            HoverDescCmd().Text(winrt::hstring{ _hoveredEntry->cmd });
            HoverDescText().Text(winrt::hstring{ _hoveredEntry->desc });
            HoverDescBar().Opacity(1.0);
        }
        else if (_hoveredCustom)
        {
            HoverDescCmd().Text(winrt::hstring{ _hoveredCustom->cmd });
            HoverDescText().Text(winrt::hstring{ _hoveredCustom->desc });
            HoverDescBar().Opacity(1.0);
        }
        else
        {
            HoverDescBar().Opacity(0.0);
        }
    }

    // --- Custom commands (Phase 6) ---

    // Render the 自定义 group: one row per saved command (command card + ✕ delete)
    // followed by a "+ 添加" card. Single-column rows (not the built-in 2-col wall)
    // keep the delete affordance unambiguous and visually mark these as user data.
    void EnhancedInputContent::_buildCustomCards()
    {
        auto panel{ CustomGroupPanel() };
        panel.Children().Clear();
        // A stale hover pointer must not survive the rebuild it's being rebuilt for.
        _hoveredCustom = nullptr;

        // Group title (matches _buildCommandCards styling).
        WUX::Controls::TextBlock title{};
        title.Text(L"自定义");
        title.FontSize(10.5);
        title.FontWeight({ 700 });
        title.Opacity(0.5);
        title.Margin({ 0, 0, 0, 4 });
        panel.Children().Append(title);

        WUX::Controls::StackPanel rows{};
        rows.Spacing(6);

        for (size_t i = 0; i < _customCommands.size(); ++i)
        {
            const auto& c = _customCommands[i];

            // Command card (left, stretch): cmd + optional tag.
            WUX::Controls::StackPanel cardContent{};
            cardContent.Spacing(1);
            WUX::Controls::TextBlock cmdText{};
            cmdText.Text(winrt::hstring{ c.cmd });
            cmdText.FontFamily(WUX::Media::FontFamily{ L"Cascadia Code, Consolas, Courier New" });
            cmdText.FontSize(12);
            cmdText.TextWrapping(WUX::TextWrapping::Wrap);
            cardContent.Children().Append(cmdText);
            if (!c.tag.empty())
            {
                WUX::Controls::TextBlock tagText{};
                tagText.Text(winrt::hstring{ c.tag });
                tagText.FontSize(10.5);
                tagText.Opacity(0.55);
                cardContent.Children().Append(tagText);
            }

            WUX::Controls::Button card{};
            card.Content(cardContent);
            card.HorizontalAlignment(WUX::HorizontalAlignment::Stretch);
            card.HorizontalContentAlignment(WUX::HorizontalAlignment::Left);
            card.Padding({ 10, 6, 10, 6 });
            card.Tag(winrt::box_value(static_cast<uint64_t>(i)));
            card.Click({ this, &EnhancedInputContent::_onCustomCardClick });
            card.RightTapped({ this, &EnhancedInputContent::_onCustomCardRightTapped });
            card.PointerEntered({ this, &EnhancedInputContent::_onCustomCardPointerEntered });
            card.PointerExited({ this, &EnhancedInputContent::_onCustomCardPointerExited });

            // Delete ✕ (right).
            WUX::Controls::Button del{};
            del.Content(winrt::box_value(winrt::hstring{ L"✕" }));
            del.FontSize(11);
            del.Padding({ 8, 6, 8, 6 });
            del.VerticalAlignment(WUX::VerticalAlignment::Stretch);
            del.Tag(winrt::box_value(static_cast<uint64_t>(i)));
            del.Click({ this, &EnhancedInputContent::_onDeleteCustomClick });
            WUXC::ToolTipService::SetToolTip(del, winrt::box_value(winrt::hstring{ L"删除" }));

            WUX::Controls::Grid rowGrid{};
            WUX::Controls::ColumnDefinition cc0{};
            WUX::Controls::ColumnDefinition cc1{};
            cc0.Width(WUX::GridLengthHelper::FromValueAndType(1, WUX::GridUnitType::Star));
            cc1.Width(WUX::GridLengthHelper::Auto());
            rowGrid.ColumnDefinitions().Append(cc0);
            rowGrid.ColumnDefinitions().Append(cc1);
            rowGrid.ColumnSpacing(6);
            WUX::Controls::Grid::SetColumn(card, 0);
            WUX::Controls::Grid::SetColumn(del, 1);
            rowGrid.Children().Append(card);
            rowGrid.Children().Append(del);
            rows.Children().Append(rowGrid);
        }

        // "+ 添加" card — toggles the inline form.
        WUX::Controls::Button addBtn{};
        addBtn.Content(winrt::box_value(winrt::hstring{ L"＋ 添加自定义命令" }));
        addBtn.HorizontalAlignment(WUX::HorizontalAlignment::Stretch);
        addBtn.HorizontalContentAlignment(WUX::HorizontalAlignment::Center);
        addBtn.Padding({ 10, 6, 10, 6 });
        addBtn.FontSize(12);
        addBtn.Click({ this, &EnhancedInputContent::_onAddCustomClick });
        rows.Children().Append(addBtn);

        panel.Children().Append(rows);
    }

    const CustomCommand* EnhancedInputContent::_customFromTag(const IInspectable& tag) const
    {
        if (!tag) return nullptr;
        const auto boxed{ tag.try_as<winrt::Windows::Foundation::IPropertyValue>() };
        if (!boxed) return nullptr;
        const auto idx{ boxed.GetUInt64() };
        if (idx >= _customCommands.size()) return nullptr;
        return &_customCommands[static_cast<size_t>(idx)];
    }

    // Left click: Send directly (same as a built-in command card).
    void EnhancedInputContent::_onCustomCardClick(const IInspectable& sender, const WUX::RoutedEventArgs&)
    {
        const auto btn{ sender.try_as<WUX::Controls::Button>() };
        if (!btn) return;
        if (const auto* c{ _customFromTag(btn.Tag()) })
        {
            _sink->Send(winrt::hstring{ c->cmd });
        }
    }

    // Right click: copy the command text, flash the shared confirmation.
    void EnhancedInputContent::_onCustomCardRightTapped(const IInspectable& sender, const WUXI::RightTappedRoutedEventArgs&)
    {
        const auto btn{ sender.try_as<WUX::Controls::Button>() };
        if (!btn) return;
        const auto* c{ _customFromTag(btn.Tag()) };
        if (!c) return;

        DataPackage pkg{};
        pkg.SetText(winrt::hstring{ c->cmd });
        Clipboard::SetContent(pkg);

        HoverDescCmd().Text(L"✓");
        HoverDescText().Text(L"已复制到剪贴板");
        HoverDescBar().Opacity(1.0);

        if (!_copyFeedbackTimer)
        {
            _copyFeedbackTimer = WUX::DispatcherTimer{};
            _copyFeedbackTimer.Interval(std::chrono::milliseconds(1200));
            _copyFeedbackTimer.Tick({ this, &EnhancedInputContent::_onCopyFeedbackTick });
        }
        _copyFeedbackTimer.Stop();
        _copyFeedbackTimer.Start();
    }

    // Hover: show cmd + desc in the shared bottom bar (desc may be empty).
    void EnhancedInputContent::_onCustomCardPointerEntered(const IInspectable& sender, const WUXI::PointerRoutedEventArgs&)
    {
        const auto btn{ sender.try_as<WUX::Controls::Button>() };
        if (!btn) return;
        if (const auto* c{ _customFromTag(btn.Tag()) })
        {
            if (_copyFeedbackTimer)
            {
                _copyFeedbackTimer.Stop();
            }
            _hoveredCustom = c;
            HoverDescCmd().Text(winrt::hstring{ c->cmd });
            HoverDescText().Text(winrt::hstring{ c->desc });
            HoverDescBar().Opacity(1.0);
        }
    }

    void EnhancedInputContent::_onCustomCardPointerExited(const IInspectable&, const WUXI::PointerRoutedEventArgs&)
    {
        _hoveredCustom = nullptr;
        if (_copyFeedbackTimer)
        {
            _copyFeedbackTimer.Stop();
        }
        HoverDescBar().Opacity(0.0);
    }

    // ✕: remove the command at the Tag index, persist, and rebuild the group.
    void EnhancedInputContent::_onDeleteCustomClick(const IInspectable& sender, const WUX::RoutedEventArgs&)
    {
        const auto btn{ sender.try_as<WUX::Controls::Button>() };
        if (!btn) return;
        const auto tag{ btn.Tag() };
        if (!tag) return;
        const auto idx{ static_cast<size_t>(winrt::unbox_value<uint64_t>(tag)) };
        if (idx >= _customCommands.size()) return;

        _customCommands.erase(_customCommands.begin() + idx);
        _localStore.Save(_customCommands); // best-effort; in-memory list is authoritative this session
        _buildCustomCards();
    }

    // "+ 添加": reveal the inline form and focus the command box.
    void EnhancedInputContent::_onAddCustomClick(const IInspectable&, const WUX::RoutedEventArgs&)
    {
        CustomFormError().Visibility(WUX::Visibility::Collapsed);
        CustomForm().Visibility(WUX::Visibility::Visible);
        CustomCmdBox().Focus(WUX::FocusState::Programmatic);
    }

    // Confirm: validate a non-empty command, append, persist, rebuild, reset+hide form.
    void EnhancedInputContent::_onCustomFormConfirm(const IInspectable&, const WUX::RoutedEventArgs&)
    {
        const std::wstring cmd{ ComposerLogic::Trim(std::wstring_view{ CustomCmdBox().Text() }) };
        if (cmd.empty())
        {
            CustomFormError().Visibility(WUX::Visibility::Visible);
            CustomCmdBox().Focus(WUX::FocusState::Programmatic);
            return;
        }

        CustomCommand entry;
        entry.cmd = cmd;
        entry.tag = std::wstring{ ComposerLogic::Trim(std::wstring_view{ CustomTagBox().Text() }) };
        entry.desc = std::wstring{ ComposerLogic::Trim(std::wstring_view{ CustomDescBox().Text() }) };
        _customCommands.push_back(std::move(entry));
        _localStore.Save(_customCommands);

        CustomCmdBox().Text(L"");
        CustomTagBox().Text(L"");
        CustomDescBox().Text(L"");
        CustomFormError().Visibility(WUX::Visibility::Collapsed);
        CustomForm().Visibility(WUX::Visibility::Collapsed);
        _buildCustomCards();
    }

    // Cancel: discard the draft and hide the form.
    void EnhancedInputContent::_onCustomFormCancel(const IInspectable&, const WUX::RoutedEventArgs&)
    {
        CustomCmdBox().Text(L"");
        CustomTagBox().Text(L"");
        CustomDescBox().Text(L"");
        CustomFormError().Visibility(WUX::Visibility::Collapsed);
        CustomForm().Visibility(WUX::Visibility::Collapsed);
    }

    void EnhancedInputContent::_onCmdTabClicked(const IInspectable&, const WUX::RoutedEventArgs&)
    {
        if (_skillTabActive)
        {
            _skillTabActive = false;
            CmdScrollViewer().Visibility(WUX::Visibility::Visible);
            SkillPage().Visibility(WUX::Visibility::Collapsed);
        }
    }

    void EnhancedInputContent::_onSkillTabClicked(const IInspectable&, const WUX::RoutedEventArgs&)
    {
        if (!_skillTabActive)
        {
            _skillTabActive = true;
            CmdScrollViewer().Visibility(WUX::Visibility::Collapsed);
            SkillPage().Visibility(WUX::Visibility::Visible);
            _ensureSkillsScanned(); // lazy first scan — keeps panel first-open fast
        }
    }

    void EnhancedInputContent::_ensureSkillsScanned()
    {
        if (_skillsScanned)
        {
            return;
        }
        _skillsScanned = true;
        _allSkills = _skillScanner.Scan();
        _rebuildSkillCards();
    }

    void EnhancedInputContent::_refreshSkills()
    {
        _allSkills = _skillScanner.Scan();
        _rebuildSkillCards();
    }

    void EnhancedInputContent::_onSkillSearchChanged(const IInspectable&, const WUX::Controls::TextChangedEventArgs&)
    {
        _rebuildSkillCards();
    }

    void EnhancedInputContent::_onSkillRefreshClick(const IInspectable&, const WUX::RoutedEventArgs&)
    {
        _refreshSkills();
    }

    const SkillEntry* EnhancedInputContent::_skillFromTag(const IInspectable& tag) const
    {
        if (!tag) return nullptr;
        const auto boxed{ tag.try_as<winrt::Windows::Foundation::IPropertyValue>() };
        if (!boxed) return nullptr;
        const auto idx{ boxed.GetUInt64() };
        if (idx >= _allSkills.size()) return nullptr;
        return &_allSkills[static_cast<size_t>(idx)];
    }

    // Render skill cards filtered by the search box (fzf). Single column, best
    // match first; shows the inline hint when nothing scanned / nothing matches.
    void EnhancedInputContent::_rebuildSkillCards()
    {
        auto panel{ SkillListPanel() };
        panel.Children().Clear();

        const std::wstring query{ SkillSearchBox().Text() };
        std::vector<size_t> visible;
        if (query.empty())
        {
            for (size_t i = 0; i < _allSkills.size(); ++i)
            {
                visible.push_back(i);
            }
        }
        else
        {
            const auto pattern = fzf::matcher::ParsePattern(query);
            std::vector<std::pair<int32_t, size_t>> scored;
            for (size_t i = 0; i < _allSkills.size(); ++i)
            {
                const auto hay = _allSkills[i].name + L" " + _allSkills[i].description;
                if (auto m = fzf::matcher::Match(hay, pattern))
                {
                    scored.push_back({ m->Score, i });
                }
            }
            // Best score first; stable keeps the name order within equal scores.
            std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) noexcept {
                return a.first > b.first;
            });
            for (const auto& [score, i] : scored)
            {
                visible.push_back(i);
            }
        }

        // Tab badge shows the total scanned (constant across filtering), like 快捷命令.
        SkillCountRun().Text(L"  " + winrt::to_hstring(_allSkills.size()));

        // 2-column card grid (mirrors _buildCommandCards): title = /id (mono),
        // subtitle = chinese display name; full description goes to the hover bar.
        WUX::Controls::Grid grid{};
        WUX::Controls::ColumnDefinition c0{};
        WUX::Controls::ColumnDefinition c1{};
        c0.Width(WUX::GridLengthHelper::FromValueAndType(1, WUX::GridUnitType::Star));
        c1.Width(WUX::GridLengthHelper::FromValueAndType(1, WUX::GridUnitType::Star));
        grid.ColumnDefinitions().Append(c0);
        grid.ColumnDefinitions().Append(c1);
        grid.ColumnSpacing(6);
        grid.RowSpacing(6);

        int row = 0;
        int col = 0;
        for (const auto idx : visible)
        {
            const auto& sk = _allSkills[idx];
            if (col == 0)
            {
                WUX::Controls::RowDefinition rd{};
                rd.Height(WUX::GridLengthHelper::Auto());
                grid.RowDefinitions().Append(rd);
            }

            WUX::Controls::StackPanel cardContent{};
            cardContent.Spacing(1);

            WUX::Controls::TextBlock idText{};
            idText.Text(winrt::hstring{ L"/" + sk.id });
            idText.FontFamily(WUX::Media::FontFamily{ L"Cascadia Code, Consolas, Courier New" });
            idText.FontSize(12);
            // Wrap instead of ellipsis: a narrow panel folds long ids onto 2+ lines
            // rather than cutting them off (user hit truncation when resizing narrow).
            idText.TextWrapping(WUX::TextWrapping::Wrap);
            cardContent.Children().Append(idText);

            WUX::Controls::TextBlock nameText{};
            nameText.Text(winrt::hstring{ sk.name });
            nameText.FontSize(10.5);
            nameText.Opacity(0.55);
            nameText.TextTrimming(WUX::TextTrimming::CharacterEllipsis);
            cardContent.Children().Append(nameText);

            WUX::Controls::Button btn{};
            btn.Content(cardContent);
            btn.HorizontalAlignment(WUX::HorizontalAlignment::Stretch);
            btn.HorizontalContentAlignment(WUX::HorizontalAlignment::Left);
            btn.Padding({ 10, 6, 10, 6 });
            // Tag holds the index into _allSkills (stable until the next rescan).
            btn.Tag(winrt::box_value(static_cast<uint64_t>(idx)));
            btn.Click({ this, &EnhancedInputContent::_onSkillCardClick });
            btn.RightTapped({ this, &EnhancedInputContent::_onSkillCardRightTapped });
            btn.PointerEntered({ this, &EnhancedInputContent::_onSkillCardPointerEntered });
            btn.PointerExited({ this, &EnhancedInputContent::_onSkillCardPointerExited });

            WUX::Controls::Grid::SetRow(btn, row);
            WUX::Controls::Grid::SetColumn(btn, col);
            grid.Children().Append(btn);

            col++;
            if (col >= 2) { col = 0; row++; }
        }
        if (!visible.empty())
        {
            panel.Children().Append(grid);
        }

        // Inline empty / failure hint (never disrupts the terminal).
        auto hint{ SkillEmptyHint() };
        if (_allSkills.empty())
        {
            hint.Text(L"未找到技能。确认 ~/.claude/skills 下每个技能目录含 SKILL.md。");
            hint.Visibility(WUX::Visibility::Visible);
        }
        else if (visible.empty())
        {
            hint.Text(L"无匹配的技能。");
            hint.Visibility(WUX::Visibility::Visible);
        }
        else
        {
            hint.Visibility(WUX::Visibility::Collapsed);
        }
    }

    // Left click: Insert into the composer (fill, NOT execute) — skills usually need
    // the user to add context/args before sending, unlike a command's direct Send.
    void EnhancedInputContent::_onSkillCardClick(const IInspectable& sender, const WUX::RoutedEventArgs&)
    {
        const auto btn{ sender.try_as<WUX::Controls::Button>() };
        if (!btn) return;
        if (const auto* sk{ _skillFromTag(btn.Tag()) })
        {
            // Type /id into the LEFT TERMINAL's input line, no execute — the user
            // adds context / presses Enter themselves (user decision 2026-07-15).
            _sink->TypeToTerminal(winrt::hstring{ L"/" + sk->id });
        }
    }

    // Right click: copy the skill name, flash the same confirmation as command cards.
    void EnhancedInputContent::_onSkillCardRightTapped(const IInspectable& sender, const WUXI::RightTappedRoutedEventArgs&)
    {
        const auto btn{ sender.try_as<WUX::Controls::Button>() };
        if (!btn) return;
        const auto* sk{ _skillFromTag(btn.Tag()) };
        if (!sk) return;

        DataPackage pkg{};
        pkg.SetText(winrt::hstring{ L"/" + sk->id });
        Clipboard::SetContent(pkg);

        HoverDescCmd().Text(L"✓");
        HoverDescText().Text(L"已复制到剪贴板");
        HoverDescBar().Opacity(1.0);

        if (!_copyFeedbackTimer)
        {
            _copyFeedbackTimer = WUX::DispatcherTimer{};
            _copyFeedbackTimer.Interval(std::chrono::milliseconds(1200));
            _copyFeedbackTimer.Tick({ this, &EnhancedInputContent::_onCopyFeedbackTick });
        }
        _copyFeedbackTimer.Stop();
        _copyFeedbackTimer.Start();
    }

    // Hover: show the skill's name + description in the shared bottom bar.
    void EnhancedInputContent::_onSkillCardPointerEntered(const IInspectable& sender, const WUXI::PointerRoutedEventArgs&)
    {
        const auto btn{ sender.try_as<WUX::Controls::Button>() };
        if (!btn) return;
        if (const auto* sk{ _skillFromTag(btn.Tag()) })
        {
            if (_copyFeedbackTimer)
            {
                _copyFeedbackTimer.Stop();
            }
            _hoveredSkill = sk;
            HoverDescCmd().Text(winrt::hstring{ L"/" + sk->id });
            HoverDescText().Text(winrt::hstring{ sk->description });
            HoverDescBar().Opacity(1.0);
        }
    }

    // Hover exit: hide the description bar.
    void EnhancedInputContent::_onSkillCardPointerExited(const IInspectable& sender, const WUXI::PointerRoutedEventArgs&)
    {
        sender; // unused
        _hoveredSkill = nullptr;
        if (_copyFeedbackTimer)
        {
            _copyFeedbackTimer.Stop();
        }
        HoverDescBar().Opacity(0.0);
    }

    void EnhancedInputContent::SetLastActiveControl(const Control::TermControl& control)
    {
        _control = control;
        _updateTargetPill();
    }

    void EnhancedInputContent::_updateTargetPill()
    {
        if (const auto& c{ _control.get() })
        {
            const auto title{ c.Title() };
            TargetPill().Text(L"→ " + (title.empty() ? winrt::hstring{ L"终端" } : title));
            TargetPill().Opacity(0.7);
        }
        else
        {
            TargetPill().Text(L"→ 无活动终端");
            TargetPill().Opacity(0.4);
        }
    }

    // --- Composer / 万能输入 (Phase 4) ---

    void EnhancedInputContent::_onComposerTextChanged(const IInspectable&, const WUXC::TextChangedEventArgs&)
    {
        _updateSendState();
    }

    // Enter = send; Shift+Enter = newline.
    // AcceptsReturn is False, so we own all Enter behaviour. We always mark the
    // event handled to prevent the TextBox's fallback processing; for Shift+Enter
    // we manually insert a newline at the caret; for plain Enter we send.
    // Win32 GetKeyState is used for Shift detection because CoreWindow::GetKeyState
    // is unreliable in this XAML Island host (always returns "up" for modifiers).
    void EnhancedInputContent::_onComposerKeyDown(const IInspectable&, const WUXI::KeyRoutedEventArgs& e)
    {
        if (e.Key() != winrt::Windows::System::VirtualKey::Enter)
        {
            return;
        }
        e.Handled(true); // always consume Enter — we handle both cases below.
        const auto shiftDown{ (::GetKeyState(VK_SHIFT) & 0x8000) != 0 };
        if (shiftDown)
        {
            // Manually insert a newline at the caret (AcceptsReturn is False so the
            // TextBox won't do it for us).
            const auto tb{ Composer() };
            const auto selStart{ static_cast<int>(tb.SelectionStart()) };
            const auto selLen{ static_cast<int>(tb.SelectionLength()) };
            auto text{ static_cast<std::wstring>(tb.Text()) };
            text.replace(selStart, selLen, L"\n");
            tb.Text(winrt::hstring{ text });
            tb.SelectionStart(selStart + 1);
            tb.SelectionLength(0);
            return;
        }
        _trySend();
    }

    // Paste of a screenshot / files becomes an attachment; plain (multi-line) text
    // is left to the TextBox so it lands in the draft rather than being sent.
    void EnhancedInputContent::_onComposerPaste(const IInspectable&, const WUXC::TextControlPasteEventArgs& e)
    {
        DataPackageView view{ nullptr };
        try
        {
            view = Clipboard::GetContent();
        }
        CATCH_LOG();
        if (!view)
        {
            return;
        }
        if (view.Contains(StandardDataFormats::StorageItems()) || view.Contains(StandardDataFormats::Bitmap()))
        {
            e.Handled(true);
            _ingestDataViewAsync(view);
        }
    }

    void EnhancedInputContent::_onSendClick(const IInspectable&, const WUX::RoutedEventArgs&)
    {
        _trySend();
    }

    // Only files and screenshots become attachments; text drops are ignored here.
    void EnhancedInputContent::_onRootDragOver(const IInspectable&, const WUX::DragEventArgs& e)
    {
        if (e.DataView().Contains(StandardDataFormats::StorageItems()) ||
            e.DataView().Contains(StandardDataFormats::Bitmap()))
        {
            e.AcceptedOperation(DataPackageOperation::Copy);
            e.DragUIOverride().Caption(L"添加为附件");
            e.DragUIOverride().IsCaptionVisible(true);
            // Handle it here so the drop doesn't also bubble from the TextBox up to
            // the root Grid (both bind these handlers) and ingest the payload twice.
            e.Handled(true);
        }
    }

    void EnhancedInputContent::_onRootDrop(const IInspectable&, const WUX::DragEventArgs& e)
    {
        // The coroutine holds the DataView by value, so it survives past this return.
        _ingestDataViewAsync(e.DataView());
        e.Handled(true); // stop the TextBox→Grid bubble from double-ingesting.
    }

    // Classify a drop/paste payload: dropped/copied files are queued by their existing
    // path; a clipboard bitmap (screenshot, no path) is persisted to shots\ first.
    safe_void_coroutine EnhancedInputContent::_ingestDataViewAsync(DataPackageView view)
    {
        const auto strong{ get_strong() };
        std::vector<std::wstring> newPaths;

        if (view.Contains(StandardDataFormats::StorageItems()))
        {
            try
            {
                const auto items{ co_await view.GetStorageItemsAsync() };
                for (const auto& item : items)
                {
                    const auto p{ item.Path() };
                    if (!p.empty())
                    {
                        newPaths.emplace_back(p);
                    }
                }
            }
            CATCH_LOG();
        }
        else if (view.Contains(StandardDataFormats::Bitmap()))
        {
            try
            {
                const auto streamRef{ co_await view.GetBitmapAsync() };
                const auto stream{ co_await streamRef.OpenReadAsync() };
                // The clipboard hands us an uncompressed DIB/BMP, which the agent
                // can't read back from disk. Decode it and re-encode as PNG so the
                // saved attachment is a format the agent understands. The raw DIB
                // is much larger than the resulting PNG (a 4K frame is ~33 MB raw
                // but a few MB compressed), so we cap the decode input generously
                // here and let SaveImageBytes enforce the real attachment size cap
                // on the encoded PNG.
                if (stream.Size() > 0 && stream.Size() <= kMaxDecodeInputBytes)
                {
                    const auto decoder{ co_await BitmapDecoder::CreateAsync(stream) };
                    auto bitmap{ co_await decoder.GetSoftwareBitmapAsync() };
                    // The PNG encoder only accepts a narrow set of pixel formats;
                    // normalize to BGRA8 premultiplied so any source DIB layout works.
                    if (bitmap.BitmapPixelFormat() != BitmapPixelFormat::Bgra8 ||
                        bitmap.BitmapAlphaMode() != BitmapAlphaMode::Premultiplied)
                    {
                        bitmap = SoftwareBitmap::Convert(bitmap, BitmapPixelFormat::Bgra8, BitmapAlphaMode::Premultiplied);
                    }

                    InMemoryRandomAccessStream pngStream{};
                    const auto encoder{ co_await BitmapEncoder::CreateAsync(BitmapEncoder::PngEncoderId(), pngStream) };
                    encoder.SetSoftwareBitmap(bitmap);
                    co_await encoder.FlushAsync();

                    const auto pngSize{ static_cast<uint32_t>(pngStream.Size()) };
                    if (pngSize > 0)
                    {
                        pngStream.Seek(0);
                        DataReader reader{ pngStream };
                        co_await reader.LoadAsync(pngSize);
                        std::vector<uint8_t> bytes(pngSize);
                        reader.ReadBytes(winrt::array_view<uint8_t>{ bytes.data(), bytes.data() + bytes.size() });
                        if (const auto saved{ _attachmentStore.SaveImageBytes(std::as_bytes(std::span{ bytes })) })
                        {
                            newPaths.emplace_back(*saved);
                        }
                    }
                }
            }
            CATCH_LOG();
        }

        if (newPaths.empty())
        {
            co_return;
        }

        // Touch XAML + the attachment queue only on the UI thread.
        co_await wil::resume_foreground(Dispatcher());
        for (auto& p : newPaths)
        {
            _attachments.emplace_back(std::move(p));
        }
        _rebuildAttachmentChips();
        _updateSendState();
        _clearError();
    }

    // Queue files that arrived via the host-layer Win32 OLE drop target (elevated path,
    // where the XAML DataPackage drop is refused by the shell). Paths are newline-joined
    // and already on disk, so we enqueue them directly — no DataView, no image persistence.
    // Runs on the UI thread (TerminalPage routes the drop here), so XAML is touched directly.
    void EnhancedInputContent::IngestDroppedPaths(const winrt::hstring& newlineJoinedPaths)
    {
        std::wstring_view all{ newlineJoinedPaths };
        auto added{ false };
        size_t start{ 0 };
        while (start <= all.size())
        {
            auto end{ all.find(L'\n', start) };
            if (end == std::wstring_view::npos)
            {
                end = all.size();
            }
            auto line{ all.substr(start, end - start) };
            start = end + 1;
            // Tolerate CRLF payloads.
            if (!line.empty() && line.back() == L'\r')
            {
                line.remove_suffix(1);
            }
            if (line.empty())
            {
                continue;
            }
            std::wstring p{ line };
            // Skip duplicates already queued as chips.
            if (std::find(_attachments.begin(), _attachments.end(), p) != _attachments.end())
            {
                continue;
            }
            _attachments.emplace_back(std::move(p));
            added = true;
        }

        if (added)
        {
            _rebuildAttachmentChips();
            _updateSendState();
            _clearError();
        }
    }

    // True if the path's extension is one we can show as a thumbnail.
    static bool _isImagePath(const std::filesystem::path& p)
    {
        auto ext{ p.extension().wstring() };
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
        return ext == L".png" || ext == L".jpg" || ext == L".jpeg" ||
               ext == L".gif" || ext == L".webp" || ext == L".bmp";
    }

    void EnhancedInputContent::_rebuildAttachmentChips()
    {
        auto row{ AttachmentRow() };
        row.Children().Clear();
        if (_attachments.empty())
        {
            AttachmentScroller().Visibility(WUX::Visibility::Collapsed);
            return;
        }
        AttachmentScroller().Visibility(WUX::Visibility::Visible);

        // One small preview card per attachment (Codex-style, compact). Image files
        // show a thumbnail, others a document glyph; each has a ✕ in the top-right and
        // a full-path tooltip. Cards flow + wrap in the VariableSizedWrapGrid.
        for (size_t i = 0; i < _attachments.size(); ++i)
        {
            const std::filesystem::path fsPath{ _attachments[i] };

            // Card face: thumbnail for images, else a centered document glyph.
            WUX::UIElement face{ nullptr };
            const bool isImage{ _isImagePath(fsPath) };
            if (isImage)
            {
                WUXC::Image img{};
                img.Stretch(WUX::Media::Stretch::UniformToFill);
                // Load bytes ourselves (file:// is container-blocked → black card).
                _loadImageAsync(_attachments[i], img, 120);
                face = img;
            }
            else
            {
                WUXC::TextBlock glyph{};
                glyph.FontFamily(WUX::Media::FontFamily{ L"Segoe MDL2 Assets" });
                glyph.Text(L"\xE8A5"); // document glyph
                glyph.FontSize(22);
                glyph.HorizontalAlignment(WUX::HorizontalAlignment::Center);
                glyph.VerticalAlignment(WUX::VerticalAlignment::Center);
                glyph.Opacity(0.7);
                face = glyph;
            }

            WUXC::Border card{};
            card.Width(56);
            card.Height(56);
            card.CornerRadius(WUX::CornerRadius{ 6, 6, 6, 6 });
            card.Background(WUX::Media::SolidColorBrush{ { 0x40, 0x80, 0x80, 0x80 } });
            card.Child(face);
            // Clip the thumbnail to the rounded card so corners stay clean.
            card.HorizontalAlignment(WUX::HorizontalAlignment::Left);
            card.VerticalAlignment(WUX::VerticalAlignment::Top);
            // Tap the card body (not the ✕) to preview an image / open other files.
            card.Tag(winrt::box_value(static_cast<uint64_t>(i)));
            card.Tapped({ this, &EnhancedInputContent::_onAttachmentCardTapped });

            // ✕ delete affordance, top-right corner.
            WUXC::Button remove{};
            remove.Content(winrt::box_value(winrt::hstring{ L"✕" }));
            remove.FontSize(9);
            remove.Width(18);
            remove.Height(18);
            remove.Padding({ 0, 0, 0, 0 });
            remove.MinWidth(18);
            remove.HorizontalAlignment(WUX::HorizontalAlignment::Right);
            remove.VerticalAlignment(WUX::VerticalAlignment::Top);
            remove.Margin(WUX::Thickness{ 0, 1, 1, 0 });
            remove.Tag(winrt::box_value(static_cast<uint64_t>(i)));
            remove.Click({ this, &EnhancedInputContent::_onRemoveAttachmentClick });

            // Stack card + delete button in one 56x56 cell; tooltip = full path.
            WUXC::Grid cell{};
            cell.Width(56);
            cell.Height(56);
            cell.Margin(WUX::Thickness{ 2, 2, 2, 2 });
            cell.Children().Append(card);
            cell.Children().Append(remove);
            WUXC::ToolTipService::SetToolTip(cell, winrt::box_value(winrt::hstring{ fsPath.filename().wstring() }));

            row.Children().Append(cell);
        }
    }

    void EnhancedInputContent::_onRemoveAttachmentClick(const IInspectable& sender, const WUX::RoutedEventArgs&)
    {
        const auto btn{ sender.try_as<WUXC::Button>() };
        if (!btn)
        {
            return;
        }
        const auto tag{ btn.Tag() };
        if (!tag)
        {
            return;
        }
        const auto idx{ static_cast<size_t>(winrt::unbox_value<uint64_t>(tag)) };
        if (idx < _attachments.size())
        {
            _attachments.erase(_attachments.begin() + idx);
            _rebuildAttachmentChips();
            _updateSendState();
        }
    }

    // Load an image file's bytes with raw Win32 (works in the elevated, container-
    // exempt process) and SetSourceAsync — bypasses the file:// container block that
    // renders ~/.claude thumbnails black. decodePixelWidth>0 downscales; 0 = full.
    safe_void_coroutine EnhancedInputContent::_loadImageAsync(std::wstring path, WUXC::Image target, int decodePixelWidth)
    {
        const auto strong{ get_strong() };

        // Read the file off the UI thread.
        co_await winrt::resume_background();
        std::vector<uint8_t> bytes;
        try
        {
            std::ifstream in{ std::filesystem::path{ path }, std::ios::binary };
            if (in)
            {
                bytes.assign(std::istreambuf_iterator<char>{ in }, std::istreambuf_iterator<char>{});
            }
        }
        catch (...)
        {
        }
        if (bytes.empty())
        {
            co_return;
        }

        // Build the stream + BitmapImage on the UI thread (XAML thread affinity).
        co_await wil::resume_foreground(Dispatcher());
        try
        {
            InMemoryRandomAccessStream stream{};
            DataWriter writer{ stream };
            writer.WriteBytes(winrt::array_view<const uint8_t>{ bytes.data(), bytes.data() + bytes.size() });
            co_await writer.StoreAsync();
            writer.DetachStream();
            stream.Seek(0);

            WUX::Media::Imaging::BitmapImage bmp{};
            if (decodePixelWidth > 0)
            {
                bmp.DecodePixelWidth(decodePixelWidth);
            }
            co_await bmp.SetSourceAsync(stream);
            target.Source(bmp);
        }
        CATCH_LOG();
    }

    // Tap a card => open the attachment with the OS default handler (the Windows
    // photos viewer for images). ShellExecute works because this is a full-trust,
    // elevated process. The ✕ button sits on top and handles its own click, so a tap
    // that reaches the card body is never on the delete affordance.
    void EnhancedInputContent::_onAttachmentCardTapped(const IInspectable& sender, const WUXI::TappedRoutedEventArgs& e)
    {
        const auto border{ sender.try_as<WUXC::Border>() };
        if (!border || !border.Tag())
        {
            return;
        }
        const auto idx{ static_cast<size_t>(winrt::unbox_value<uint64_t>(border.Tag())) };
        if (idx >= _attachments.size())
        {
            return;
        }
        e.Handled(true);
        ::ShellExecuteW(nullptr, L"open", _attachments[idx].c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    void EnhancedInputContent::_updateSendState()
    {
        const auto text{ Composer().Text() };
        SendButton().IsEnabled(ComposerLogic::IsSendEnabled(std::wstring_view{ text }, _attachments.size()));
        AttachmentHint().Text(_attachments.empty() ? winrt::hstring{ L"" } : (winrt::hstring{ L"附件 " } + winrt::to_hstring(_attachments.size())));
    }

    // Compose text + attachment paths into one message and send. No active terminal is
    // the only synchronous failure we can see; keep the draft and show the error bar.
    void EnhancedInputContent::_trySend()
    {
        const auto text{ Composer().Text() };
        if (!ComposerLogic::IsSendEnabled(std::wstring_view{ text }, _attachments.size()))
        {
            return;
        }
        if (!_control.get())
        {
            _showError(L"无活动终端，内容已保留");
            return;
        }

        const auto payload{ ComposerLogic::BuildSendPayload(std::wstring_view{ text }, _attachments) };
        _sink->Send(winrt::hstring{ payload });

        // Success: clear only the sent content (requirements §7 scenario 8).
        Composer().Text(L"");
        _attachments.clear();
        _rebuildAttachmentChips();
        _updateSendState();
        _clearError();
    }

    // Send the message, then submit it — as two separate PTY writes (see header).
    // The first write carries the text; the agent's TUI reads that multi-byte burst
    // as a paste and only fills the composer. After a short gap a lone Enter lands in
    // its own read(), where it's taken as an interactive keypress and submits the line.
    // Fused into one write ("text\r"), the \r rides along inside the paste as a literal
    // newline and nothing is submitted — the bug this splits apart to fix.
    safe_void_coroutine EnhancedInputContent::_dispatchSendAsync(winrt::hstring text)
    {
        const auto strong{ get_strong() };

        auto control{ _control.get() };
        if (!control)
        {
            co_return;
        }

        {
            ActionAndArgs textAction{ ShortcutAction::SendInput, SendInputArgs{ text } };
            DispatchActionRequested.raise(control, textAction);
            control.Focus(WUX::FocusState::Programmatic);
        }

        // Long enough that the Enter is a separate read() from the text burst; short
        // enough to feel instant. Below this window the two coalesce and the submit
        // is swallowed.
        co_await winrt::resume_after(std::chrono::milliseconds{ 150 });
        co_await wil::resume_foreground(Dispatcher());

        // The target pane may have closed during the gap.
        control = _control.get();
        if (!control)
        {
            co_return;
        }

        ActionAndArgs enterAction{ ShortcutAction::SendInput, SendInputArgs{ winrt::hstring{ L"\r" } } };
        DispatchActionRequested.raise(control, enterAction);
        control.Focus(WUX::FocusState::Programmatic);
    }

    void EnhancedInputContent::_showError(std::wstring_view message)
    {
        ErrorText().Text(winrt::hstring{ message });
        ErrorBar().Visibility(WUX::Visibility::Visible);
    }

    void EnhancedInputContent::_clearError()
    {
        ErrorBar().Visibility(WUX::Visibility::Collapsed);
    }

    // --- Screenshot cache cleanup (Phase 6) ---

    // Manual trigger for the same double-threshold cleanup that runs on startup
    // (architecture §6). Best-effort + silent (CleanupShots never throws); flash a
    // confirmation in the shared hover bar so the click has visible feedback.
    void EnhancedInputContent::_onCleanupShotsClick(const IInspectable&, const WUX::RoutedEventArgs&)
    {
        // Manual button = clear ALL cached screenshots now (architecture §6 fallback),
        // not the age/count threshold sweep that runs automatically on startup.
        const auto removed{ _attachmentStore.PurgeAllShots() };

        HoverDescCmd().Text(L"✓");
        HoverDescText().Text(L"已清理截图缓存（删除 " + winrt::to_hstring(removed) + L" 个）");
        HoverDescBar().Opacity(1.0);

        if (!_copyFeedbackTimer)
        {
            _copyFeedbackTimer = WUX::DispatcherTimer{};
            _copyFeedbackTimer.Interval(std::chrono::milliseconds(1200));
            _copyFeedbackTimer.Tick({ this, &EnhancedInputContent::_onCopyFeedbackTick });
        }
        _copyFeedbackTimer.Stop();
        _copyFeedbackTimer.Start();
    }

#pragma region IPaneContent

    WUX::FrameworkElement EnhancedInputContent::GetRoot()
    {
        return *this;
    }

    void EnhancedInputContent::Focus(WUX::FocusState reason)
    {
        Composer().Focus(reason);
    }

    void EnhancedInputContent::Close()
    {
        CloseRequested.raise(*this, nullptr);
    }

    INewContentArgs EnhancedInputContent::GetNewTerminalArgs(const BuildStartupKind /*kind*/) const
    {
        return BaseContentArgs(L"enhancedInput");
    }

    winrt::hstring EnhancedInputContent::Icon() const
    {
        static constexpr std::wstring_view glyph{ L"\xe756" };
        return winrt::hstring{ glyph };
    }

#pragma endregion
}
