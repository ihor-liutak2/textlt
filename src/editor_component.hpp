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
    
    std::string GetCurrentLineText() const;
    void DeleteCurrentLine();

private:
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

    std::vector<std::string> text_lines_;
    std::string current_filepath_ = "untitled.txt";
    EditorConfig* config_ = nullptr;
    const Theme* theme_ = nullptr;
    size_t cursor_x_ = 0;
    size_t cursor_y_ = 0;
    size_t scroll_y_ = 0;
    size_t selection_anchor_x_ = 0;
    size_t selection_anchor_y_ = 0;
    bool has_selection_ = false;
    ftxui::Box editor_box_;
};

} // namespace textlt
