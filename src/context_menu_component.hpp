#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "ftxui/component/component_base.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/terminal.hpp"
#include "theme.hpp"

namespace textlt {

class ContextMenuComponent : public ftxui::ComponentBase {
public:
    struct Item {
        std::string command_id;
        std::string label;
        bool enabled = true;
    };
    using Execute = std::function<void(const std::string&)>;

    ContextMenuComponent(const Theme& theme, Execute execute);
    void Open(int x, int y, std::vector<Item> items);
    void Close();
    bool IsOpen() const { return !items_.empty(); }
    bool Focusable() const override { return IsOpen(); }
    ftxui::Element OnRender() override;
    ftxui::Element RenderIn(ftxui::Dimensions viewport);
    bool OnEvent(ftxui::Event event) override;

private:
    bool IsEnabled(int index) const;
    void MoveSelection(int direction);
    void ExecuteSelected();

    const Theme& theme_;
    Execute execute_;
    std::vector<Item> items_;
    int anchor_x_ = 0;
    int anchor_y_ = 0;
    int selected_ = -1;
    int pressed_ = -1;
    int first_row_ = 0;
    ftxui::Box box_{0, -1, 0, -1};
};

} // namespace textlt
