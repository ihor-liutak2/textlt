#pragma once

#include <functional>

#include "actions/actions_workspace_component.hpp"
#include "find_replace/find_target.hpp"

namespace textlt {

class ActionsFindTarget : public FindTarget {
public:
    using Provider = std::function<actions::ActionsWorkspaceComponent*()>;

    explicit ActionsFindTarget(Provider provider);

    bool IsAvailable() const override;
    bool SupportsReplace() const override;
    std::string UnavailableMessage() const override;
    std::uint64_t Version() const override;

    std::vector<FindResult> Find(const std::string& query,
        const FindOptions& options, std::size_t limit, bool& truncated) override;
    void Activate(std::size_t index) override;
    bool ReplaceAt(std::size_t index, const std::string& replacement,
        std::string& error) override;
    bool ReplaceAll(const std::string& query, const std::string& replacement,
        const FindOptions& options, std::size_t& count,
        std::string& error) override;
    void ClearHighlights() override;

private:
    actions::ActionsWorkspaceComponent* Actions() const;

    Provider provider_;
};

} // namespace textlt
