#include "modals/modal_irc_settings.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <mutex>
#include <utility>

#include "editor/clipboard_controller.hpp"
#include "editor_utils.hpp"
#include "ftxui/component/component_options.hpp"
#include "irc/irc_settings.hpp"
#include "keyboard_shortcuts.hpp"
#include "modals/modal_window.hpp"
#include "theme.hpp"
#include "ui_button.hpp"

namespace textlt {
namespace {

using namespace ftxui;
using namespace std::chrono_literals;

int SettingsWidth() { return std::min(Terminal::Size().dimx, 78); }
int LabelWidth() { return SettingsWidth() < 34 ? 14 : 20; }

class SettingsField final : public ComponentBase {
public:
    using Copy = std::function<bool(const std::string&)>;

    SettingsField(const Theme& theme, std::string& value, bool secret, Copy copy)
        : theme_(theme), value_(value), secret_(secret), copy_(std::move(copy)) {
        InputOption option;
        option.multiline = false;
        option.password = secret_;
        option.cursor_position = &cursor_;
        option.transform = [this](InputState state) { return theme_.InputTransform(std::move(state)); };
        input_ = Input(&value_, " ", std::move(option));
        Add(input_);
    }

    void Reset() {
        cursor_ = static_cast<int>(value_.size());
        anchor_ = cursor_;
        scroll_ = 0;
        last_click_ = {};
        drag_.reset();
    }

    Element OnRender() override {
        if (!Focused()) {
            anchor_ = cursor_;
            last_click_ = {};
            drag_.reset();
        }
        cursor_ = std::clamp(cursor_, 0, static_cast<int>(value_.size()));
        anchor_ = std::clamp(anchor_, 0, static_cast<int>(value_.size()));
        const int width = std::max(1, SettingsWidth() - LabelWidth() - 3);
        const auto shown = Display(0, value_.size());
        const int cursor_column = WidthAt(cursor_);
        const auto display_width = ftxui::string_width(shown);
        if (Focused() && anchor_ == cursor_ && display_width < width) {
            scroll_ = 0;
            return input_->Render() | reflect(box_);
        }
        scroll_ = std::clamp(scroll_, std::max(0, cursor_column - width + 1), cursor_column);
        const auto begin = ColumnIndex(shown, 0, scroll_);
        const auto end = ColumnIndex(shown, begin, width);
        const auto anchor_column = WidthAt(anchor_);
        const auto [lower, upper] = std::minmax(anchor_column, cursor_column);
        Elements cells;
        for (auto i = begin; i < end;) {
            const auto next = utils::NextUtf8CodepointStart(shown, i);
            const int column = static_cast<int>(utils::Utf8DisplayWidth(shown, 0, i));
            auto glyph = text(shown.substr(i, next - i));
            if (Focused() && column >= lower && column < upper) glyph |= inverted;
            if (Focused() && column == cursor_column) glyph |= focusCursorBlockBlinking;
            cells.push_back(std::move(glyph));
            i = next;
        }
        if (Focused() && cursor_column >= display_width) {
            cells.push_back(text(" ") | focusCursorBlockBlinking);
        }
        cells.push_back(filler());
        return theme_.InputTransform({hbox(std::move(cells)) | xflex, false, Focused(), false}) | reflect(box_);
    }

