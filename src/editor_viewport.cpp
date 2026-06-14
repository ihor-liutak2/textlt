#include "editor_component.hpp"

#include <algorithm>
#include <utility>

#include "syntax_highlighter.hpp"

namespace textlt {

ftxui::Element EditorComponent::RenderViewport() {
    UpdateScroll();

    ftxui::Elements lines_elements;
    const size_t visible_height = VisibleHeight();
    const size_t visible_width = VisibleTextWidth();
    const size_t last_visible_line =
        std::min(text_lines_.size(), scroll_y_ + visible_height);
    const size_t line_number_width = LineNumberWidth();
    const bool show_line_numbers = config_ && config_->show_line_numbers;
    const bool syntax_highlighting = !config_ || config_->syntax_highlighting;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    const std::string file_path = current_filepath_;
    const size_t total_lines = text_lines_.size();
    const bool needs_scrollbar = total_lines > visible_height;
    const size_t slider_height = needs_scrollbar
        ? std::max<size_t>(1, (visible_height * visible_height) / total_lines)
        : visible_height;
    const size_t available_track_space = visible_height > slider_height
        ? visible_height - slider_height
        : 0;
    const size_t slider_top = needs_scrollbar
        ? (scroll_y_ * available_track_space) / (total_lines - visible_height)
        : 0;
    const auto bracket_origin = FindBracketNearCursor();
    const auto bracket_match = FindMatchingBracket();
    auto is_bracket_highlight = [&](size_t x, size_t y) {
        if (!bracket_origin || !bracket_match) {
            return false;
        }
        return (bracket_origin->first == static_cast<int>(x) &&
                bracket_origin->second == static_cast<int>(y)) ||
            (bracket_match->first == static_cast<int>(x) &&
             bracket_match->second == static_cast<int>(y));
    };
    SyntaxHighlighter::TokenizationContext syntax_context;
    if (syntax_highlighting) {
        for (size_t line_index = 0; line_index < scroll_y_; ++line_index) {
            SyntaxHighlighter::TokenizeLine(text_lines_[line_index], file_path, &syntax_context);
        }
    }

    auto render_scrollbar_cell = [&](size_t viewport_y) {
        if (!needs_scrollbar) {
            return ftxui::text(" ") | ftxui::bgcolor(theme.background);
        }

        if (viewport_y >= slider_top && viewport_y < slider_top + slider_height) {
            return ftxui::text("█") | ftxui::color(theme.cursor);
        }
        return ftxui::text("│") | ftxui::color(theme.gutter) | ftxui::dim;
    };

    auto render_line = [&](size_t line_index, size_t viewport_y) {
        const std::string& line_content = text_lines_[line_index];

        ftxui::Element line_number = show_line_numbers
            ? ftxui::text(LineNumberText(line_index, line_number_width)) |
                ftxui::color(theme.gutter)
            : ftxui::text("");

        ftxui::Elements line_parts{line_number};
        const bool is_cursor_line = line_index == cursor_y_;
        const size_t cursor_x = is_cursor_line
            ? std::min(cursor_x_, line_content.size())
            : line_content.size();
        const size_t render_start = scroll_x_;
        const size_t render_end = scroll_x_ + visible_width;

        auto render_visible_text = [&](const std::vector<SyntaxHighlighter::Token>* syntax_tokens) {
            size_t syntax_token_index = 0;
            if (syntax_tokens) {
                while (syntax_token_index < syntax_tokens->size() &&
                       (*syntax_tokens)[syntax_token_index].start +
                           (*syntax_tokens)[syntax_token_index].length <= render_start) {
                    ++syntax_token_index;
                }
            }

            for (size_t x = render_start; x < render_end && x < line_content.size(); ++x) {
                ftxui::Element character = ftxui::text(line_content.substr(x, 1));
                ftxui::Color text_color = theme.editor_text;
                if (syntax_tokens) {
                    while (syntax_token_index + 1 < syntax_tokens->size() &&
                           x >= (*syntax_tokens)[syntax_token_index].start +
                               (*syntax_tokens)[syntax_token_index].length) {
                        ++syntax_token_index;
                    }
                    if (syntax_token_index < syntax_tokens->size()) {
                        const SyntaxHighlighter::Token& token = (*syntax_tokens)[syntax_token_index];
                        if (x >= token.start && x < token.start + token.length) {
                            text_color = SyntaxHighlighter::ColorForStyle(token.style, theme);
                        }
                    }
                }

                const SearchMatch* search_match = SearchMatchAt(x, line_index);
                if (IsCharacterSelected(x, line_index)) {
                    character = character |
                        ftxui::bgcolor(theme.selection_bg) |
                        ftxui::color(theme.selection_fg);
                } else if (search_match) {
                    const ftxui::Color match_bg =
                        IsActiveSearchMatch(*search_match)
                            ? theme.active_match_bg
                            : theme.match_bg;
                    character = character |
                        ftxui::bgcolor(match_bg) |
                        ftxui::color(theme.selection_fg);
                } else {
                    character = character | ftxui::color(text_color);
                }

                if (is_bracket_highlight(x, line_index)) {
                    character = character |
                        ftxui::bgcolor(theme.cursor) |
                        ftxui::color(theme.background) |
                        ftxui::bold |
                        ftxui::underlined;
                }

                if (is_cursor_line && x == cursor_x) {
                    character = character | ftxui::inverted;
                }
                line_parts.push_back(std::move(character));
            }

            if (is_cursor_line &&
                cursor_x == line_content.size() &&
                cursor_x >= render_start &&
                cursor_x < render_end) {
                line_parts.push_back(ftxui::text(" ") | ftxui::inverted);
            }
        };

        if (syntax_highlighting) {
            const std::vector<SyntaxHighlighter::Token> syntax_tokens =
                SyntaxHighlighter::TokenizeLine(line_content, file_path, &syntax_context);
            render_visible_text(&syntax_tokens);
        } else {
            // Standard raw rendering branch used when syntax highlighting is disabled.
            render_visible_text(nullptr);
        }

        line_parts.push_back(ftxui::filler());
        line_parts.push_back(render_scrollbar_cell(viewport_y));

        return ftxui::hbox(std::move(line_parts));
    };

    // Render only the visible part of the buffer.
    for (size_t i = scroll_y_; i < last_visible_line; ++i) {
        lines_elements.push_back(render_line(i, lines_elements.size()));
    }

    while (lines_elements.size() < visible_height) {
        const size_t viewport_y = lines_elements.size();
        lines_elements.push_back(ftxui::hbox({
            show_line_numbers
                ? ftxui::text(std::string(line_number_width, ' ') + " │ ") |
                    ftxui::color(theme.gutter)
                : ftxui::text(""),
            ftxui::filler(),
            render_scrollbar_cell(viewport_y),
        }));
    }

    return ftxui::vbox(std::move(lines_elements)) | ftxui::reflect(editor_box_);
}

size_t EditorComponent::VisibleHeight() const {
    if (editor_box_.y_max < editor_box_.y_min) {
        return 1;
    }

    const size_t total_height =
        static_cast<size_t>(editor_box_.y_max - editor_box_.y_min + 1);
    if (bottom_overlay_rows_ >= total_height) {
        return 1;
    }

    return total_height - bottom_overlay_rows_;
}

size_t EditorComponent::VisibleTextWidth() const {
    static constexpr size_t kScrollbarColumns = 1;
    const size_t total_width = editor_box_.x_max >= editor_box_.x_min
        ? static_cast<size_t>(editor_box_.x_max - editor_box_.x_min + 1)
        : 80;
    const bool show_line_numbers = config_ && config_->show_line_numbers;
    const size_t line_number_columns =
        show_line_numbers ? LineNumberText(0, LineNumberWidth()).size() : 0;
    if (line_number_columns + kScrollbarColumns >= total_width) {
        return 1;
    }
    return total_width - line_number_columns - kScrollbarColumns;
}

void EditorComponent::UpdateScroll() {
    ClampCursorToBuffer();
    const size_t visible_height = VisibleHeight();
    if (cursor_y_ >= scroll_y_ + visible_height) {
        scroll_y_ = cursor_y_ - visible_height + 1;
    }
    if (cursor_y_ < scroll_y_) {
        scroll_y_ = cursor_y_;
    }
    if (text_lines_.size() <= visible_height) {
        scroll_y_ = 0;
    } else {
        const size_t max_scroll_y = text_lines_.size() - visible_height;
        scroll_y_ = std::min(scroll_y_, max_scroll_y);
    }

    const size_t visible_width = VisibleTextWidth();
    if (cursor_x_ >= scroll_x_ + visible_width) {
        scroll_x_ = cursor_x_ - visible_width + 1;
    }
    if (cursor_x_ < scroll_x_) {
        scroll_x_ = cursor_x_;
    }

    const size_t current_line_size = text_lines_[cursor_y_].size();
    if (current_line_size < visible_width) {
        scroll_x_ = 0;
    } else {
        const size_t max_scroll_x = current_line_size - visible_width + 1;
        scroll_x_ = std::min(scroll_x_, max_scroll_x);
    }
}

} // namespace textlt
