#include "modals/modal_search_files.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <cstdlib>
#include <fstream>
#include <system_error>
#include <utility>
#include "json_utils.hpp"
#include "ui_button.hpp"

#include "ftxui/component/component_options.hpp"
#include "ftxui/dom/elements.hpp"

namespace textlt {
namespace {

ButtonSpec SearchButtonSpec(std::string label) {
    ButtonSpec spec;
    spec.caption = std::move(label);
    spec.variant = ButtonVariant::AccentEdges;

    const std::string& caption = spec.caption;
    if (caption == "Open" || caption == "Apply" || caption == "Add" || caption == "Save") {
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
    if (caption == "<< Start" || caption == "< Mask" || caption == "Mask >") {
        spec.role = ButtonRole::Navigation;
        spec.size = ButtonSize::Compact;
        return spec;
    }
    if (caption == "Paste") {
        spec.role = ButtonRole::Warning;
        return spec;
    }
    if (caption == "Copy paths") {
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

bool IsDigitsOnly(const std::string& value) {
    return !value.empty() &&
           std::all_of(value.begin(), value.end(), [](unsigned char ch) {
               return std::isdigit(ch) != 0;
           });
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
