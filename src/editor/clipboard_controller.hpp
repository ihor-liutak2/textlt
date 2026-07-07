#pragma once

#include <string>

namespace textlt {

class EditorComponent;

class ClipboardController {
public:
    struct ActionResult {
        bool changed_editor = false;
        std::string message;
    };

    std::string ReadText() const;
    bool WriteText(const std::string& text) const;

    ActionResult CutFromEditor(EditorComponent& editor) const;
    ActionResult CopyFromEditor(EditorComponent& editor) const;
    ActionResult PasteIntoEditor(EditorComponent& editor) const;
};

} // namespace textlt
