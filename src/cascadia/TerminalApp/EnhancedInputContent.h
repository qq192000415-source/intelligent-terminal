// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include "EnhancedInputContent.g.h"
#include "BasicPaneEvents.h"
#include "EnhancedInput/TerminalSink.h"
#include "EnhancedInput/CommandData.h"
#include "EnhancedInput/AttachmentStore.h"
#include "EnhancedInput/ComposerLogic.h"
#include "EnhancedInput/SkillScanner.h"
#include "EnhancedInput/LocalStore.h"

namespace winrt::TerminalApp::implementation
{
    struct EnhancedInputContent : EnhancedInputContentT<EnhancedInputContent>, BasicPaneEvents
    {
    public:
        EnhancedInputContent();

        void SetLastActiveControl(const Microsoft::Terminal::Control::TermControl& control);

        // Queue already-on-disk paths as attachments. Called on the UI thread by
        // TerminalPage's host-level OLE drop routing (elevated drag path). Newline-separated.
        void IngestDroppedPaths(const winrt::hstring& newlineSeparatedPaths);

#pragma region IPaneContent
        winrt::Windows::UI::Xaml::FrameworkElement GetRoot();
        void UpdateSettings(const winrt::Microsoft::Terminal::Settings::Model::CascadiaSettings&) {}
        winrt::Windows::Foundation::Size MinimumSize() { return { 280, 200 }; }
        void Focus(winrt::Windows::UI::Xaml::FocusState reason = winrt::Windows::UI::Xaml::FocusState::Programmatic);
        void Close();
        winrt::Microsoft::Terminal::Settings::Model::INewContentArgs GetNewTerminalArgs(BuildStartupKind kind) const;
        winrt::hstring Title() { return L"增强输入"; }
        uint64_t TaskbarState() { return 0; }
        uint64_t TaskbarProgress() { return 0; }
        bool ReadOnly() { return false; }
        winrt::hstring Icon() const;
        Windows::Foundation::IReference<winrt::Windows::UI::Color> TabColor() const noexcept { return nullptr; }
        winrt::Windows::UI::Xaml::Media::Brush BackgroundBrush() { return Background(); }
#pragma endregion

        til::typed_event<winrt::Windows::Foundation::IInspectable, Microsoft::Terminal::Settings::Model::ActionAndArgs> DispatchActionRequested;

    private:
        friend struct EnhancedInputContentT<EnhancedInputContent>;

        winrt::weak_ref<Microsoft::Terminal::Control::TermControl> _control{ nullptr };
        std::shared_ptr<ITerminalSink> _sink{ nullptr };

        // Tracks which tab is active (false = 快捷命令, true = 技能).
        bool _skillTabActive{ false };

        // Command entry currently under the pointer; lets the copy-confirmation
        // timer restore the correct description when it reverts.
        const CommandEntry* _hoveredEntry{ nullptr };
        // One-shot timer that reverts the "已复制" confirmation back to the description.
        winrt::Windows::UI::Xaml::DispatcherTimer _copyFeedbackTimer{ nullptr };

        // Skills (Phase 5). Scanned lazily on first switch to the skill tab and on
        // refresh; _allSkills is the full scan, cards store an index into it (stable
        // between rebuilds — the vector is only replaced wholesale by a rescan).
        // _hoveredSkill lets the copy-confirmation timer restore the right text.
        SkillScanner _skillScanner{};
        std::vector<SkillEntry> _allSkills;
        bool _skillsScanned{ false };
        const SkillEntry* _hoveredSkill{ nullptr };

        // Composer / 万能输入 (Phase 4). _attachmentStore default-resolves to
        // %USERPROFILE%\.claude\shots; _attachments holds the pending local paths
        // shown as chips and appended to the send payload.
        AttachmentStore _attachmentStore{};
        std::vector<std::wstring> _attachments;

        // Custom commands (Phase 6). Loaded from ~/.claude/custom_commands.json at
        // construction and re-saved on every add / delete. Cards store an index into
        // this vector; _hoveredCustom lets the copy-confirmation timer restore the
        // right hover text. The vector is only mutated on the UI thread, and any
        // mutation is immediately followed by a full card rebuild, so a stale
        // _hoveredCustom pointer can't outlive its element (cleared before rebuild).
        LocalStore _localStore{};
        std::vector<CustomCommand> _customCommands;
        const CustomCommand* _hoveredCustom{ nullptr };

        void _updateTargetPill();
        void _buildCommandCards();

        // Tab switching
        void _onCmdTabClicked(const Windows::Foundation::IInspectable&, const winrt::Windows::UI::Xaml::RoutedEventArgs&);
        void _onSkillTabClicked(const Windows::Foundation::IInspectable&, const winrt::Windows::UI::Xaml::RoutedEventArgs&);

        // Command card events — the Button Tag holds a const CommandEntry* cast to IInspectable via boxing
        void _onCmdCardClick(const Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::RoutedEventArgs&);
        void _onCmdCardRightTapped(const Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::Input::RightTappedRoutedEventArgs&);
        void _onCmdCardPointerEntered(const Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs&);
        void _onCmdCardPointerExited(const Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs&);

        // Reverts the "已复制" confirmation flash back to the hovered card's description (or hides the bar).
        void _onCopyFeedbackTick(const Windows::Foundation::IInspectable&, const Windows::Foundation::IInspectable&);

        // Helper: retrieve the CommandEntry pointer stored in a Button's Tag.
        static const CommandEntry* _entryFromTag(const Windows::Foundation::IInspectable& tag);

        // --- Custom commands (Phase 6) ---

