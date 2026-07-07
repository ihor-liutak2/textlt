#include "app.hpp"

#include <memory>

#include "ftxui/component/event.hpp"

namespace textlt {

void TextltApp::SetTtsHeaderActiveButton(TtsHeaderButton button) {
    tts_header_active_button_ = button;
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::QueueTtsBookPreparationFromCursor(bool force_rebuild) {
    const std::shared_ptr<DocumentSession> document = ActiveSessionPtr();
    if (!document) {
        active_action_ = "No document selected for TTS";
        screen_.PostEvent(ftxui::Event::Custom);
        return;
    }

    // Submit the selected open document, not whichever editor pane happens to
    // have focus, so TTS follows the left-panel selection.
    cloud_tts_pipeline_.Submit(
        document->ToContent(),
        document->CurrentFilePath(),
        document->CursorRow(),
        force_rebuild);

    active_action_ = "Queued TTS book preparation";
    screen_.PostEvent(ftxui::Event::Custom);
}


} // namespace textlt
