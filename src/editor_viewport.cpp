#include "editor_component.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "editor_utils.hpp"
#include "syntax_highlighter.hpp"

namespace textlt {
namespace {

constexpr size_t kScrollbarColumns = 2;

struct WrapSegment {
    size_t start = 0;
    size_t end = 0;
};

bool IsSoftWrapBoundary(const std::string& line, size_t position) {
    if (position == 0 || position >= line.size()) {
        return true;
    }

    const char left = line[position - 1];
    const char right = line[position];
    return std::isspace(static_cast<unsigned char>(left)) ||
        std::isspace(static_cast<unsigned char>(right)) ||
        !(utils::IsWordCharacter(left) && utils::IsWordCharacter(right));
}

size_t FindSmartWrapEnd(const std::string& line, size_t start, size_t width) {
    const size_t hard_end = std::min(line.size(), start + width);
    if (hard_end >= line.size()) {
        return line.size();
    }

    // Prefer whitespace breaks first because they keep words and identifiers
    // visually intact without introducing leading punctuation on the next row.
    for (size_t position = hard_end; position > start + 1; --position) {
        if (std::isspace(static_cast<unsigned char>(line[position - 1]))) {
            return position;
        }
    }

    // Fall back to non-word boundaries, such as operators or punctuation. This
    // prevents common identifiers and keywords from being split mid-token.
    for (size_t position = hard_end; position > start + 1; --position) {
        if (IsSoftWrapBoundary(line, position)) {
            return position;
        }
    }

    // Very long standalone tokens must still make progress, so the viewport
    // hard-wraps only when no safe boundary exists inside the visible width.
    return hard_end;
}

std::vector<WrapSegment> BuildWrapSegments(const std::string& line, size_t width) {
    std::vector<WrapSegment> segments;
    width = std::max<size_t>(1, width);

    if (line.empty()) {
        segments.push_back({});
        return segments;
    }

    size_t start = 0;
    while (start < line.size()) {
        size_t end = FindSmartWrapEnd(line, start, width);
        if (end <= start) {
            end = std::min(line.size(), start + width);
        }
        segments.push_back({start, end});
        start = end;
    }

    return segments;
}

} // namespace

ftxui::Element EditorComponent::RenderViewport() {
    UpdateScroll();

    ftxui::Elements lines_elements;
    const size_t visible_height = VisibleHeight();
    const size_t visible_width = VisibleTextWidth();
    const size_t line_number_width = LineNumberWidth();
    const bool show_line_numbers = config_ && config_->show_line_numbers;
    const bool syntax_highlighting = !config_ || config_->syntax_highlighting;
    const bool smart_word_wrap = config_ && config_->smart_word_wrap;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    const std::string file_path = current_filepath_;
    const size_t total_lines = text_lines_.size();
    const bool needs_scrollbar = total_lines > visible_height;
    const size_t slider_height = needs_scrollbar
        ? std::min(
              visible_height,
              std::max<size_t>(
                  visible_height >= 2 ? 2 : 1,
                  (visible_height * visible_height) / total_lines))
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
            return ftxui::text(std::string(kScrollbarColumns, ' ')) |
                ftxui::bgcolor(theme.background);
        }

        if (viewport_y >= slider_top && viewport_y < slider_top + slider_height) {
            return ftxui::text(std::string(kScrollbarColumns, ' ')) |
                ftxui::bgcolor(theme.modal_selected_item_bg) |
                ftxui::color(theme.modal_selected_item_fg);
        }
        return ftxui::text("│ ") | ftxui::color(theme.gutter) | ftxui::dim;
    };

    auto render_line_segment = [&](
        size_t line_index,
        WrapSegment segment,
        size_t viewport_y,
        const std::vector<SyntaxHighlighter::Token>* syntax_tokens) {
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
        const size_t render_start = smart_word_wrap ? segment.start : scroll_x_;
        const size_t render_end = smart_word_wrap
            ? segment.end
            : std::min(line_content.size(), scroll_x_ + visible_width);

        size_t syntax_token_index = 0;
        if (syntax_tokens) {
            while (syntax_token_index < syntax_tokens->size() &&
                   (*syntax_tokens)[syntax_token_index].start +
                       (*syntax_tokens)[syntax_token_index].length <= render_start) {
                ++syntax_token_index;
            }
        }

        // Render a visual segment while preserving raw document coordinates.
        // Every style query still receives the original line index and raw
        // character offset, so cursor, selection, search and bracket highlights
        // remain aligned with the underlying text buffer.
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
            cursor_x <= render_end &&
            segment.end == line_content.size()) {
            line_parts.push_back(ftxui::text(" ") | ftxui::inverted);
        }

        line_parts.push_back(ftxui::filler());
        line_parts.push_back(render_scrollbar_cell(viewport_y));

        return ftxui::hbox(std::move(line_parts));
    };

    // Render visible raw lines. When Smart Word Wrap is enabled, each raw line
    // is split into visual sub-rows that fit the viewport width while retaining
    // the same backing document line and character coordinates.
    for (size_t line_index = scroll_y_;
         line_index < text_lines_.size() && lines_elements.size() < visible_height;
         ++line_index) {
        std::vector<SyntaxHighlighter::Token> syntax_tokens;
        const std::vector<SyntaxHighlighter::Token>* syntax_token_ptr = nullptr;
        if (syntax_highlighting) {
            syntax_tokens =
                SyntaxHighlighter::TokenizeLine(text_lines_[line_index], file_path, &syntax_context);
            syntax_token_ptr = &syntax_tokens;
        }

        const std::vector<WrapSegment> segments = smart_word_wrap
            ? BuildWrapSegments(text_lines_[line_index], visible_width)
            : std::vector<WrapSegment>{{scroll_x_, std::min(
                  text_lines_[line_index].size(), scroll_x_ + visible_width)}};

        for (const WrapSegment& segment : segments) {
            if (lines_elements.size() >= visible_height) {
                break;
            }
            lines_elements.push_back(
                render_line_segment(line_index, segment, lines_elements.size(), syntax_token_ptr));
        }
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
    if (config_ && config_->smart_word_wrap) {
        scroll_x_ = 0;
        return;
    }

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
