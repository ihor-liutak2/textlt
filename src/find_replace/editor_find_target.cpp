#include "find_replace/editor_find_target.hpp"

#include <cstdint>
#include <utility>

namespace textlt {

EditorFindTarget::EditorFindTarget(Provider provider)
    : provider_(std::move(provider)) {}

EditorComponent* EditorFindTarget::Editor() const {
    return provider_ ? provider_() : nullptr;
}

bool EditorFindTarget::IsAvailable() const {
    auto* editor = Editor();
    return editor != nullptr && editor->GetSession() != nullptr;
}

bool EditorFindTarget::SupportsReplace() const {
    auto* editor = Editor();
    return editor != nullptr && editor->GetSession() != nullptr &&
        !editor->IsReadOnly();
}

std::string EditorFindTarget::UnavailableMessage() const {
    return "No active document";
}

std::uint64_t EditorFindTarget::Version() const {
    auto* editor = Editor();
    const auto session = editor ? editor->GetSession() : nullptr;
    if (!session) return 0;
    const auto identity = static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(session.get()));
    const std::uint64_t buffer_version = session->buffer.Version();
    return identity ^ (buffer_version + 0x9e3779b97f4a7c15ULL +
        (identity << 6) + (identity >> 2));
}

std::vector<FindResult> EditorFindTarget::Find(const std::string& query,
    const FindOptions& options, std::size_t limit, bool& truncated) {
    std::vector<FindResult> results;
    auto* editor = Editor();
    if (!editor || !editor->GetSession()) {
        truncated = false;
        return results;
    }
    for (const auto& match : editor->FindAll(query, options.match_case,
             options.whole_word, limit, truncated)) {
        FindResult result;
        result.key = std::to_string(match.row) + ":" +
            std::to_string(match.column) + ":" + std::to_string(match.length);
        result.label = "Ln " + std::to_string(match.row + 1);
        result.preview = match.preview;
        result.preview_match_start = match.preview_match_start;
        result.preview_match_length = match.preview_match_length;
        results.push_back(std::move(result));
    }
    return results;
}

void EditorFindTarget::Activate(std::size_t index) {
    if (auto* editor = Editor()) editor->ActivateSearchResult(index);
}

bool EditorFindTarget::ReplaceAt(std::size_t index,
    const std::string& replacement, std::string& error) {
    auto* editor = Editor();
    if (!editor) {
        error = "No active document";
        return false;
    }
    return editor->ReplaceSearchResult(index, replacement, error);
}

bool EditorFindTarget::ReplaceAll(const std::string& query,
    const std::string& replacement, const FindOptions& options,
    std::size_t& count, std::string& error) {
    auto* editor = Editor();
    if (!editor) {
        error = "No active document";
        return false;
    }
    if (editor->IsReadOnly()) {
        error = "Document is read-only";
        return false;
    }
    count = editor->ReplaceAllSearchMatches(query, replacement,
        options.match_case, options.whole_word);
    return true;
}

void EditorFindTarget::ClearHighlights() {
    if (auto* editor = Editor()) editor->ClearSearchHighlights();
}

} // namespace textlt
