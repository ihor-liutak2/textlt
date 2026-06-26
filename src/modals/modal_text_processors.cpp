#include "modal_text_processors.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <utility>

#include "ftxui/component/component_options.hpp"
#include "ftxui/dom/elements.hpp"

namespace textlt {
namespace {

std::string BracketLabel(const std::string& label) {
    return "[" + label + "]";
}

std::string ScopeString(TextParserScope scope) {
    return scope == TextParserScope::Paragraph ? "paragraph" : "text";
}

} // namespace

TextProcessorsModalContent::TextProcessorsModalContent(
    const Theme* theme,
    TargetTextProvider target_text_provider,
    ReplaceTargetCallback replace_target_text,
    CloseCallback on_close)
    : theme_(theme),
      target_text_provider_(std::move(target_text_provider)),
      replace_target_text_(std::move(replace_target_text)),
      on_close_(std::move(on_close)) {
    text_tab_button_ = MakeTextButton("Text", [this] { SetScope(TextParserScope::Text); });
    paragraph_tab_button_ = MakeTextButton("Paragraph", [this] { SetScope(TextParserScope::Paragraph); });

    ftxui::MenuOption parser_option = ftxui::MenuOption::Vertical();
    parser_option.on_change = [this] { SyncParamsFromSelection(); };
    parser_option.entries_option.transform = [this](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        ftxui::Element row = ftxui::text(TrimForDisplay(state.label, 42));
        if (state.focused || state.active) {
            return row |
                ftxui::bgcolor(theme.modal_selected_item_bg) |
                ftxui::color(theme.modal_selected_item_fg) |
                ftxui::bold;
        }
        return row | ftxui::color(theme.modal_text_color);
    };
    parser_menu_ = ftxui::Menu(&parser_labels_, &selected_parser_, parser_option);
    parser_list_component_ = ftxui::CatchEvent(parser_menu_, [this](ftxui::Event event) {
        if (event == ftxui::Event::Return || event.input() == "\x0A") {
            ApplySelected();
            return true;
        }
        return false;
    });

    for (size_t index = 0; index < 4; ++index) {
        ftxui::InputOption option;
        option.multiline = false;
        option.cursor_position = &param_cursors_[index];
        param_inputs_.push_back(ftxui::Input(&param_values_[index], "", option));
    }

    ftxui::InputOption repeat_option;
    repeat_option.multiline = false;
    repeat_option.cursor_position = &repeat_cursor_;
    repeat_option.on_enter = [this] { ApplySelected(); };
    repeat_input_ = ftxui::Input(&repeat_count_, "1", repeat_option);

    ftxui::CheckboxOption checkbox_option = ftxui::CheckboxOption::Simple();
    checkbox_option.transform = [this](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        ftxui::Element item = ftxui::text(
            std::string(state.state ? "[X] " : "[ ] ") + state.label);
        if (state.focused || state.active) {
            return item |
                ftxui::bgcolor(theme.modal_selected_item_bg) |
                ftxui::color(theme.modal_selected_item_fg);
        }
        return item | ftxui::color(theme.modal_text_color);
    };
    whole_document_checkbox_ = ftxui::Checkbox(
        "Whole document", &whole_document_, checkbox_option);

    apply_button_ = MakeTextButton("Apply", [this] { ApplySelected(); });
    reload_button_ = MakeTextButton("Reload", [this] { Reload(); });
    close_button_ = MakeTextButton("Close", [this] {
        if (on_close_) {
            on_close_();
        }
    });

    std::vector<ftxui::Component> controls = {
        text_tab_button_,
        paragraph_tab_button_,
        parser_list_component_,
    };
    controls.insert(controls.end(), param_inputs_.begin(), param_inputs_.end());
    controls.push_back(repeat_input_);
    controls.push_back(whole_document_checkbox_);
    controls.push_back(apply_button_);
    controls.push_back(reload_button_);
    controls.push_back(close_button_);

    container_ = ftxui::Container::Vertical(controls);
}

ftxui::Component TextProcessorsModalContent::MakeTextButton(
    std::string label,
    std::function<void()> on_click) {
    ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
    option.label = std::move(label);
    option.on_click = std::move(on_click);
    option.transform = [this](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        ftxui::Element button = ftxui::text(BracketLabel(state.label));
        if (state.focused || state.active) {
            return button |
                ftxui::bgcolor(theme.modal_selected_item_bg) |
                ftxui::color(theme.modal_selected_item_fg);
        }
        return button | ftxui::color(theme.modal_accent);
    };

    return ftxui::Button(option);
}

void TextProcessorsModalContent::Open() {
    Reload();
    if (parser_list_component_) {
        parser_list_component_->TakeFocus();
    }
}

void TextProcessorsModalContent::Close() {
    status_ = "Select a text processor.";
}

void TextProcessorsModalContent::Reload() {
    std::string error;
    const std::filesystem::path default_processors_directory =
        std::filesystem::current_path() / "text_processors";

    if (!manager_.EnsureUserConfiguration(default_processors_directory, error)) {
        status_ = error;
        filtered_parsers_.clear();
        parser_labels_.clear();
        return;
    }

    if (!manager_.LoadFromUserConfiguration(error)) {
        status_ = error;
        filtered_parsers_.clear();
        parser_labels_.clear();
        return;
    }

    RebuildParserList();
    status_ = "Loaded " + std::to_string(manager_.GetParsers().size()) +
        " text processors from user configuration.";
}

void TextProcessorsModalContent::ApplySelected() {
    const TextParserDefinition* parser = SelectedParser();
    if (parser == nullptr) {
        status_ = "No text processor selected.";
        return;
    }

    std::string input_text;
    std::string error;
    if (!target_text_provider_ ||
        !target_text_provider_(whole_document_, input_text, error)) {
        status_ = error.empty() ? "Cannot read target text." : error;
        return;
    }

    const TextParserApplyResult result = manager_.ApplyParser(
        parser->id,
        input_text,
        CurrentParams(),
        RepeatCount());
    if (!result.success) {
        status_ = result.error.empty() ? "Text processor failed." : result.error;
        return;
    }

    if (!replace_target_text_ ||
        !replace_target_text_(whole_document_, result.text, error)) {
        status_ = error.empty() ? "Cannot replace target text." : error;
        return;
    }

    status_ = "Applied " + parser->name + " to " +
        (whole_document_ ? "whole document." : "selected text.");
}

bool TextProcessorsModalContent::HandleEvent(ftxui::Event event) {
    if (parser_list_component_ && parser_list_component_->Focused()) {
        if (event.input() == "r" || event.input() == "R") {
            Reload();
            return true;
        }

        if (event == ftxui::Event::Return || event.input() == "\x0A") {
            ApplySelected();
            return true;
        }
    }

    return false;
}

void TextProcessorsModalContent::SetScope(TextParserScope scope) {
    if (active_scope_ == scope) {
        return;
    }

    active_scope_ = scope;
    selected_parser_ = 0;
    RebuildParserList();
    status_ = "Showing " + ScopeLabel(scope) + " processors.";
}

TextParserScope TextProcessorsModalContent::CurrentScope() const {
    return active_scope_;
}

void TextProcessorsModalContent::RebuildParserList() {
    filtered_parsers_.clear();
    parser_labels_.clear();

    for (const TextParserDefinition& parser : manager_.GetParsers()) {
        if (parser.scope != active_scope_) {
            continue;
        }
        filtered_parsers_.push_back(&parser);
        parser_labels_.push_back(parser.name);
    }

    if (filtered_parsers_.empty()) {
        parser_labels_.push_back("No processors for " + ScopeLabel(active_scope_));
    }

    selected_parser_ = std::clamp(
        selected_parser_,
        0,
        std::max(0, static_cast<int>(parser_labels_.size()) - 1));
    SyncParamsFromSelection();
}

void TextProcessorsModalContent::SyncParamsFromSelection() {
    const TextParserDefinition* parser = SelectedParser();
    for (size_t index = 0; index < param_values_.size(); ++index) {
        if (parser && index < parser->params.size()) {
            param_values_[index] = parser->params[index].default_value;
        } else {
            param_values_[index].clear();
        }
        param_cursors_[index] = static_cast<int>(param_values_[index].size());
    }

    if (parser) {
        repeat_count_ = std::to_string(std::max(1, parser->repeat_default));
    } else {
        repeat_count_ = "1";
    }
    repeat_cursor_ = static_cast<int>(repeat_count_.size());
}

const TextParserDefinition* TextProcessorsModalContent::SelectedParser() const {
    if (filtered_parsers_.empty()) {
        return nullptr;
    }
    if (selected_parser_ < 0 ||
        selected_parser_ >= static_cast<int>(filtered_parsers_.size())) {
        return nullptr;
    }
    return filtered_parsers_[static_cast<size_t>(selected_parser_)];
}

std::unordered_map<std::string, std::string> TextProcessorsModalContent::CurrentParams() const {
    std::unordered_map<std::string, std::string> params;
    const TextParserDefinition* parser = SelectedParser();
    if (!parser) {
        return params;
    }

    for (size_t index = 0; index < parser->params.size() && index < param_values_.size(); ++index) {
        params[parser->params[index].id] = param_values_[index];
    }
    return params;
}

int TextProcessorsModalContent::RepeatCount() const {
    try {
        size_t parsed = 0;
        const int value = std::stoi(repeat_count_, &parsed);
        if (parsed != repeat_count_.size()) {
            return 1;
        }
        return std::clamp(value, 1, 50);
    } catch (...) {
        return 1;
    }
}

ftxui::Element TextProcessorsModalContent::RenderTitle() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    return hbox({
        text(" Text Processors ") | bold | color(theme.modal_accent),
        text(" "),
        text("Lua scripts from ~/.config/textlt/text_parsers.json") |
            dim |
            color(theme.modal_text_color),
    });
}

