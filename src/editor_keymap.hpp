#pragma once

#include <memory>
#include <string>
#include <vector>

#include "shortcut_registry.hpp"

namespace textlt {

class EditorComponent;
class TextltApp;

class EditorKeymap {
public:
    static std::vector<ShortcutBindingDefinition> DefaultBindings();
    bool RunAction(TextltApp& app, const std::string& action_id) const;

private:
    enum class MoveAction {
        Left,
        Right,
        Up,
        Down,
        WordLeft,
        WordRight,
        Home,
        End,
        DocumentStart,
        DocumentEnd,
        PageUp,
        PageDown,
        ParagraphUp,
        ParagraphDown,
        ParagraphSelectionUp,
        ParagraphSelectionDown,
    };

    static std::shared_ptr<EditorComponent> ActiveEditorPtr(TextltApp& app);
    static bool EnsureWritable(TextltApp& app, const std::shared_ptr<EditorComponent>& editor);
    static void FinishEditorAction(TextltApp& app);
    static void MoveCursor(EditorComponent& editor, MoveAction action, bool extend_selection);
};

} // namespace textlt