        // Render the "自定义" group (cards for each saved command + a "+ 添加" card)
        // into CustomGroupPanel. Called after the built-in groups at construction and
        // again after every add / delete.
        void _buildCustomCards();
        // The CustomCommand* behind a card Button's Tag (boxed index into _customCommands).
        const CustomCommand* _customFromTag(const Windows::Foundation::IInspectable& tag) const;

        // Custom card events — mirror the built-in command cards: left = Send (direct),
        // right = copy, hover = description bar.
        void _onCustomCardClick(const Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::RoutedEventArgs&);
        void _onCustomCardRightTapped(const Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::Input::RightTappedRoutedEventArgs&);
        void _onCustomCardPointerEntered(const Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs&);
        void _onCustomCardPointerExited(const Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs&);
        // ✕ on a card deletes that command (index in Tag), persists, and rebuilds.
        void _onDeleteCustomClick(const Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::RoutedEventArgs&);
        // ↑/↓ on a custom card swaps it with its neighbor, persists, and rebuilds.
        void _onMoveUpCustomClick(const Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::RoutedEventArgs&);
        void _onMoveDownCustomClick(const Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::RoutedEventArgs&);

        // Inline add form: the "+ 添加" card toggles CustomForm visible; Confirm
        // validates a non-empty command, appends, persists, and rebuilds; Cancel hides.
        void _onAddCustomClick(const Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::RoutedEventArgs&);
        void _onCustomFormConfirm(const Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::RoutedEventArgs&);
        void _onCustomFormCancel(const Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::RoutedEventArgs&);

        // --- Skills (Phase 5) ---

        // Scan on first switch to the skill tab (lazy — keeps first-open fast).
        void _ensureSkillsScanned();
        // Rescan from disk (the ↺ button) and re-render with the current filter.
        void _refreshSkills();
        // Render skill cards into SkillListPanel, filtered by the search box via fzf.
        // Shows the inline empty/failure hint when nothing matches / nothing scanned.
        void _rebuildSkillCards();
        // The SkillEntry* behind a card Button's Tag (boxed index into _allSkills).
        const SkillEntry* _skillFromTag(const Windows::Foundation::IInspectable& tag) const;

        void _onSkillSearchChanged(const Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::Controls::TextChangedEventArgs&);
        void _onSkillRefreshClick(const Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::RoutedEventArgs&);
        // Skill card events. Left click = Insert (fill composer, NOT execute — the
        // opposite of a command card's Send); right = copy; hover = description bar.
        void _onSkillCardClick(const Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::RoutedEventArgs&);
        void _onSkillCardRightTapped(const Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::Input::RightTappedRoutedEventArgs&);
        void _onSkillCardPointerEntered(const Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs&);
        void _onSkillCardPointerExited(const Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs&);

        // --- Composer / 万能输入 (Phase 4) ---

        // Composer text box events.
        void _onComposerTextChanged(const Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::Controls::TextChangedEventArgs&);
        void _onComposerKeyDown(const Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::Input::KeyRoutedEventArgs& e);
        // Intercept paste of images / files; plain text falls through to the box.
        void _onComposerPaste(const Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::Controls::TextControlPasteEventArgs& e);
        void _onSendClick(const Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::RoutedEventArgs&);

        // Drag / drop onto the whole panel. The drop delegates to the fire-and-forget
        // _ingestDataViewAsync (which holds the DataView by value), so the handler itself
        // stays a plain void — you don't co_await a safe_void_coroutine.
        void _onRootDragOver(const Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::DragEventArgs& e);
        void _onRootDrop(const Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::DragEventArgs& e);

        // Shared drop/paste ingestion: classify a DataPackageView into image files,
        // storage items, and screenshots; queue attachments. By-value for the coroutine.
        safe_void_coroutine _ingestDataViewAsync(winrt::Windows::ApplicationModel::DataTransfer::DataPackageView view);

        // Attachment queue → chip row.
        void _rebuildAttachmentChips();
        void _onRemoveAttachmentClick(const Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::RoutedEventArgs&);

        // Load an image file's bytes ourselves (raw Win32, works elevated) into an
        // in-memory stream and SetSourceAsync onto target — a file:// URI to
        // ~/.claude is blocked by the MSIX app container (no broadFileSystemAccess),
        // which is why thumbnails render black. decodePixelWidth downscales in the
        // decoder (thumbnails are 120px wide).
        safe_void_coroutine _loadImageAsync(std::wstring path, winrt::Windows::UI::Xaml::Controls::Image target, int decodePixelWidth);
        // Tap a card => open the file with the OS default handler (photos viewer for
        // images), via elevated ShellExecute (works in this full-trust process).
        void _onAttachmentCardTapped(const Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::Input::TappedRoutedEventArgs&);

        // Enable/disable the send button + attachment hint from current draft state.
        void _updateSendState();
        // Compose text + attachment paths and send; on no active terminal keep the
        // draft and surface the error bar (requirements §7 scenario 8).
        void _trySend();
        // Deliver a composed message, then submit it. The text and the Enter are two
        // separate PTY writes with a short gap between them: the agent's TUI treats a
        // single multi-byte write as a paste (any \r inside is a literal newline, not
        // a submit), so a lone Enter arriving afterwards is read as an interactive
        // keypress and actually submits the line.
        safe_void_coroutine _dispatchSendAsync(winrt::hstring text);
        void _showError(std::wstring_view message);
        void _clearError();

        // --- Screenshot cache cleanup (Phase 6) ---
        // Manual entry point for the double-threshold cleanup that also runs on
        // startup (architecture §6). Flashes a confirmation in the hover bar.
        void _onCleanupShotsClick(const Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::RoutedEventArgs&);
    };
}

namespace winrt::TerminalApp::factory_implementation
{
    BASIC_FACTORY(EnhancedInputContent);
}