ftxui::Element TextProcessorsModalContent::Render() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    return vbox({
        RenderScopeTabs(),
        separator() | color(theme.modal_border),
        hbox({
            vbox({
                text("Processors") | bold | color(theme.modal_accent),
                RenderParserList() | flex,
            }) | size(WIDTH, EQUAL, 34),
            separator() | color(theme.modal_border),
            vbox({
                RenderSelectedParserInfo(),
                separator() | color(theme.modal_border),
                RenderParameterFields(),
                separator() | color(theme.modal_border),
                RenderActionRow(),
                RenderHelpLine(),
            }) | flex,
        }) | flex,
    }) | bgcolor(theme.modal_background);
}

ftxui::Element TextProcessorsModalContent::RenderScopeTabs() const {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    return hbox({
        text(" Scope: ") | bold | color(theme.modal_accent),
        active_scope_ == TextParserScope::Text
            ? text("[Text]") | bgcolor(theme.modal_selected_item_bg) | color(theme.modal_selected_item_fg)
            : text_tab_button_->Render(),
        text(" "),
        active_scope_ == TextParserScope::Paragraph
            ? text("[Paragraph]") | bgcolor(theme.modal_selected_item_bg) | color(theme.modal_selected_item_fg)
            : paragraph_tab_button_->Render(),
        filler(),
        text(" Target: ") | bold | color(theme.modal_accent),
        whole_document_checkbox_->Render(),
    });
}