    bool OnEvent(Event event) override {
        if (event.is_mouse()) return MouseEvent(event);
        if (!Focused()) return false;
        if (MatchesShortcut(event, ShortcutModifier::Ctrl, 'a')) {
            anchor_ = 0;
            cursor_ = static_cast<int>(value_.size());
            return true;
        }
        const bool cut = MatchesShortcut(event, ShortcutModifier::Ctrl, 'x');
        if (cut || MatchesShortcut(event, ShortcutModifier::Ctrl, 'c')) {
            const auto [begin, end] = std::minmax(anchor_, cursor_);
            if (!secret_ && begin != end && copy_ && copy_(value_.substr(begin, end - begin)) &&
                cut) EraseSelection();
            return true;
        }
        if (auto pasted = ClipboardController::PastedText(event, false)) {
            EraseSelection();
            value_.insert(cursor_, *pasted);
            cursor_ += static_cast<int>(pasted->size());
            anchor_ = cursor_;
            return true;
        }
        if (event.is_character() || event == Event::Backspace || event == Event::Delete) {
            if (anchor_ != cursor_) {
                EraseSelection();
                if (!event.is_character()) return true;
            }
        }
        const bool handled = input_->OnEvent(event);
        if (handled) anchor_ = cursor_;
        return handled;
    }

private:
    static std::size_t ColumnIndex(const std::string& text, std::size_t start, int columns) {
        return utils::Utf8ByteIndexAtDisplayColumn(text, start, std::max(0, columns));
    }

    std::string Display(std::size_t start, std::size_t end) const {
        if (!secret_) return value_.substr(start, end - start);
        std::string mask;
        for (auto i = start; i < end; i = utils::NextUtf8CodepointStart(value_, i)) mask += '*';
        return mask;
    }

    int WidthAt(std::size_t byte) const {
        return ftxui::string_width(Display(0, byte));
    }

    int MouseIndex(int x) const {
        const auto column = std::max(0, scroll_ + x - box_.x_min);
        if (!secret_) return static_cast<int>(ColumnIndex(value_, 0, column));
        std::size_t i = 0;
        for (int n = 0; n < column && i < value_.size(); ++n) i = utils::NextUtf8CodepointStart(value_, i);
        return static_cast<int>(i);
    }

    bool MouseEvent(Event event) {
        const auto mouse = event.mouse();
        if (drag_ && mouse.motion == Mouse::Released) {
            drag_.reset();
            return true;
        }
        if (drag_ && mouse.motion == Mouse::Moved) {
            cursor_ = MouseIndex(mouse.x);
            return true;
        }
        if (mouse.button != Mouse::Left || mouse.motion != Mouse::Pressed ||
            !box_.Contain(mouse.x, mouse.y)) return false;
        drag_ = CaptureMouse(event);
        if (!drag_) return false;
        TakeFocus();
        cursor_ = anchor_ = MouseIndex(mouse.x);
        const auto now = std::chrono::steady_clock::now();
        if (now - last_click_ <= 500ms && mouse.x == last_x_ && !value_.empty()) {
            SelectWord();
            last_click_ = {};
        } else {
            last_click_ = now;
            last_x_ = mouse.x;
        }
        return true;
    }

    void SelectWord() {
        if (secret_) {
            anchor_ = 0;
            cursor_ = static_cast<int>(value_.size());
            return;
        }
        if (static_cast<std::size_t>(cursor_) == value_.size()) {
            --cursor_;
            while (cursor_ && (static_cast<unsigned char>(value_[cursor_]) & 0xc0) == 0x80) --cursor_;
        }
        const auto word = [&](std::size_t at) {
            return static_cast<unsigned char>(value_[at]) >= 128 || utils::IsWordCharacter(value_[at]);
        };
        const bool select_word = word(cursor_);
        std::size_t begin = cursor_;
        while (begin > 0) {
            const auto prior = utils::PreviousUtf8CodepointStart(value_, begin);
            if (word(prior) != select_word) break;
            begin = prior;
        }
        auto end = utils::NextUtf8CodepointStart(value_, cursor_);
        while (end < value_.size() && word(end) == select_word) end = utils::NextUtf8CodepointStart(value_, end);
        anchor_ = static_cast<int>(begin);
        cursor_ = static_cast<int>(end);
    }

    void EraseSelection() {
        const auto [begin, end] = std::minmax(anchor_, cursor_);
        value_.erase(begin, end - begin);
        cursor_ = anchor_ = begin;
    }

