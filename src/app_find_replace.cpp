#include "app.hpp"

#include <algorithm>
#include <memory>
#include <string>

namespace textlt {

    void TextltApp::OpenFindPanel(bool replace_mode) {
        if (menu_bar_) {
            menu_bar_->CloseDropdown();
        }
        current_search_mode_ = replace_mode ? SearchMode::Replace : SearchMode::Find;
        search_panel_tab_index_ = replace_mode ? 1 : 0;
        focused_layer_ = 5;
        RefreshFindMatches();
        if (replace_mode) {
            active_search_panel_input_ = SearchPanelInput::Find;
            replace_find_input_->TakeFocus();
        } else {
            active_search_panel_input_ = SearchPanelInput::Find;
            find_input_->TakeFocus();
        }
    }

    void TextltApp::CloseFindPanel() {
        current_search_mode_ = SearchMode::None;
        std::static_pointer_cast<EditorComponent>(text_editor_)->ClearSearchHighlights();
        FocusEditor();
    }

    void TextltApp::RefreshFindMatches() {
        auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
        editor_ptr->HighlightMatches(find_query_);
        if (find_query_.empty()) {
            active_action_ = "Find query empty";
        } else {
            active_action_ = FindMatchStatus();
        }
    }

    void TextltApp::FindNext() {
        auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
        editor_ptr->HighlightMatches(find_query_);
        editor_ptr->JumpToNextMatch();
        active_action_ = FindMatchStatus();
    }

    void TextltApp::FindPrevious() {
        auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
        editor_ptr->HighlightMatches(find_query_);
        editor_ptr->JumpToPreviousMatch();
        active_action_ = FindMatchStatus();
    }

    void TextltApp::ReplaceNext() {
        auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
        editor_ptr->ExecuteReplaceNext(find_query_, replace_text_);
        active_action_ = "Replace next: " + FindMatchStatus();
    }

    void TextltApp::ReplaceAll() {
        auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
        editor_ptr->HighlightMatches(find_query_);
        const size_t count_before = editor_ptr->SearchMatchCount();
        editor_ptr->ExecuteReplaceAll(find_query_, replace_text_);
        active_action_ = "Replaced " + std::to_string(count_before) + " matches";
    }

    void TextltApp::PasteIntoFindPanelInput() {
        std::string clipboard_text = ReadSystemClipboard();
        if (clipboard_text.empty()) {
            active_action_ = "Clipboard empty.";
            return;
        }

        if (current_search_mode_ == SearchMode::Replace &&
            active_search_panel_input_ == SearchPanelInput::Replace) {
            replace_input_cursor_position_ = std::clamp(
                replace_input_cursor_position_, 0, static_cast<int>(replace_text_.size()));
            replace_text_.insert(replace_input_cursor_position_, clipboard_text);
            replace_input_cursor_position_ += static_cast<int>(clipboard_text.size());
            replace_input_->TakeFocus();
            active_action_ = "Pasted text into replace field";
            return;
        }

        int& cursor_position = current_search_mode_ == SearchMode::Replace
            ? replace_find_input_cursor_position_
            : find_input_cursor_position_;
        cursor_position = std::clamp(
            cursor_position, 0, static_cast<int>(find_query_.size()));
        find_query_.insert(cursor_position, clipboard_text);
        cursor_position += static_cast<int>(clipboard_text.size());
        RefreshFindMatches();
        if (current_search_mode_ == SearchMode::Replace) {
            replace_find_input_->TakeFocus();
        } else {
            find_input_->TakeFocus();
        }
    }

    void TextltApp::ClearFindPanelFields() {
        find_query_.clear();
        find_input_cursor_position_ = 0;
        replace_find_input_cursor_position_ = 0;

        if (current_search_mode_ == SearchMode::Replace) {
            replace_text_.clear();
            replace_input_cursor_position_ = 0;
            active_search_panel_input_ = SearchPanelInput::Find;
            replace_find_input_->TakeFocus();
        } else {
            find_input_->TakeFocus();
        }

        RefreshFindMatches();
    }

    std::string TextltApp::FindMatchStatus() const {
        auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
        const size_t count = editor_ptr->SearchMatchCount();
        if (find_query_.empty()) {
            return "Find";
        }
        if (count == 0) {
            return "No matches";
        }
        return "Match " + std::to_string(editor_ptr->CurrentSearchMatchIndex()) +
        " of " + std::to_string(count);
    }

} // namespace textlt