ftxui::Element TextProcessorsModalContent::RenderParserList() const {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    if (parser_labels_.empty()) {
        return text("No processors loaded.") | color(theme.modal_text_color) | frame;
    }
    return parser_list_component_->Render() | vscroll_indicator | frame;
}

ftxui::Element TextProcessorsModalContent::RenderSelectedParserInfo() const {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    const TextParserDefinition* parser = SelectedParser();
    if (!parser) {
        return vbox({
            text("No processor selected.") | color(theme.modal_text_color),
        });
    }

    return vbox({
        hbox({
            text("Name: ") | bold | color(theme.modal_accent),
            text(TrimForDisplay(parser->name, 44)) | color(theme.modal_text_color),
        }),
        hbox({
            text("Scope: ") | bold | color(theme.modal_accent),
            text(ScopeString(parser->scope)) | color(theme.modal_text_color),
            text("   Script: ") | bold | color(theme.modal_accent),
            text(TrimForDisplay(parser->script_path.generic_string(), 32)) |
                color(theme.modal_text_color),
        }),
        text(TrimForDisplay(parser->description, 82)) |
            dim |
            color(theme.modal_text_color),
    });
}

ftxui::Element TextProcessorsModalContent::RenderParameterFields() const {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    const TextParserDefinition* parser = SelectedParser();
    Elements rows;

    rows.push_back(hbox({
        text("Parameters") | bold | color(theme.modal_accent),
        filler(),
        text("Repeat: ") | color(theme.modal_text_color),
        repeat_input_->Render() | size(WIDTH, EQUAL, 5),
    }));

    if (!parser || parser->params.empty()) {
        rows.push_back(text("No parameters for this processor.") |
            dim |
            color(theme.modal_text_color));
        return vbox(rows);
    }

    for (size_t index = 0; index < parser->params.size() && index < param_inputs_.size(); ++index) {
        const TextParserParam& param = parser->params[index];
        rows.push_back(hbox({
            text(" " + TrimForDisplay(param.label, 18) + ": ") |
                color(theme.modal_text_color) |
                size(WIDTH, EQUAL, 22),
            param_inputs_[index]->Render() |
                size(WIDTH, GREATER_THAN, 24) |
                xflex,
            text("  ") | color(theme.modal_text_color),
            text(param.type.empty() ? "string" : param.type) |
                dim |
                color(theme.modal_text_color),
        }));
    }

    return vbox(rows);
}

