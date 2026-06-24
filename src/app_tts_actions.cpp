#include "app.hpp"

#include <memory>

#include "ftxui/component/event.hpp"

namespace textlt {


void TextltApp::QueueTtsBookPreparationFromCursor() {
    auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);

    // Submit document content and cursor row index for TTS processing
    cloud_tts_pipeline_.Submit(
        editor_ptr->GetAllText(),
        editor_ptr->CurrentFilePath(),
        editor_ptr->GetCursorRow());

    active_action_ = "Queued TTS book preparation";
    screen_.PostEvent(ftxui::Event::Custom);
}


} // namespace textlt
