#include "modal_text_processors.hpp"

#include "app_resources.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <sstream>
#include <string>
#include <utility>

#include "ftxui/component/component_options.hpp"
#include "ftxui/dom/elements.hpp"

#include "ui_button.hpp"

namespace textlt {
namespace {

constexpr int kProcessorListVisibleRows = 16;
const std::vector<std::string> kProcessorGroups = {
    "All",
    "Cleanup",
    "Lines",
    "Paragraphs",
    "Case",
    "Code",
    "Punctuation",
    "Tables",
    "Markdown",
    "Analysis",
    "User",
    "Custom",
};
ButtonSpec ProcessorTabSpec(std::string label, bool selected = false) {
    ButtonSpec spec;
    spec.caption = std::move(label);
    spec.role = ButtonRole::Tab;
    spec.variant = ButtonVariant::AccentBar;
    spec.size = ButtonSize::Compact;
    spec.selected = selected;
    return spec;
}

bool IsBackspaceEvent(const ftxui::Event& event) {
    return event == ftxui::Event::Backspace ||
           event.input() == "\x7F" ||
           event.input() == "\x08";
}

bool IsIntegerText(const std::string& value) {
    if (value.empty()) {
        return false;
    }

    size_t index = 0;
    if (value[0] == '-' || value[0] == '+') {
        index = 1;
    }
    if (index >= value.size()) {
        return false;
    }

    return std::all_of(value.begin() + static_cast<std::ptrdiff_t>(index), value.end(),
        [](unsigned char ch) { return std::isdigit(ch) != 0; });
}

bool IsDecimalText(const std::string& value, char decimal_separator) {
    if (value.empty()) {
        return false;
    }

    bool has_digit = false;
    bool has_separator = false;
    for (size_t index = 0; index < value.size(); ++index) {
        const unsigned char ch = static_cast<unsigned char>(value[index]);
        if (index == 0 && (ch == '-' || ch == '+')) {
            continue;
        }
        if (std::isdigit(ch) != 0) {
            has_digit = true;
            continue;
        }
        if (static_cast<char>(ch) == decimal_separator && !has_separator) {
            has_separator = true;
            continue;
        }
        return false;
    }

    return has_digit;
}

std::string NormalizeDecimalText(std::string value, char decimal_separator) {
    if (decimal_separator != '.') {
        std::replace(value.begin(), value.end(), decimal_separator, '.');
    }
    return value;
}

std::string ToLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
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
    code_tab_button_ = MakeTextButton("Code", [this] { SetScope(TextParserScope::Code); });

    for (const std::string& group : kProcessorGroups) {
        group_tab_buttons_.push_back(MakeTextButton(group, [this, group] { SetGroup(group); }));
    }

    processor_grid_component_ = ftxui::CatchEvent(
        ftxui::Renderer([this] {
            return RenderProcessorGrid();
        }),
        [this](ftxui::Event event) {
            if (event.is_mouse()) {
                const ftxui::Mouse& mouse = event.mouse();
                if (mouse.button == ftxui::Mouse::WheelDown) {
                    MoveParserSelection(3);
                    return true;
                }
                if (mouse.button == ftxui::Mouse::WheelUp) {
                    MoveParserSelection(-3);
                    return true;
                }
                if (mouse.button == ftxui::Mouse::Left &&
                    mouse.motion == ftxui::Mouse::Pressed) {
                    for (int index = 0; index < processor_cell_count_; ++index) {
                        if (processor_cell_indices_[index] >= 0 &&
                            processor_cell_boxes_[index].Contain(mouse.x, mouse.y)) {
                            MoveParserSelectionToIndex(processor_cell_indices_[index]);
                            return true;
                        }
                    }
                }
            }
            return false;
        });

    for (size_t index = 0; index < 4; ++index) {
        ftxui::InputOption option;
        option.multiline = false;
        option.cursor_position = &param_cursors_[index];
        option.transform = [this](ftxui::InputState state) {
            const Theme& theme = theme_ ? *theme_ : FallbackTheme();
            return theme.InputTransform(std::move(state));
        };
        param_inputs_.push_back(ftxui::Input(&param_values_[index], "", option));
    }

    ftxui::InputOption repeat_option;
    repeat_option.multiline = false;
    repeat_option.cursor_position = &repeat_cursor_;
    repeat_option.on_enter = [this] { ApplySelected(); };
    repeat_option.transform = [this](ftxui::InputState state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        return theme.InputTransform(std::move(state));
    };
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

    std::vector<ftxui::Component> controls = {
        text_tab_button_,
        paragraph_tab_button_,
        code_tab_button_,
    };
    controls.insert(controls.end(), group_tab_buttons_.begin(), group_tab_buttons_.end());
    controls.push_back(repeat_input_);
    controls.insert(controls.end(), {
        whole_document_checkbox_,
        processor_grid_component_,
    });
    controls.insert(controls.end(), param_inputs_.begin(), param_inputs_.end());

    container_ = ftxui::Container::Vertical(controls);
}

ftxui::Component TextProcessorsModalContent::MakeTextButton(
    std::string label,
    std::function<void()> on_click) {
    ButtonSpec spec = ProcessorTabSpec(std::move(label));
    ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
    option.label = ButtonCaptionText(spec);
    option.on_click = std::move(on_click);
    option.transform = [this, spec = std::move(spec)](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        return RenderButton(theme, spec, state.focused || state.active);
    };

    return ftxui::Button(option);
}

#include "modal_text_processors/behavior.cpp"
#include "modal_text_processors/render.cpp"
#include "modal_text_processors/display_utils.cpp"
#include "modal_text_processors/wrapper.cpp"
