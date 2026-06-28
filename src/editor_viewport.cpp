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
constexpr size_t kFallbackViewportWidth = 80;
constexpr size_t kMaxSafeViewportWidth = 1000;
constexpr size_t kMaxSafeViewportHeight = 300;

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
    const std::string file_path = CurrentFilePath();

    // Use doc_ state
    const size_t total_lines = doc_ ? doc_->lines.size() : 0;
    const bool needs_scrollbar = total_lines > visible_height;

    const size_t slider_height = needs_scrollbar
    ? std::min(visible_height,
               std::max<size_t>(visible_height >= 2 ? 2 : 1,
                                (visible_height * visible_height) / total_lines))
    : visible_height;
    const size_t available_track_space = visible_height > slider_height
    ? visible_height - slider_height
    : 0;
    const size_t slider_top = needs_scrollbar && (total_lines > visible_height)
    ? (scroll_y_ * available_track_space) / (total_lines - visible_height)
    : 0;

    const auto bracket_origin = FindBracketNearCursor();
    const auto bracket_match = FindMatchingBracket();
    auto is_bracket_highlight = [&](size_t x, size_t y) {
        if (!bracket_origin || !bracket_match) return false;
        return (bracket_origin->first == x && bracket_origin->second == y) ||
        (bracket_match->first == x && bracket_match->second == y);
    };

    SyntaxHighlighter::TokenizationContext syntax_context;
    if (syntax_highlighting && doc_) {
        for (size_t line_index = 0; line_index < scroll_y_; ++line_index) {
            SyntaxHighlighter::TokenizeLine(doc_->lines[line_index], file_path, &syntax_context);
        }
    }

    auto render_scrollbar_cell = [&](size_t viewport_y) {
        if (!needs_scrollbar) {
            return ftxui::text(std::string(kScrollbarColumns, ' ')) | ftxui::bgcolor(theme.background);
        }
        if (viewport_y >= slider_top && viewport_y < slider_top + slider_height) {
            return ftxui::text(std::string(kScrollbarColumns, ' ')) |
            ftxui::bgcolor(theme.modal_selected_item_bg) | ftxui::color(theme.modal_selected_item_fg);
        }
        return ftxui::text("│ ") | ftxui::color(theme.gutter) | ftxui::dim;
    };

    auto render_line_segment = [&](size_t line_index, utils::Utf8WrapSegment segment, size_t viewport_y,
                                   const std::vector<SyntaxHighlighter::Token>* syntax_tokens) {
        const std::string& line_content = doc_->lines[line_index];
        ftxui::Element line_number = show_line_numbers
        ? ftxui::text(LineNumberText(line_index, line_number_width)) | ftxui::color(theme.gutter)
        : ftxui::text("");

        ftxui::Elements line_parts{line_number};
        const bool is_cursor_line = (doc_ && line_index == doc_->cursor_row);
        const size_t cursor_x = is_cursor_line ? std::min(doc_->cursor_col, line_content.size()) : line_content.size();
        const size_t render_start = smart_word_wrap ? segment.start : scroll_x_;
        const size_t render_end = smart_word_wrap
        ? segment.end
        : utils::Utf8ByteIndexAtDisplayColumn(line_content, scroll_x_, visible_width);

        size_t syntax_token_index = 0;
        if (syntax_tokens) {
            while (syntax_token_index < syntax_tokens->size() &&
                (*syntax_tokens)[syntax_token_index].start + (*syntax_tokens)[syntax_token_index].length <= render_start) {
                ++syntax_token_index;
                }
        }

        size_t glyph_start = render_start;
        while (glyph_start < render_end &&
               glyph_start < line_content.size() &&
               utils::IsUtf8ContinuationByte(line_content[glyph_start])) {
            ++glyph_start;
        }

        for (size_t x = glyph_start; x < render_end && x < line_content.size();) {
            const size_t glyph_end =
                std::min(utils::NextUtf8CodepointStart(line_content, x), render_end);
            const std::string glyph = line_content.substr(x, glyph_end - x);
            ftxui::Element character = ftxui::text(glyph);
            ftxui::Color text_color = theme.editor_text;

            if (syntax_tokens) {
                while (syntax_token_index + 1 < syntax_tokens->size() &&
                    x >= (*syntax_tokens)[syntax_token_index].start + (*syntax_tokens)[syntax_token_index].length) {
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
                character = character | ftxui::bgcolor(theme.selection_bg) | ftxui::color(theme.selection_fg);
            } else if (search_match) {
                const ftxui::Color match_bg = IsActiveSearchMatch(*search_match) ? theme.active_match_bg : theme.match_bg;
                character = character | ftxui::bgcolor(match_bg) | ftxui::color(theme.selection_fg);
            } else {
                character = character | ftxui::color(text_color);
            }

            if (is_bracket_highlight(x, line_index)) {
                character = character | ftxui::bgcolor(theme.cursor) | ftxui::color(theme.background) | ftxui::bold | ftxui::underlined;
            }

            if (is_cursor_line && x == cursor_x) {
                character = ftxui::text(glyph) | ftxui::bgcolor(theme.selection_bg) |
                ftxui::color(theme.selection_fg) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 1);
            }
            line_parts.push_back(std::move(character));
            x = glyph_end;
        }

        if (is_cursor_line && cursor_x == line_content.size() && cursor_x >= render_start && cursor_x <= render_end && segment.end == line_content.size()) {
            line_parts.push_back(ftxui::text(" ") | ftxui::bgcolor(theme.selection_bg) | ftxui::color(theme.selection_fg) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 1));
        }

        line_parts.push_back(ftxui::filler());
        line_parts.push_back(render_scrollbar_cell(viewport_y));
        return ftxui::hbox(std::move(line_parts));
        };

        if (doc_) {
            for (size_t line_index = scroll_y_; line_index < doc_->lines.size() && lines_elements.size() < visible_height; ++line_index) {
                std::vector<SyntaxHighlighter::Token> syntax_tokens;
                const std::vector<SyntaxHighlighter::Token>* syntax_token_ptr = nullptr;
                if (syntax_highlighting) {
                    syntax_tokens = SyntaxHighlighter::TokenizeLine(doc_->lines[line_index], file_path, &syntax_context);
                    syntax_token_ptr = &syntax_tokens;
                }

                const std::vector<utils::Utf8WrapSegment> segments = smart_word_wrap
                ? utils::BuildUtf8WrapSegments(doc_->lines[line_index], visible_width)
                : std::vector<utils::Utf8WrapSegment>{utils::Utf8WrapSegment{
                    scroll_x_,
                    utils::Utf8ByteIndexAtDisplayColumn(
                        doc_->lines[line_index], scroll_x_, visible_width)}};

                for (const utils::Utf8WrapSegment& segment : segments) {
                    if (lines_elements.size() >= visible_height) break;
                    lines_elements.push_back(render_line_segment(line_index, segment, lines_elements.size(), syntax_token_ptr));
                }
            }
        }

        while (lines_elements.size() < visible_height) {
            const size_t viewport_y = lines_elements.size();
            lines_elements.push_back(ftxui::hbox({
                show_line_numbers ? ftxui::text(std::string(line_number_width, ' ') + " │ ") | ftxui::color(theme.gutter) : ftxui::text(""),
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

    const long long total_height =
        static_cast<long long>(editor_box_.y_max) -
        static_cast<long long>(editor_box_.y_min) + 1;
    if (total_height <= 0) {
        return 1;
    }

    // The viewport box can be uninitialized during early component composition
    // or before a pane has been reflected by FTXUI. Clamp the value so a bad
    // box cannot allocate an enormous number of rendered rows.
    return std::min(static_cast<size_t>(total_height), kMaxSafeViewportHeight);
}

size_t EditorComponent::VisibleTextWidth() const {
    size_t total_width = kFallbackViewportWidth;
    if (editor_box_.x_max >= editor_box_.x_min) {
        const long long measured_width =
            static_cast<long long>(editor_box_.x_max) -
            static_cast<long long>(editor_box_.x_min) + 1;
        if (measured_width > 0 &&
            static_cast<size_t>(measured_width) <= kMaxSafeViewportWidth) {
            total_width = static_cast<size_t>(measured_width);
        }
    }

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
    if (!doc_) return;

    const size_t visible_height = VisibleHeight();
    if (doc_->cursor_row >= scroll_y_ + visible_height) {
        scroll_y_ = doc_->cursor_row - visible_height + 1;
    }
    if (doc_->cursor_row < scroll_y_) {
        scroll_y_ = doc_->cursor_row;
    }

    if (doc_->lines.size() <= visible_height) {
        scroll_y_ = 0;
    } else {
        const size_t max_scroll_y = doc_->lines.size() - visible_height;
        scroll_y_ = std::min(scroll_y_, max_scroll_y);
    }

    const size_t visible_width = VisibleTextWidth();
    if (config_ && config_->smart_word_wrap) {
        scroll_x_ = 0;
        return;
    }

    if (doc_->cursor_col < scroll_x_) {
        scroll_x_ = doc_->cursor_col;
    }

    const std::string& current_line = doc_->lines[doc_->cursor_row];
    while (scroll_x_ < doc_->cursor_col &&
           utils::Utf8DisplayWidth(current_line, scroll_x_, doc_->cursor_col) >= visible_width) {
        scroll_x_ = utils::NextUtf8CodepointStart(current_line, scroll_x_);
    }

    if (utils::Utf8DisplayWidth(current_line) < visible_width) {
        scroll_x_ = 0;
    }
}

} // namespace textlt
