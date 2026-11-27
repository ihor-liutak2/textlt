#pragma once

#include <array>
#include <functional>
#include <utility>

#include "ftxui/dom/elements.hpp"
#include "theme.hpp"

namespace textlt {

enum class WorkspaceMode { Documents, Notes, Actions, Connect };

class WorkspaceTabs {
public:
    using Callbacks = std::array<std::function<void()>, 4>;

    WorkspaceTabs(const WorkspaceMode& active, Callbacks callbacks)
        : active_(active), callbacks_(std::move(callbacks)) {
        ClearHitboxes();
    }

    void ClearHitboxes() {
        boxes_.fill(ftxui::Box{1, 0, 1, 0});
    }

    ftxui::Element Render(const Theme& theme) {
        constexpr std::array labels{" Text ", " Notes ", " Actions ", " Connect "};
        ftxui::Elements tabs;
        for (std::size_t index = 0; index < labels.size(); ++index) {
            auto tab = ftxui::hbox({
                ftxui::text(" "),
                ftxui::text(labels[index]),
                ftxui::text(" "),
            });
            if (index == static_cast<std::size_t>(active_)) {
                tab = tab | ftxui::bgcolor(theme.modal_selected_item_bg) |
                      ftxui::color(theme.modal_selected_item_fg);
            }
            tabs.push_back(tab | ftxui::reflect(boxes_[index]));
        }
        return ftxui::hbox(std::move(tabs));
    }

    bool ActivateAt(int x, int y) {
        for (std::size_t index = 0; index < boxes_.size(); ++index) {
            if (boxes_[index].Contain(x, y)) {
                if (index != static_cast<std::size_t>(active_) && callbacks_[index]) {
                    callbacks_[index]();
                }
                return true;
            }
        }
        return false;
    }

private:
    const WorkspaceMode& active_;
    Callbacks callbacks_;
    std::array<ftxui::Box, 4> boxes_{};
};

} // namespace textlt
