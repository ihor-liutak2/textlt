#pragma once

#include <string>
#include <vector>

#include "ftxui/component/component_base.hpp"
#include "ftxui/dom/elements.hpp"
#include "editor_config.hpp"
#include "theme.hpp"

namespace textlt {

class EditorComponent : public ftxui::ComponentBase {
public:
    EditorComponent(EditorConfig* config, const Theme* theme);

    ftxui::Element Render() override;
    bool OnEvent(ftxui::Event event) override;
    bool Focusable() const override;

    void SaveToFile(const std::string& path);
    void LoadFromFile(const std::string& path);
    const std::string& CurrentFilePath() const;

    bool HasSelection() const;
    std::string GetSelectedText() const;
    void DeleteSelection();
    void ClearSelection();
    void InsertText(const std::string& text);
    void Undo();
    void Redo();
    
    std::string GetCurrentLineText() const;
    void DeleteCurrentLine();

private:
    struct EditorState {
        std::vector<std::string> lines;
        int cursor_x = 0;
        int cursor_y = 0;
    };

    static constexpr size_t kMaxHistory = 100;

    size_t VisibleHeight() const;
    size_t LineNumberWidth() const;
    std::string LineNumberText(size_t line_index, size_t width) const;
    void UpdateScroll();
    static bool IsWordCharacter(char character);
    bool IsCharacterSelected(size_t x, size_t y) const;
    void BeginSelection();
    void ClampCursorToBuffer();
    void MoveCursorHome();
    void MoveCursorEnd();
    void MoveCursorLeft();
    void MoveCursorRight();
    void MoveCursorUp();
    void MoveCursorDown();
    void MoveCursorToPreviousWord();
    void MoveCursorToNextWord();
    EditorState CurrentState() const;
    void ApplyState(const EditorState& state);
    void SaveSnapshot();
    void SaveSnapshotForTyping(const std::string& input);
    void EndTypingGroup();
    void DeleteSelectionWithoutSnapshot();

    std::vector<std::string> text_lines_;
    std::vector<EditorState> undo_stack_;
    std::vector<EditorState> redo_stack_;
    std::string current_filepath_ = "untitled.txt";
    EditorConfig* config_ = nullptr;
    const Theme* theme_ = nullptr;
    size_t cursor_x_ = 0;
    size_t cursor_y_ = 0;
    size_t scroll_y_ = 0;
    size_t selection_anchor_x_ = 0;
    size_t selection_anchor_y_ = 0;
    bool has_selection_ = false;
    bool typing_group_active_ = false;
    ftxui::Box editor_box_;
};

} // namespace textlt
