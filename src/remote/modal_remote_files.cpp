#include "remote/modal_remote_files.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <iterator>
#include <sstream>
#include <system_error>
#include <utility>

#include "ftxui/component/component_options.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"
#include "remote/remote_dialog_theme.hpp"

namespace textlt {
namespace {

constexpr int kVisibleRows = 14;
constexpr int kDoubleClickMinMs = 80;
constexpr int kDoubleClickMaxMs = 500;

std::string BracketLabel(const std::string& label) {
    return "[" + label + "]";
}

bool IsBackspaceEvent(const ftxui::Event& event) {
    return event == ftxui::Event::Backspace ||
        event.input() == "\x7F" ||
        event.input() == "\x08";
}

bool IsPlainRemoteName(const std::string& name) {
    return !name.empty() &&
        name != "." &&
        name != ".." &&
        name.find('/') == std::string::npos &&
        name.find('\\') == std::string::npos;
}

std::string CurrentConnectionLabel(const std::vector<RemoteConnectionConfig>& connections, int index) {
    if (connections.empty()) {
        return "No connection";
    }
    index = std::clamp(index, 0, static_cast<int>(connections.size()) - 1);
    const RemoteConnectionConfig& config = connections[static_cast<size_t>(index)];
    if (!config.name.empty()) {
        return config.name;
    }
    if (!config.ssh_config_host.empty()) {
        return config.ssh_config_host;
    }
    return config.host;
}

} // namespace

#include "remote/modal_remote_files/setup.cpp"
#include "remote/modal_remote_files/render.cpp"
#include "remote/modal_remote_files/events.cpp"
#include "remote/modal_remote_files/loading_navigation.cpp"
#include "remote/modal_remote_files/transfers.cpp"
#include "remote/modal_remote_files/operations.cpp"
#include "remote/modal_remote_files/state_utils.cpp"
#include "remote/modal_remote_files/wrapper.cpp"

} // namespace textlt