    const Theme& theme_;
    std::string& value_;
    bool secret_;
    Copy copy_;
    Component input_;
    Box box_{1, 0, 1, 0};
    CapturedMouse drag_;
    int cursor_ = 0;
    int anchor_ = 0;
    int scroll_ = 0;
    int last_x_ = 0;
    std::chrono::steady_clock::time_point last_click_{};
};

class SettingsForm final : public ComponentBase {
public:
    void SetFields(Components parts) {
        count_ = static_cast<int>(parts.size());
        Add(Container::Vertical(std::move(parts), &selected_));
    }

    void Reset() { selected_ = 0; }
    void SetEditing(bool editing) {
        editing_ = editing;
        if (!editing_) Reset();
    }

    bool OnEvent(Event event) override {
        if (!editing_) return false;
        if ((event == Event::Tab && selected_ == count_ - 1) ||
            (event == Event::TabReverse && selected_ == 0)) return false;
        if (event.is_mouse() && box.Contain(event.mouse().x, event.mouse().y) &&
            (event.mouse().button == Mouse::WheelUp || event.mouse().button == Mouse::WheelDown)) {
            selected_ = std::clamp(selected_ + (event.mouse().button == Mouse::WheelUp ? -1 : 1), 0, count_ - 1);
            ChildAt(0)->ActiveChild()->TakeFocus();
            return true;
        }
        return ChildAt(0)->OnEvent(event);
    }

    Box box{};

private:
    int selected_ = 0;
    int count_ = 0;
    bool editing_ = false;
};

struct Draft {
    connect::IrcProfile profile;
    std::string port = "6697";
    std::string channels;
};

struct TestStatus {
    std::mutex mutex;
    std::string message;
};

std::string ParseDraft(const Draft& draft, connect::IrcProfile& profile) {
    profile = draft.profile;
    unsigned port = 0;
    const auto limit = draft.port.data() + draft.port.size();
    const auto parsed = std::from_chars(draft.port.data(), limit, port);
    if (parsed.ec != std::errc{} || parsed.ptr != limit || !port || port > 65535) {
        return "Port must be between 1 and 65535.";
    }
    profile.port = static_cast<std::uint16_t>(port);
    profile.channels.clear();
    for (auto pos = draft.channels.find_first_not_of(" ,\t"); pos != std::string::npos;) {
        const auto end = draft.channels.find_first_of(" ,\t", pos);
        profile.channels.push_back(draft.channels.substr(pos, end - pos));
        pos = draft.channels.find_first_not_of(" ,\t", end);
    }
    return connect::ValidateIrcProfile(profile);
}

} // namespace

class IrcSettingsContent final : public IModalContent {
public:
    IrcSettingsContent(const Theme& theme, connect::IrcProvider& provider, std::filesystem::path path,
                       std::function<void()> close, std::function<void()> redraw, SettingsField::Copy copy)
        : theme_(theme), provider_(provider), path_(std::move(path)), close_(std::move(close)),
          redraw_(std::move(redraw)), form_(Make<SettingsForm>()) {
        auto menu = MenuOption::Vertical();
        menu.on_change = [this] { Select(selection_); };
        menu.entries_option.transform = [this](const EntryState& state) {
            auto row = text(state.label);
            if (state.active) row |= bgcolor(theme_.modal_selected_item_bg) | color(theme_.modal_selected_item_fg);
            return row;
        };
        menu_ = Menu(&labels_, &selection_, std::move(menu));
        Components controls{menu_};
        auto add_field = [&](std::string label, std::string& value, bool secret = false) {
            const auto field = Make<SettingsField>(theme_, value, secret, copy);
            fields_.emplace_back(std::move(label), field);
            controls.push_back(field);
        };
        auto& p = draft_.profile;
        add_field("Connection name", p.name);
        add_field("Server", p.server);
        add_field("Port", draft_.port);
        add_field("Nickname", p.nickname);
        add_field("Username", p.username);
        add_field("SASL account", p.sasl_account);
        add_field("SASL password", p.sasl_password, true);
        add_field("Server password", p.server_password, true);
        add_field("Auto-join channels", draft_.channels);
        for (const auto& [label, value] : std::vector<std::pair<std::string, bool*>>{
                 {"Use TLS", &p.tls}, {"Automatically reconnect", &p.auto_reconnect},
                 {"Start on application launch", &p.start_on_launch}}) {
            auto option = CheckboxOption::Simple();
            option.label = label;
            option.checked = value;
            option.transform = [this](const EntryState& state) {
                auto row = text(std::string(state.state ? "[x] " : "[ ] ") + state.label);
                return state.focused ? row | bgcolor(theme_.modal_selected_item_bg) |
                    color(theme_.modal_selected_item_fg) | focus : row;
            };
            auto check = Checkbox(std::move(option));
            checks_.push_back(check);
            controls.push_back(std::move(check));
        }
        form_->SetFields(std::move(controls));
    }

