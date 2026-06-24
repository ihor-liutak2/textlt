#include "app.hpp"

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
            replace_find_input_->TakeFocus();
        } else {
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
