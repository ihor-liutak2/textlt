#include "find_replace/actions_find_target.hpp"

#include <utility>

namespace textlt {

ActionsFindTarget::ActionsFindTarget(Provider provider)
    : provider_(std::move(provider)) {}

actions::ActionsWorkspaceComponent* ActionsFindTarget::Actions() const {
    return provider_ ? provider_() : nullptr;
}

bool ActionsFindTarget::IsAvailable() const {
    auto* actions = Actions();
    return actions != nullptr && actions->HasSelectedPage();
}

bool ActionsFindTarget::SupportsReplace() const {
    return IsAvailable();
}

std::string ActionsFindTarget::UnavailableMessage() const {
    return "Select an Actions page to find text";
}

std::uint64_t ActionsFindTarget::Version() const {
    auto* actions = Actions();
    return actions ? actions->Revision() : 0;
}

std::vector<FindResult> ActionsFindTarget::Find(const std::string& query,
    const FindOptions& options, std::size_t limit, bool& truncated) {
    std::vector<FindResult> results;
    auto* actions = Actions();
    if (!actions || !actions->HasSelectedPage()) {
        truncated = false;
        return results;
    }
    for (const auto& match : actions->SearchActions(query, options.match_case,
             options.whole_word, limit, truncated)) {
        FindResult result;
        result.key = match.item_id + ":" +
            std::to_string(static_cast<int>(match.field)) + ":" +
            std::to_string(match.tab_index) + ":" + std::to_string(match.byte);
        result.label = match.label;
        result.preview = match.preview;
        result.preview_match_start = match.preview_match_start;
        result.preview_match_length = match.preview_match_length;
        results.push_back(std::move(result));
    }
    return results;
}

void ActionsFindTarget::Activate(std::size_t index) {
    if (auto* actions = Actions()) actions->ActivateActionSearchResult(index);
}

bool ActionsFindTarget::ReplaceAt(std::size_t index,
    const std::string& replacement, std::string& error) {
    auto* actions = Actions();
    if (!actions) {
        error = UnavailableMessage();
        return false;
    }
    return actions->ReplaceActionSearchResult(index, replacement, error);
}

bool ActionsFindTarget::ReplaceAll(const std::string& query,
    const std::string& replacement, const FindOptions& options,
    std::size_t& count, std::string& error) {
    auto* actions = Actions();
    if (!actions) {
        error = UnavailableMessage();
        return false;
    }
    return actions->ReplaceAllActionsText(query, replacement, options.match_case,
        options.whole_word, count, error);
}

void ActionsFindTarget::ClearHighlights() {
    if (auto* actions = Actions()) actions->ClearActionSearchHighlights();
}

} // namespace textlt
