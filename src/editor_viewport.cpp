#include "editor_component.hpp"

#include <algorithm>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "editor_utils.hpp"
#include "syntax_highlighter.hpp"
#include "ftxui/screen/string.hpp"

namespace textlt {

ftxui::Element EditorComponent::RenderViewport() {
    UpdateScroll();

    ftxui::Elements lines_elements;
    const size_t visible_height = VisibleHeight();
    const size_t raw_height = viewport_ ? viewport_->RawHeight() : visible_height;
    const size_t top_padding = viewport_ ? viewport_->ContentTopPadding() : 0;
    const size_t visible_width = VisibleTextWidth();
    const size_t line_number_width = LineNumberWidth();
    const bool show_line_numbers = viewport_ && viewport_->ShouldShowLineNumbers(config_);
    const bool show_scrollbar = viewport_ && viewport_->ShouldShowScrollbar();
    const size_t scrollbar_columns = viewport_ ? viewport_->ScrollbarColumns() : 0;
    const size_t text_left_padding = viewport_ ? viewport_->TextLeftPadding(session_.get(), config_) : 0;
    const bool syntax_highlighting = !config_ || config_->syntax_highlighting;
    const bool smart_word_wrap = config_ && config_->smart_word_wrap;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    const std::string file_path = CurrentFilePath();

    const size_t total_lines = session_ ? session_->lines.size() : 0;
    const size_t effective_total = (smart_word_wrap && session_)
        ? utils::WordWrapTotalVisualRows(session_->lines, visible_width)
        : total_lines;
    const bool needs_scrollbar = show_scrollbar && effective_total > visible_height;

    const size_t slider_height = needs_scrollbar
        ? std::min(visible_height,
                   std::max<size_t>(visible_height >= 2 ? 2 : 1,
                                    (visible_height * visible_height) / effective_total))
        : visible_height;
    const size_t available_track_space = visible_height > slider_height
        ? visible_height - slider_height
        : 0;
    const size_t slider_top = needs_scrollbar && (effective_total > visible_height)
        ? (utils::WordWrapVisualRowAtLine(session_->lines, viewport_->scroll_y, visible_width) * available_track_space) / (effective_total - visible_height)
        : 0;

    const auto bracket_origin = FindBracketNearCursor();
    const auto bracket_match = FindMatchingBracket();
    auto is_bracket_highlight = [&](size_t x, size_t y) {
        if (!bracket_origin || !bracket_match) return false;
        return (bracket_origin->first == x && bracket_origin->second == y) ||
            (bracket_match->first == x && bracket_match->second == y);
    };

    SyntaxHighlighter::TokenizationContext syntax_context;
    if (syntax_highlighting && session_) {
        for (size_t line_index = 0; line_index < viewport_->scroll_y; ++line_index) {
            SyntaxHighlighter::TokenizeLine(session_->lines[line_index], file_path, &syntax_context);
        }
    }

    auto render_scrollbar_cell = [&](size_t viewport_y) {
        if (!show_scrollbar) {
            return ftxui::text("");
        }
        if (!needs_scrollbar) {
            return ftxui::text(std::string(scrollbar_columns, ' ')) | ftxui::bgcolor(theme.background);
        }
        if (viewport_y >= slider_top && viewport_y < slider_top + slider_height) {
            return ftxui::text(std::string(scrollbar_columns, ' ')) |
                ftxui::bgcolor(theme.modal_selected_item_bg) | ftxui::color(theme.modal_selected_item_fg);
        }
        return ftxui::text("│ ") | ftxui::color(theme.gutter) | ftxui::dim;
    };

    auto blank_line_number = [&]() {
        return show_line_numbers
            ? ftxui::text(std::string(line_number_width, ' ') + " │ ") | ftxui::color(theme.gutter)
            : ftxui::text("");
    };

    auto render_empty_row = [&](size_t viewport_y) {
        ftxui::Elements row{blank_line_number()};
        if (text_left_padding > 0) {
            row.push_back(ftxui::text(std::string(text_left_padding, ' ')));
        }
        row.push_back(ftxui::filler());
        row.push_back(render_scrollbar_cell(viewport_y));
        return ftxui::hbox(std::move(row));
    };

    for (size_t i = 0; i < top_padding && lines_elements.size() < raw_height; ++i) {
        lines_elements.push_back(render_empty_row(0));
    }

    auto render_line_segment = [&](size_t line_index, utils::Utf8WrapSegment segment, size_t viewport_y,
                                   const std::vector<SyntaxHighlighter::Token>* syntax_tokens) {
        const std::string& line_content = session_->lines[line_index];
        ftxui::Element line_number = show_line_numbers
            ? ftxui::text(LineNumberText(line_index, line_number_width)) | ftxui::color(theme.gutter)
            : ftxui::text("");

        ftxui::Elements line_parts{line_number};
        if (text_left_padding > 0) {
            line_parts.push_back(ftxui::text(std::string(text_left_padding, ' ')));
        }
        const bool is_cursor_line = (session_ && line_index == session_->CursorRow());
        const bool has_selection = session_ && session_->HasSelection();
        const size_t cursor_x = is_cursor_line ? std::min(session_->CursorCol(), line_content.size()) : line_content.size();
        const size_t render_start = smart_word_wrap ? segment.start : viewport_->scroll_x;
        const size_t render_end = smart_word_wrap
            ? segment.end
            : utils::Utf8ByteIndexAtDisplayColumn(line_content, viewport_->scroll_x, visible_width);

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

            if (!has_selection && is_cursor_line && x == cursor_x) {
                character = ftxui::text(glyph) | ftxui::bgcolor(theme.selection_bg) |
                    ftxui::color(theme.selection_fg) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 1);
            }
            line_parts.push_back(std::move(character));
            x = glyph_end;
        }