ftxui::Element TextProcessorsModalContent::RenderActionRow() const {
    using namespace ftxui;
    return hbox({
        apply_button_->Render(),
        text(" "),
        reload_button_->Render(),
        text(" "),
        close_button_->Render(),
        filler(),
    });
}

ftxui::Element TextProcessorsModalContent::RenderHelpLine() const {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    return hbox({
        text(" Selection target uses selected text. Whole document replaces all text. ") |
            dim |
            color(theme.modal_text_color),
        text(" R: reload ") |
            dim |
            color(theme.modal_text_color),
        text(" Esc: close ") |
            dim |
            color(theme.modal_text_color),
    });
}

std::string TextProcessorsModalContent::TrimForDisplay(
    const std::string& text,
    size_t max_size) const {
    if (text.size() <= max_size) {
        return text;
    }
    if (max_size <= 3) {
        return text.substr(0, max_size);
    }
    return text.substr(0, max_size - 3) + "...";
}

std::string TextProcessorsModalContent::ScopeLabel(TextParserScope scope) const {
    return scope == TextParserScope::Paragraph ? "paragraph" : "text";
}

TextProcessorsModal::TextProcessorsModal(
    const Theme* theme,
    TargetTextProvider target_text_provider,
    ReplaceTargetCallback replace_target_text)
    : theme_(theme) {
    content_ = std::make_shared<TextProcessorsModalContent>(
        theme_,
        std::move(target_text_provider),
        std::move(replace_target_text),
        [this] { Close(); });

    modal_ = std::make_shared<ModalWindow>(content_, theme_, [this] { Close(); });
    modal_->SetFooterButtons({
        {"Apply", [this] {
            if (content_) {
                content_->ApplySelected();
            }
        }},
        {"Reload", [this] {
            if (content_) {
                content_->Reload();
            }
        }},
        {"Close", [this] { Close(); }},
    });
    modal_->SetBodyFrameScrolling(false);
}

ftxui::Component TextProcessorsModal::View() const {
    return modal_;
}

void TextProcessorsModal::Open() {
    open_ = true;
    if (content_) {
        content_->SetTheme(theme_);
        content_->Open();
        content_->GetMainComponent()->TakeFocus();
    }
    if (modal_) {
        modal_->SetTheme(theme_);
    }
}

void TextProcessorsModal::Close() {
    open_ = false;
    if (content_) {
        content_->Close();
    }
}

bool TextProcessorsModal::IsOpen() const {
    return open_;
}

bool TextProcessorsModal::OnEvent(ftxui::Event event) {
    if (!open_ || !modal_) {
        return false;
    }

    if (event == ftxui::Event::Escape) {
        Close();
        return true;
    }

    if (content_ && content_->HandleEvent(event)) {
        return true;
    }

    return modal_->OnEvent(std::move(event));
}

} // namespace textlt
