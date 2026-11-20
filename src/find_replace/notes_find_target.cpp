#include "find_replace/notes_find_target.hpp"

#include <cstdint>
#include <utility>

namespace textlt {

NotesFindTarget::NotesFindTarget(Provider provider)
    : provider_(std::move(provider)) {}

notes::NotesWorkspaceComponent* NotesFindTarget::Notes() const {
    return provider_ ? provider_() : nullptr;
}

bool NotesFindTarget::IsAvailable() const {
    auto* notes = Notes();
    return notes != nullptr && notes->IsEditing();
}

bool NotesFindTarget::SupportsReplace() const {
    return IsAvailable();
}

std::string NotesFindTarget::UnavailableMessage() const {
    return "Open a note in edit mode to find text";
}

std::uint64_t NotesFindTarget::Version() const {
    auto* notes = Notes();
    if (!notes) return 0;
    const auto* note = notes->EditingNote();
    const auto identity = static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(note));
    return identity ^ (notes->NoteRevision() + 0x9e3779b97f4a7c15ULL +
        (identity << 6) + (identity >> 2));
}

std::vector<FindResult> NotesFindTarget::Find(const std::string& query,
    const FindOptions& options, std::size_t limit, bool& truncated) {
    std::vector<FindResult> results;
    auto* notes = Notes();
    if (!notes || !notes->IsEditing()) {
        truncated = false;
        return results;
    }
    for (const auto& match : notes->SearchNote(query, options.match_case,
             options.whole_word, limit, truncated)) {
        FindResult result;
        result.key = std::to_string(match.block) + ":" +
            std::to_string(match.byte) + ":" + std::to_string(match.length);
        result.label = "Block " + std::to_string(match.block + 1);
        result.preview = match.preview;
        result.preview_match_start = match.preview_match_start;
        result.preview_match_length = match.preview_match_length;
        results.push_back(std::move(result));
    }
    return results;
}

void NotesFindTarget::Activate(std::size_t index) {
    if (auto* notes = Notes()) notes->ActivateNoteSearchResult(index);
}

bool NotesFindTarget::ReplaceAt(std::size_t index,
    const std::string& replacement, std::string& error) {
    auto* notes = Notes();
    if (!notes) {
        error = UnavailableMessage();
        return false;
    }
    return notes->ReplaceNoteSearchResult(index, replacement, error);
}

bool NotesFindTarget::ReplaceAll(const std::string& query,
    const std::string& replacement, const FindOptions& options,
    std::size_t& count, std::string& error) {
    auto* notes = Notes();
    if (!notes) {
        error = UnavailableMessage();
        return false;
    }
    return notes->ReplaceAllNoteText(query, replacement, options.match_case,
        options.whole_word, count, error);
}

void NotesFindTarget::ClearHighlights() {
    if (auto* notes = Notes()) notes->ClearNoteSearchHighlights();
}

} // namespace textlt
