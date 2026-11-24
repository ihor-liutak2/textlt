#pragma once

#include "actions/action_model.hpp"
#include "notes/note_document.hpp"
#include "textlt/connect/content_handler.hpp"

namespace textlt {
namespace notes { class NotesWorkspaceComponent; }
namespace actions { class ActionsWorkspaceComponent; }

std::unique_ptr<connect::ContentHandler> MakeNoteConnectHandler(notes::NotesWorkspaceComponent& workspace);
std::unique_ptr<connect::ContentHandler> MakeActionConnectHandler(actions::ActionsWorkspaceComponent& workspace);
std::pair<connect::TransferMetadata, connect::StructuredPayload> ConnectSnapshot(const notes::NoteDocument& note);
std::pair<connect::TransferMetadata, connect::StructuredPayload> ConnectSnapshot(const actions::ActionItem& action);

} // namespace textlt
