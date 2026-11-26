#include "modals/modal_telegram_settings.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

#include "editor/clipboard_controller.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/component_options.hpp"
#include "ftxui/dom/elements.hpp"
#include "modals/modal_interface.hpp"
#include "modals/modal_window.hpp"
#include "ui_button.hpp"
#include "ui_selectable_input.hpp"

namespace textlt {
namespace {

using namespace ftxui;

Component TelegramButton(
    const Theme& theme,
    std::string label,
    ButtonRole role,
    std::function<void()> action) {
    auto option = ButtonOption::Simple();
    option.label = label;
    option.on_click = std::move(action);
    option.transform = [&theme, label = std::move(label), role](const EntryState& state) {
        const auto spec = ButtonSpecFromLabel(
            label, role, ButtonVariant::Minimal, ButtonSize::Compact);
        return RenderModalFlatButton(theme, spec, state.focused);
    };
    return Button(std::move(option));
}

} // namespace

class TelegramSettingsContent final
    : public IModalContent,
      public std::enable_shared_from_this<TelegramSettingsContent> {
public:
    TelegramSettingsContent(
        const Theme& theme,
        telegram::TelegramProvider& provider,
        std::function<void()> close,
        TelegramSettingsModal::ClipboardRead read_clipboard)
        : theme_(theme), provider_(provider), close_(std::move(close)),
          read_clipboard_(std::move(read_clipboard)),
          draft_(provider.Settings()), original_token_(draft_.bot_token) {
        InputOption token_option;
        token_option.multiline = false;
        token_option.password = &token_hidden_;
        token_option.transform = [this](InputState state) {
            return state.element | bgcolor(theme_.modal_input_bg) |
                color(theme_.modal_input_fg);
        };
        token_cursor_ = static_cast<int>(draft_.bot_token.size());
        token_selection_ = std::make_shared<InputSelection>();
        auto token_input = MakeSelectableInput(
            draft_.bot_token, token_cursor_, "Paste bot token", std::move(token_option),
            token_selection_);
        token_input_ = CatchEvent(token_input, [this, token_input](Event event) {
            const std::string before = draft_.bot_token;
            if (!token_input->OnEvent(std::move(event))) return false;
            if (before != draft_.bot_token) TokenChanged();
            return true;
        });

        auto visibility = ButtonOption::Simple();
        visibility.label = &visibility_label_;
        visibility.on_click = [this] {
            token_hidden_ = !token_hidden_;
            visibility_label_ = token_hidden_ ? "Show" : "Hide";
            request_redraw_();
        };
        visibility.transform = [this](const EntryState& state) {
            const auto spec = ButtonSpecFromLabel(
                state.label, ButtonRole::Toggle, ButtonVariant::Minimal, ButtonSize::Compact);
            return RenderModalFlatButton(theme_, spec, state.focused);
        };
        visibility_button_ = Button(std::move(visibility));
        test_button_ = TelegramButton(
            theme_, "Test Connection", ButtonRole::Primary, [this] { TestConnection(); });

        auto reconnect_option = CheckboxOption::Simple();
        reconnect_option.label = "Automatically reconnect";
        reconnect_option.checked = &draft_.auto_reconnect;
        reconnect_option.transform = [this](const EntryState& state) {
            return RenderCheckbox(state.label, state.state, state.focused);
        };
        reconnect_ = Checkbox(reconnect_option);

        auto launch_option = CheckboxOption::Simple();
        launch_option.label = "Start on application launch";
        launch_option.checked = &draft_.start_on_launch;
        launch_option.transform = [this](const EntryState& state) {
            return RenderCheckbox(state.label, state.state, state.focused);
        };
        start_on_launch_ = Checkbox(launch_option);

        auto chat_menu_option = MenuOption::Vertical();
        chat_menu_option.on_enter = [this] { ToggleSelectedChat(); };
        chat_menu_option.entries_option.transform = [this](const EntryState& state) {
            Element row = text(state.label);
            if (state.index >= 0 &&
                state.index < static_cast<int>(chat_row_boxes_.size())) {
                row |= reflect(chat_row_boxes_[state.index]);
            }
            return state.active
                ? row | bgcolor(theme_.modal_selected_item_bg) |
                    color(theme_.modal_selected_item_fg)
                : row | color(theme_.modal_text_color);
        };
        chat_menu_ = Menu(&chat_labels_, &selected_chat_, std::move(chat_menu_option));
        chat_menu_input_ = CatchEvent(chat_menu_, [this](Event event) {
            if (!event.is_mouse() || event.mouse().button != Mouse::Left ||
                event.mouse().motion != Mouse::Pressed) {
                return false;
            }
            const auto& mouse = event.mouse();
            const auto row = std::find_if(chat_row_boxes_.begin(), chat_row_boxes_.end(),
                [&](const Box& box) { return box.Contain(mouse.x, mouse.y); });
            if (row == chat_row_boxes_.end()) return false;

            const int clicked = static_cast<int>(row - chat_row_boxes_.begin());
            const auto now = std::chrono::steady_clock::now();
            const bool double_click = clicked == last_chat_click_index_ &&
                now - last_chat_click_ <= std::chrono::milliseconds(500);
            last_chat_click_ = double_click ? std::chrono::steady_clock::time_point{} : now;
            last_chat_click_index_ = double_click ? -1 : clicked;

            const bool handled = chat_menu_->OnEvent(event);
            if (handled && double_click) {
                selected_chat_ = clicked;
                ToggleSelectedChat();
            }
            return handled;
        });
        chat_list_ = Renderer(chat_menu_input_, [this] {
            RefreshChats();
            if (chat_labels_.empty()) {
                return text("No chats discovered yet. Message the bot to discover a chat.") |
                    dim | yflex;
            }
            return chat_menu_input_->Render() | vscroll_indicator | yframe | flex;
        });

        tree_ = CatchEvent(Container::Vertical({
            token_input_,
            Container::Horizontal({visibility_button_, test_button_}),
            reconnect_,
            start_on_launch_,
            Renderer([this] { return RenderBotIdentity(); }),
            Renderer([] {
                return text("Select a discovered chat and press Enter to toggle authorization:");
            }),
            chat_list_,
        }), [this](Event event) {
            if (event == Event::Escape) {
                close_();
                return true;
            }
            if (event == Event::Return && chat_menu_->Focused()) {
                ToggleSelectedChat();
                return true;
            }
            return false;
        });
    }

    void Open() {
        draft_ = provider_.Settings();
        original_token_ = draft_.bot_token;
        token_hidden_ = true;
        visibility_label_ = "Show";
        token_cursor_ = static_cast<int>(draft_.bot_token.size());
        *token_selection_ = {};
        test_status_.clear();
        bot_identity_ = {draft_.bot_first_name, draft_.bot_username};
        RefreshChats();
    }

    void PasteToken() {
        if (!read_clipboard_) {
            test_status_ = "Clipboard access is unavailable.";
            request_redraw_();
            return;
        }
        const std::weak_ptr<TelegramSettingsContent> weak = shared_from_this();
        read_clipboard_([weak](std::optional<std::string> clipboard) {
            const auto self = weak.lock();
            if (!self) return;
            if (!clipboard) {
                self->test_status_ = "Could not read the clipboard.";
                self->request_redraw_();
                return;
            }
            std::string token = ClipboardController::NormalizeText(*clipboard, false);
            const auto first = token.find_first_not_of(" \t");
            if (first == std::string::npos) {
                token.clear();
            } else {
                token = token.substr(first, token.find_last_not_of(" \t") - first + 1);
            }
            if (token != self->draft_.bot_token) {
                self->draft_.bot_token = std::move(token);
                self->token_cursor_ = static_cast<int>(self->draft_.bot_token.size());
                *self->token_selection_ = {};
                self->TokenChanged();
                return;
            }
            self->test_status_.clear();
            self->request_redraw_();
        });
    }

    void Cancel() { close_(); }

    bool Save() {
        std::string error;
        if (!provider_.SaveSettings(draft_, error)) {
            test_status_ = error.empty() ? "Could not save Telegram settings." : error;
            request_redraw_();
            return false;
        }
        close_();
        return true;
    }

    std::string GetTitle() override { return "Telegram Settings"; }

    ModalSizePreference GetModalSizePreference() const override {
        const auto terminal = Terminal::Size();
        return {std::clamp(terminal.dimx, 36, 90), std::clamp(terminal.dimy, 18, 34)};
    }

    ModalFrameStyle GetModalFrameStyle() const override {
        return ModalFrameStyle::TitleInBorder;
    }

    Component GetMainComponent() override { return tree_; }

    Element Render() override {
        RefreshChats();
        Elements body{
            text("Bot token") | bold,
            hbox({
                token_input_->Render() | flex,
                text(" "),
                visibility_button_->Render(),
                text(" "),
                test_button_->Render(),
            }),
            reconnect_->Render(),
            start_on_launch_->Render(),
            RenderBotIdentity(),
            separator() | color(theme_.modal_border),
            text("Authorized chats") | bold,
            text("Select a discovered chat and press Enter to toggle authorization:") | dim,
            chat_list_->Render() | size(HEIGHT, LESS_THAN, 8) | flex,
            text("Connection: " + provider_.Status()) | dim |
                color(theme_.modal_text_color),
            test_status_.empty() ? emptyElement()
                : text("Test: " + test_status_) | dim | color(theme_.modal_text_color),
        };
        return vbox(std::move(body)) | bgcolor(theme_.modal_background) |
            color(theme_.modal_foreground);
    }

    void SetRequestRedraw(std::function<void()> callback) {
        request_redraw_ = callback ? std::move(callback) : [] {};
    }

private:
    void TokenChanged() {
        draft_.authorized_chat_ids.clear();
        bot_identity_ = {};
        draft_.bot_first_name.clear();
        draft_.bot_username.clear();
        test_status_.clear();
        request_redraw_();
    }

    Element RenderBotIdentity() const {
        if (bot_identity_.username.empty() && bot_identity_.first_name.empty()) {
            return text("Bot: not tested") | dim;
        }
        const std::string name = bot_identity_.first_name.empty()
            ? bot_identity_.username : bot_identity_.first_name;
        const std::string username = bot_identity_.username.empty()
            ? "" : " (@" + bot_identity_.username + ")";
        return text("Authenticated bot: " + name + username);
    }

    void TestConnection() {
        test_status_ = "Testing Telegram connection…";
        request_redraw_();
        const auto result = provider_.TestConnection(draft_.bot_token);
        if (!result.success) {
            test_status_ = result.error;
            request_redraw_();
            return;
        }
        bot_identity_ = result.bot;
        draft_.bot_first_name = result.bot.first_name;
        draft_.bot_username = result.bot.username;
        test_status_ = "Connection successful.";
        request_redraw_();
    }

    void RefreshChats() {
        if (draft_.bot_token != original_token_) {
            rendered_chats_.clear();
            chat_labels_.clear();
            chat_row_boxes_.clear();
            selected_chat_ = 0;
            return;
        }
        const auto chats = provider_.KnownChats();
        rendered_chats_ = chats;
        chat_labels_.clear();
        for (const auto& chat : chats) {
            const bool authorized = std::find(
                draft_.authorized_chat_ids.begin(), draft_.authorized_chat_ids.end(),
                chat.id) != draft_.authorized_chat_ids.end();
            const std::string username = chat.username.empty() ? "" : "  @" + chat.username;
            chat_labels_.push_back(
                std::string(authorized ? "[x] " : "[ ] ") +
                (chat.title.empty() ? chat.id : chat.title) + username);
        }
        if (selected_chat_ >= static_cast<int>(chat_labels_.size())) {
            selected_chat_ = std::max(0, static_cast<int>(chat_labels_.size()) - 1);
        }
        chat_row_boxes_.resize(chat_labels_.size());
    }

    void ToggleSelectedChat() {
        RefreshChats();
        if (selected_chat_ < 0 || selected_chat_ >= static_cast<int>(rendered_chats_.size())) return;
        const std::string& id = rendered_chats_[selected_chat_].id;
        const auto found = std::find(draft_.authorized_chat_ids.begin(),
                                     draft_.authorized_chat_ids.end(), id);
        if (found == draft_.authorized_chat_ids.end()) {
            draft_.authorized_chat_ids.push_back(id);
        } else {
            draft_.authorized_chat_ids.erase(found);
        }
        RefreshChats();
        request_redraw_();
    }

    Element RenderCheckbox(const std::string& label, bool checked, bool focused) const {
        auto row = text(std::string(checked ? "[x] " : "[ ] ") + label);
        return focused
            ? row | color(theme_.modal_selected_item_fg) |
                bgcolor(theme_.modal_selected_item_bg)
            : row | color(theme_.modal_text_color);
    }

    const Theme& theme_;
    telegram::TelegramProvider& provider_;
    std::function<void()> close_;
    std::function<void()> request_redraw_ = [] {};
    TelegramSettingsModal::ClipboardRead read_clipboard_;
    telegram::TelegramSettings draft_;
    std::string original_token_;
    telegram::TelegramBotIdentity bot_identity_;
    std::vector<telegram::TelegramChat> rendered_chats_;
    std::vector<std::string> chat_labels_;
    std::vector<Box> chat_row_boxes_;
    std::string test_status_;
    std::string visibility_label_ = "Show";
    int token_cursor_ = 0;
    std::shared_ptr<InputSelection> token_selection_;
    int selected_chat_ = 0;
    int last_chat_click_index_ = -1;
    std::chrono::steady_clock::time_point last_chat_click_{};
    bool token_hidden_ = true;
    Component token_input_;
    Component visibility_button_;
    Component test_button_;
    Component reconnect_;
    Component start_on_launch_;
    Component chat_menu_;
    Component chat_menu_input_;
    Component chat_list_;
    Component tree_;
};

void TelegramSettingsModal::Configure(
    const Theme& theme,
    telegram::TelegramProvider& provider,
    std::function<void()> close,
    std::function<void()> request_redraw,
    ClipboardRead read_clipboard) {
    content_ = std::make_shared<TelegramSettingsContent>(
        theme, provider, close, std::move(read_clipboard));
    content_->SetRequestRedraw(std::move(request_redraw));
    modal_ = std::make_shared<ModalWindow>(content_, &theme, std::move(close));
    modal_->SetFooterButtons({
        {"Paste token", [content = content_] { content->PasteToken(); }, ButtonRole::Utility},
        {"Save", [content = content_] { content->Save(); }, ButtonRole::Primary},
        {"Cancel", [content = content_] { content->Cancel(); }, ButtonRole::Cancel},
    }, 1);
}

void TelegramSettingsModal::Open() {
    if (content_) content_->Open();
    open_ = true;
    if (modal_) modal_->TakeFocus();
}

void TelegramSettingsModal::Close() {
    open_ = false;
}

Component TelegramSettingsModal::View() const {
    return modal_;
}

} // namespace textlt