        if (!has_selection &&
            is_cursor_line &&
            cursor_x == line_content.size() &&
            cursor_x >= render_start &&
            cursor_x <= render_end &&
            segment.end == line_content.size()) {
            line_parts.push_back(ftxui::text(" ") | ftxui::bgcolor(theme.selection_bg) | ftxui::color(theme.selection_fg) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 1));
        }

        line_parts.push_back(ftxui::filler());
        line_parts.push_back(render_scrollbar_cell(viewport_y));
        return ftxui::hbox(std::move(line_parts));
    };

    size_t rendered_content_rows = 0;
    if (session_) {
        for (size_t line_index = viewport_->scroll_y; line_index < session_->lines.size() && rendered_content_rows < visible_height; ++line_index) {
            std::vector<SyntaxHighlighter::Token> syntax_tokens;
            const std::vector<SyntaxHighlighter::Token>* syntax_token_ptr = nullptr;
            if (syntax_highlighting) {
                syntax_tokens = SyntaxHighlighter::TokenizeLine(session_->lines[line_index], file_path, &syntax_context);
                syntax_token_ptr = &syntax_tokens;
            }

            const std::vector<utils::Utf8WrapSegment> segments = smart_word_wrap
                ? utils::BuildUtf8WrapSegments(session_->lines[line_index], visible_width)
                : std::vector<utils::Utf8WrapSegment>{utils::Utf8WrapSegment{
                    viewport_->scroll_x,
                    utils::Utf8ByteIndexAtDisplayColumn(
                        session_->lines[line_index], viewport_->scroll_x, visible_width)}};

            for (const utils::Utf8WrapSegment& segment : segments) {
                if (rendered_content_rows >= visible_height) break;
                lines_elements.push_back(render_line_segment(line_index, segment, rendered_content_rows, syntax_token_ptr));
                ++rendered_content_rows;
            }
        }
    }

    while (rendered_content_rows < visible_height) {
        lines_elements.push_back(render_empty_row(rendered_content_rows));
        ++rendered_content_rows;
    }

    while (lines_elements.size() < raw_height) {
        lines_elements.push_back(render_empty_row(visible_height > 0 ? visible_height - 1 : 0));
    }

    return ftxui::vbox(std::move(lines_elements)) | ftxui::reflect(viewport_->box);
}

size_t EditorComponent::VisibleHeight() const {
    return viewport_ ? viewport_->VisibleHeight() : 1;
}

size_t EditorComponent::VisibleTextWidth() const {
    return viewport_ ? viewport_->VisibleTextWidth(session_.get(), config_) : 1;
}

void EditorComponent::UpdateScroll() {
    ClampCursorToBuffer();
    if (!session_ || !viewport_) return;
    viewport_->ScrollToCursor(*session_, config_);
}

} // namespace textlt
