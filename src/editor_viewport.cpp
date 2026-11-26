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
    const bool smart_word_wrap = (viewport_ && viewport_->IsDistractionMode()) ||
        (config_ && config_->smart_word_wrap);
    const auto horizontal = session_ ? viewport_->HorizontalScrollMetrics(*session_, config_)
                                     : HorizontalScrollbar{};
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    const std::string file_path = session_ && !session_->lexer_path.empty()
        ? session_->lexer_path : CurrentFilePath();
    const bool highlight_diff_navigation = session_ && session_->git_compare && session_->git_compare_has_navigation;

    const size_t total_lines = session_ ? session_->lines.size() : 0;
    const size_t effective_total = (smart_word_wrap && session_ && viewport_)
        ? viewport_->EstimatedWrappedTotalRows(*session_, config_)
        : total_lines;
    const bool needs_scrollbar = show_scrollbar &&
        effective_total > visible_height;

    const size_t slider_height = needs_scrollbar
        ? std::min(visible_height,
                   std::max<size_t>(visible_height >= 2 ? 2 : 1,
                                    (visible_height * visible_height) / effective_total))
        : visible_height;
    const size_t available_track_space = visible_height > slider_height
        ? visible_height - slider_height
        : 0;
    const size_t scrollbar_visual_position = needs_scrollbar && (effective_total > visible_height)
        ? std::min(
            (smart_word_wrap && session_ && viewport_)
                ? viewport_->EstimatedWrappedVisualRowAtLine(*session_, config_, viewport_->scroll_y)
                : viewport_->scroll_y,
            effective_total - visible_height)
        : 0;
    const size_t slider_top = needs_scrollbar && (effective_total > visible_height)
        ? (scrollbar_visual_position * available_track_space) / (effective_total - visible_height)
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

        ftxui::Color diff_background = theme.background;
        if (session_ && session_->git_compare && line_index < session_->diff_line_kinds.size()) {
            switch (session_->diff_line_kinds[line_index]) {
                case DiffLineKind::Added:
                    diff_background = theme.IsLight()
                        ? (ftxui::Color::RGB)(210, 246, 218)
                        : (ftxui::Color::RGB)(35, 76, 48);
                    break;
                case DiffLineKind::Deleted:
                    diff_background = theme.IsLight()
                        ? (ftxui::Color::RGB)(255, 220, 220)
                        : (ftxui::Color::RGB)(91, 42, 46);
                    break;
                case DiffLineKind::Changed:
                    diff_background = theme.IsLight()
                        ? (ftxui::Color::RGB)(255, 243, 190)
                        : (ftxui::Color::RGB)(91, 75, 35);
                    break;
                case DiffLineKind::None: break;
            }
        }
        if (highlight_diff_navigation && line_index == session_->git_compare_navigation_line) {
            diff_background = theme.IsLight()
                ? (ftxui::Color::RGB)(174, 218, 255)
                : (ftxui::Color::RGB)(40, 92, 135);
        }
        line_number = line_number | ftxui::bgcolor(diff_background);

        ftxui::Elements line_parts{line_number};
        if (text_left_padding > 0) {
            line_parts.push_back(ftxui::text(std::string(text_left_padding, ' ')));
        }
        const bool is_cursor_line = (session_ && line_index == session_->CursorRow());
        const bool has_selection = session_ && session_->HasSelection();
        const size_t cursor_x = is_cursor_line
            ? utils::Utf8ByteIndexAtDisplayColumn(line_content, session_->CursorCol(), 0)
            : line_content.size();
        const size_t render_start = segment.start;
        const size_t render_end = segment.end;
        if (!smart_word_wrap) {
            const size_t start_column = utils::Utf8DisplayWidth(line_content, 0, render_start);
            if (start_column > viewport_->scroll_x) {
                line_parts.push_back(ftxui::text(std::string(start_column - viewport_->scroll_x, ' ')));
            }
        }

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
                character = character | ftxui::color(text_color) | ftxui::bgcolor(diff_background);
            }

            if (is_bracket_highlight(x, line_index)) {
                character = character | ftxui::bgcolor(theme.cursor) | ftxui::color(theme.background) | ftxui::bold | ftxui::underlined;
            }

            if (!has_selection && is_cursor_line && x == cursor_x) {
                character = ftxui::text(glyph) | ftxui::bgcolor(theme.selection_bg) |
                    ftxui::color(theme.selection_fg);
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
        return ftxui::hbox(std::move(line_parts)) | ftxui::bgcolor(diff_background);
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

            const size_t skip = smart_word_wrap && line_index == viewport_->scroll_y
                ? viewport_->wrapped_scroll_offset : 0;
            std::vector<utils::Utf8WrapSegment> segments;
            if (smart_word_wrap) {
                segments = utils::BuildUtf8WrapSegmentsLimited(
                    session_->lines[line_index],
                    visible_width,
                    visible_height - rendered_content_rows + skip);
            } else {
                const auto& line = session_->lines[line_index];
                size_t start = utils::Utf8ByteIndexAtDisplayColumn(line, 0, viewport_->scroll_x);
                if (start < line.size() && utils::Utf8DisplayWidth(line, 0, start) < viewport_->scroll_x) {
                    start = utils::NextUtf8CodepointStart(line, start);
                }
                const size_t end = utils::Utf8ByteIndexAtDisplayColumn(line, 0, viewport_->scroll_x + visible_width);
                segments.push_back({start, std::max(start, end)});
            }

            for (size_t index = skip; index < segments.size(); ++index) {
                if (rendered_content_rows >= visible_height) break;
                lines_elements.push_back(render_line_segment(
                    line_index, segments[index], rendered_content_rows, syntax_token_ptr));
                ++rendered_content_rows;
            }
        }
    }

    while (rendered_content_rows < visible_height) {
        lines_elements.push_back(render_empty_row(rendered_content_rows));
        ++rendered_content_rows;
    }

    const bool show_horizontal = horizontal.max_offset > 0;
    while (lines_elements.size() < raw_height - show_horizontal) {
        lines_elements.push_back(render_empty_row(visible_height > 0 ? visible_height - 1 : 0));
    }
    if (show_horizontal) {
        const auto rule = [](std::string_view glyph, size_t count) {
            std::string line;
            for (size_t i = 0; i < count; ++i) line += glyph;
            return ftxui::text(std::move(line));
        };
        auto track = ftxui::hbox({
            rule("─", horizontal.thumb_start) | ftxui::color(theme.gutter),
            rule("━", horizontal.thumb_width) | ftxui::color(theme.cursor),
            rule("─", horizontal.width - horizontal.thumb_start - horizontal.thumb_width) |
                ftxui::color(theme.gutter),
        }) | ftxui::reflect(viewport_->horizontal_scrollbar_box);
        lines_elements.push_back(ftxui::hbox({
            blank_line_number(),
            ftxui::text(std::string(text_left_padding, ' ')),
            std::move(track),
            ftxui::filler(),
            ftxui::text(std::string(scrollbar_columns, ' ')),
        }));
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
    const size_t previous_top = viewport_->scroll_y;
    viewport_->ScrollToCursor(*session_, config_);
    if (viewport_->scroll_y != previous_top) viewport_->wrapped_scroll_offset = 0;

    if (current_search_match_ >= search_matches_.size()) {
        viewport_->wrapped_scroll_offset = 0;
        return;
    }
    const SearchMatch& match = search_matches_[current_search_match_];
    if (match.y != session_->CursorRow() || match.x != session_->CursorCol()) {
        viewport_->wrapped_scroll_offset = 0;
        return;
    }

    const std::string& line = session_->lines[match.y];
    const size_t width = VisibleTextWidth();
    if (viewport_->IsDistractionMode() || (config_ && config_->smart_word_wrap)) {
        const size_t height = VisibleHeight();
        const auto segments = utils::BuildUtf8WrapSegmentsLimited(line, width, line.size());
        const auto match_segment = std::find_if(segments.begin(), segments.end(),
            [&](const auto& segment) { return match.x < segment.end; });
        if (match_segment == segments.end()) return;
        const size_t match_offset = static_cast<size_t>(match_segment - segments.begin());
        size_t remaining = height;
        for (size_t row = viewport_->scroll_y; row <= match.y && remaining > 0; ++row) {
            const size_t skip = row == viewport_->scroll_y
                ? viewport_->wrapped_scroll_offset : 0;
            if (row == match.y) {
                if (match_offset >= skip && match_offset - skip < remaining) return;
                break;
            }
            const auto preceding = utils::BuildUtf8WrapSegmentsLimited(
                session_->lines[row], width, remaining + skip);
            remaining -= preceding.size() - std::min(skip, preceding.size());
        }
        viewport_->scroll_y = match.y;
        viewport_->wrapped_scroll_offset = match_offset >= height
            ? match_offset : 0;
        return;
    }
    viewport_->wrapped_scroll_offset = 0;
    if (viewport_->HasManualHorizontalScroll()) return;
    // Keep the whole match visible when it fits, starting at the match otherwise.
    const size_t match_end = utils::Utf8DisplayWidth(line, 0, match.x + match.length);
    if (match_end > viewport_->scroll_x + width) {
        const size_t match_start = utils::Utf8DisplayWidth(line, 0, match.x);
        viewport_->scroll_x = std::min(match_start, match_end - width);
    }
}

} // namespace textlt
