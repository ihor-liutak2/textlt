#include "modals/modal_search_files.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <cstdlib>
#include <fstream>
#include <system_error>
#include <utility>
#include "json_utils.hpp"
#include "keyboard_shortcuts.hpp"
#include "ui_button.hpp"

#include "ftxui/component/component_options.hpp"
#include "ftxui/dom/elements.hpp"

namespace textlt {
namespace {

constexpr int kMaxContextLines = 5;

class ContextInput final : public ftxui::ComponentBase {
public:
    ContextInput(size_t& value, const Theme*& theme, std::function<void()> on_enter)
        : value_(value), theme_(theme), on_enter_(std::move(on_enter)) {}

private:
    bool Focusable() const override { return true; }

    ftxui::Element OnRender() override {
        using namespace ftxui;
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        auto digit = text(std::to_string(value_)) | color(theme.modal_input_fg);
        if (Focused()) {
            digit |= bgcolor(theme.modal_selected_item_bg) |
                color(theme.modal_selected_item_fg) | bold;
        }
        return hbox({std::move(digit), filler()}) |
            size(WIDTH, EQUAL, 3) | bgcolor(theme.modal_input_bg) | reflect(box_);
    }

    bool OnEvent(ftxui::Event event) override {
        using namespace ftxui;
        if (event.is_mouse()) {
            const auto& mouse = event.mouse();
            if (mouse.button == Mouse::Left && mouse.motion == Mouse::Pressed &&
                box_.Contain(mouse.x, mouse.y) && CaptureMouse(event)) {
                TakeFocus();
                return true;
            }
            return false;
        }
        if (!Focused()) {
            return false;
        }
        if (event == Event::Return) {
            on_enter_();
            return true;
        }
        if (event == Event::Backspace || event == Event::Delete) {
            value_ = 0;
            return true;
        }
        if (event.is_character()) {
            const auto& input = event.character();
            if (input.size() == 1 && input.front() >= '0' && input.front() <= '0' + kMaxContextLines) {
                value_ = static_cast<size_t>(input.front() - '0');
            }
            return true;
        }
        return false;
    }

    size_t& value_;
    const Theme*& theme_;
    std::function<void()> on_enter_;
    ftxui::Box box_;
};

ButtonSpec SearchButtonSpec(std::string label) {
    ButtonSpec spec;
    spec.caption = std::move(label);
    spec.variant = ButtonVariant::AccentEdges;

    const std::string& caption = spec.caption;
    if (caption == "Open" || caption == "Use Mask" || caption == "Add" ||
        caption == "Save" || caption == "Search File Contents" ||
        caption == "Search Filenames") {
        spec.role = ButtonRole::Primary;
        return spec;
    }
    if (caption == "Delete") {
        spec.role = ButtonRole::Danger;
        return spec;
    }
    if (caption == "Toggle" || caption == "All" || caption == "None") {
        spec.role = ButtonRole::Toggle;
        spec.size = ButtonSize::Compact;
        return spec;
    }
    if (caption == "Close") {
        spec.role = ButtonRole::Cancel;
        return spec;
    }
    if (caption == "Paste") {
        spec.role = ButtonRole::Warning;
        return spec;
    }
    if (caption == "Copy Paths" || caption == "Copy Text") {
        spec.role = ButtonRole::Utility;
        return spec;
    }
    if (caption == "Clear") {
        spec.role = ButtonRole::Warning;
        return spec;
    }

    spec.role = ButtonRole::Secondary;
    return spec;
}

std::string ToDisplayPath(const std::filesystem::path& path) {
    const std::string value = path.generic_string();
    return value.empty() ? "." : value;
}

std::string RootLabelForPath(const std::filesystem::path& path) {
    const std::string filename = path.filename().string();
    if (!filename.empty()) {
        return filename;
    }

    const std::string value = path.generic_string();
    return value.empty() ? "." : value;
}

std::string LowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool IsIgnoredDirectoryName(const std::string& name) {
    static const std::vector<std::string> ignored = {
        ".git",
        ".hg",
        ".svn",
        ".cache",
        ".idea",
        ".vscode",
        "build",
        "cmake-build-debug",
        "cmake-build-release",
        "node_modules",
        "dist",
        "out",
        "target"
    };

    const std::string lower = LowerCopy(name);
    return std::find(ignored.begin(), ignored.end(), lower) != ignored.end();
}

} // namespace

#include "modal_search_files/components.cpp"
#include "modal_search_files/lifecycle.cpp"
#include "modal_search_files/directories.cpp"
#include "modal_search_files/search_actions.cpp"
#include "modal_search_files/render.cpp"
#include "modal_search_files/filters.cpp"
#include "modal_search_files/wrapper_events.cpp"

} // namespace textlt
