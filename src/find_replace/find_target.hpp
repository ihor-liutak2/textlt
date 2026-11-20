#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace textlt {

struct FindOptions {
    bool match_case = false;
    bool whole_word = false;
};

// One match exposed by a workspace backend. `preview` is the single text line
// that contains the match and `preview_match_start` / `preview_match_length`
// describe the matched bytes inside it. The panel shortens the preview to fit
// the sidebar and keeps the highlight aligned.
struct FindResult {
    std::string key;
    std::string label;
    std::string preview;
    std::size_t preview_match_start = 0;
    std::size_t preview_match_length = 0;
};

class FindTarget {
public:
    virtual ~FindTarget() = default;

    virtual bool IsAvailable() const = 0;
    virtual bool SupportsReplace() const = 0;
    virtual std::string UnavailableMessage() const = 0;
    virtual std::uint64_t Version() const = 0;

    // Returns at most `limit` results and sets `truncated` when more matches
    // exist. Implementations also refresh their own match highlighting.
    virtual std::vector<FindResult> Find(
        const std::string& query,
        const FindOptions& options,
        std::size_t limit,
        bool& truncated) = 0;

    // Moves the target cursor/selection to the indexed match. The index refers
    // to the vector returned by the latest Find() call.
    virtual void Activate(std::size_t index) = 0;

    // Replaces the indexed match. The index refers to the latest Find().
    virtual bool ReplaceAt(
        std::size_t index,
        const std::string& replacement,
        std::string& error) = 0;

    virtual bool ReplaceAll(
        const std::string& query,
        const std::string& replacement,
        const FindOptions& options,
        std::size_t& count,
        std::string& error) = 0;

    virtual void ClearHighlights() = 0;
};

constexpr std::size_t kFindResultLimit = 100;

} // namespace textlt