    void Open() {
        entries_.clear();
        for (const auto& profile : provider_.Profiles()) {
            Draft entry{profile, std::to_string(profile.port), {}};
            for (const auto& channel : profile.channels) {
                if (!entry.channels.empty()) entry.channels += ", ";
                entry.channels += channel;
            }
            entries_.push_back(std::move(entry));
        }
        active_ = -1;
        form_->Reset();
        if (entries_.empty()) New();
        else Select(0);
    }

    void New() {
        Draft entry;
        entry.profile.id = irc::NewProfileId();
        entries_.push_back(std::move(entry));
        Select(static_cast<int>(entries_.size()) - 1);
    }

    void Remove() {
        if (active_ < 0) return;
        const auto index = active_;
        entries_.erase(entries_.begin() + index);
        active_ = -1;
        if (entries_.empty()) {
            labels_.clear();
            test_ = std::make_shared<TestStatus>();
            form_->SetEditing(false);
        } else Select(std::min(index, static_cast<int>(entries_.size()) - 1));
    }

    bool HasProfile() const { return active_ >= 0; }

    void Test() {
        if (!HasProfile()) return;
        connect::IrcProfile profile;
        if (const auto error = ParseDraft(draft_, profile); !error.empty()) return Note(error);
        Note("Testing DNS, transport and IRC registration…");
        const std::weak_ptr<TestStatus> target = test_;
        const auto result = provider_.Test(std::move(profile), [target, redraw = redraw_](std::string message) {
            if (auto status = target.lock()) {
                {
                    std::lock_guard lock(status->mutex);
                    status->message = std::move(message);
                }
                if (redraw) redraw();
            }
        });
        if (!result.accepted) Note(result.reason);
    }

    void Save() {
        StoreDraft();
        std::vector<connect::IrcProfile> profiles;
        for (std::size_t i = 0; i < entries_.size(); ++i) {
            connect::IrcProfile profile;
            if (const auto error = ParseDraft(entries_[i], profile); !error.empty()) {
                Select(static_cast<int>(i));
                return Note(error);
            }
            profiles.push_back(std::move(profile));
        }
        if (!irc::SaveSettings(path_, profiles)) return Note("Could not save IRC settings.");
        provider_.SetProfiles(std::move(profiles));
        close_();
    }

    Component GetMainComponent() override { return form_; }
    std::string GetTitle() override { return "IRC Settings"; }
    bool FooterTextAboveButtons() const override { return true; }
    std::string GetFooterText() const override {
        std::lock_guard lock(test_->mutex);
        if (!test_->message.empty()) return test_->message;
        return HasProfile() ? provider_.Status(draft_.profile.id).diagnostic : "Add an IRC connection.";
    }

    ModalFrameStyle GetModalFrameStyle() const override { return ModalFrameStyle::TitleInBorder; }
    ModalSizePreference GetModalSizePreference() const override {
        const auto terminal = Terminal::Size();
        return {SettingsWidth(), std::min(terminal.dimy, 27)};
    }

