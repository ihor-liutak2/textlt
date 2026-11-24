#pragma once

#include <array>
#include <functional>
#include <string>
#include <utility>

#include "ftxui/dom/elements.hpp"
#include "theme.hpp"

namespace textlt {

enum class WorkspaceTab { Documents, Notes, Actions, Connect };

class WorkspaceTabs {
public:
    using Callbacks = std::array<std::function<void()>, 4>;
    static constexpr int kConnectRowWidth = 22;

    explicit WorkspaceTabs(Callbacks callbacks = {}) : callbacks_(std::move(callbacks)) {}

    void SetConnectCallback(std::function<void()> callback) {
        callbacks_[static_cast<std::size_t>(WorkspaceTab::Connect)] = std::move(callback);
    }

    void ClearHitboxes() {
        boxes_.fill(ftxui::Box{1, 0, 1, 0});
    }

    ftxui::Element Render(const Theme& theme, WorkspaceTab active) {
        ClearHitboxes();
        constexpr std::array labels{"Docs", "Notes", "Actions", "Connect"};
        ftxui::Elements tabs;
        for (std::size_t index = 0; index < labels.size(); ++index) {
            if (index == static_cast<std::size_t>(active)) continue;
            tabs.push_back(ftxui::text(std::string(" ") + labels[index] + " ") |
                           ftxui::color(theme.modal_text_color) | ftxui::reflect(boxes_[index]));
        }
        return ftxui::hbox(std::move(tabs));
    }

    bool ActivateAt(int x, int y) {
        for (std::size_t index = 0; index < boxes_.size(); ++index) {
            if (boxes_[index].Contain(x, y) && callbacks_[index]) {
                callbacks_[index]();
                return true;
            }
        }
        return false;
    }

private:
    Callbacks callbacks_;
    std::array<ftxui::Box, 4> boxes_{};
};

} // namespace textlt