    Element Render() override {
        if (HasProfile()) labels_[active_] = draft_.profile.name.empty() ? "Unnamed connection" : draft_.profile.name;
        Elements rows{text("Saved connections") | bold,
            entries_.empty() ? text("No saved IRC connections") | dim
                             : menu_->Render() | yframe | vscroll_indicator | size(HEIGHT, LESS_THAN, 5)};
        if (HasProfile()) {
            const auto diagnostic = GetFooterText();
            if (ftxui::string_width(diagnostic) > SettingsWidth() - 4) {
                rows.push_back(paragraph(diagnostic) | dim);
            }
            rows.push_back(separator() | color(theme_.modal_border));
            for (const auto& [label, field] : fields_) {
                rows.push_back(hbox({text(" " + label) | size(WIDTH, EQUAL, LabelWidth()), field->Render() | flex}));
            }
            for (const auto& check : checks_) rows.push_back(check->Render());
        }
        return vbox(std::move(rows)) | color(theme_.modal_text_color) |
            bgcolor(theme_.modal_background) | reflect(form_->box);
    }

private:
    void StoreDraft() {
        if (HasProfile()) entries_[active_] = draft_;
    }

    void Select(int index) {
        if (index < 0 || index >= static_cast<int>(entries_.size()) || index == active_) return;
        StoreDraft();
        selection_ = active_ = index;
        form_->SetEditing(true);
        draft_ = entries_[index];
        labels_.clear();
        for (const auto& entry : entries_) labels_.push_back(entry.profile.name);
        for (const auto& [label, field] : fields_) field->Reset();
        test_ = std::make_shared<TestStatus>();
    }

    void Note(std::string message) {
        test_ = std::make_shared<TestStatus>();
        test_->message = std::move(message);
    }

    const Theme& theme_;
    connect::IrcProvider& provider_;
    std::filesystem::path path_;
    std::function<void()> close_;
    std::function<void()> redraw_;
    std::shared_ptr<SettingsForm> form_;
    Component menu_;
    Components checks_;
    std::vector<std::pair<std::string, std::shared_ptr<SettingsField>>> fields_;
    Draft draft_;
    std::vector<Draft> entries_;
    std::vector<std::string> labels_;
    int selection_ = 0;
    int active_ = -1;
    std::shared_ptr<TestStatus> test_ = std::make_shared<TestStatus>();
};

void IrcSettingsModal::Configure(const Theme& theme, connect::IrcProvider& provider,
    std::filesystem::path path, std::function<void()> close, std::function<void()> redraw,
    std::function<bool(const std::string&)> write_clipboard) {
    content_ = std::make_shared<IrcSettingsContent>(theme, provider, std::move(path), close,
        std::move(redraw), std::move(write_clipboard));
    modal_ = std::make_shared<ModalWindow>(content_, &theme, close);
    modal_->SetBodyFrameScrolling(true);
    modal_->SetCycleBodyWithFooter(true);
    modal_->SetWrapFooterButtons(true);
    modal_->SetFooterButtons({
        {"New", [content = content_] { content->New(); }, ButtonRole::Utility},
        {"Remove", [content = content_] { content->Remove(); }, ButtonRole::Cancel,
            [content = content_] { return content->HasProfile(); }},
        {"Test Connection", [content = content_] { content->Test(); }, ButtonRole::Utility,
            [content = content_] { return content->HasProfile(); }},
        {"Save", [content = content_] { content->Save(); }, ButtonRole::Primary},
        {"Cancel", std::move(close), ButtonRole::Cancel},
    }, 3);
}

void IrcSettingsModal::Open() {
    content_->Open();
    open_ = true;
    content_->GetMainComponent()->TakeFocus();
}

void IrcSettingsModal::Close() { open_ = false; }
Component IrcSettingsModal::View() const { return modal_; }

} // namespace textlt
