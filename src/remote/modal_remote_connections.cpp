#include "remote/modal_remote_connections.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <utility>

#include "ftxui/component/component_options.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"
#include "editor_utils.hpp"
#include "ui_button.hpp"
#include "remote/remote_dialog_theme.hpp"
#include "remote/remote_dropbox_provider.hpp"
#include "remote/remote_ftps_provider.hpp"
#include "remote/remote_oauth_token_store.hpp"
#include "remote/remote_provider.hpp"
#include "remote/remote_provider_factory.hpp"
#include "remote/remote_sftp_provider.hpp"
#include "remote/remote_ssh_config.hpp"

namespace textlt {
namespace {

std::string TrimForDisplay(const std::string& value, size_t width) {
    if (textlt::utils::Utf8DisplayWidth(value) <= width) {
        return value;
    }
    if (width <= 3) {
        return value.substr(
            0,
            textlt::utils::Utf8ByteIndexAtDisplayColumn(value, 0, width));
    }
    return value.substr(
        0,
        textlt::utils::Utf8ByteIndexAtDisplayColumn(value, 0, width - 3)) + "...";
}

int ParsePort(const std::string& text) {
    try {
        const int value = std::stoi(text);
        return value > 0 ? value : 22;
    } catch (...) {
        return 22;
    }
}

std::string TypeDisplayName(RemoteConnectionType type) {
    switch (type) {
        case RemoteConnectionType::Sftp:
            return "SFTP";
        case RemoteConnectionType::Dropbox:
            return "Dropbox";
        case RemoteConnectionType::Ftps:
            return "FTPS";
        case RemoteConnectionType::GoogleDrive:
            return "Google Drive";
        case RemoteConnectionType::MicrosoftDrive:
            return "Microsoft Drive";
    }
    return "Remote";
}

std::string DefaultNameForType(RemoteConnectionType type) {
    switch (type) {
        case RemoteConnectionType::Sftp:
            return "New SFTP";
        case RemoteConnectionType::Dropbox:
            return "New Dropbox";
        case RemoteConnectionType::Ftps:
            return "New FTPS";
        case RemoteConnectionType::GoogleDrive:
            return "New Google Drive";
        case RemoteConnectionType::MicrosoftDrive:
            return "New Microsoft Drive";
    }
    return "New Remote";
}

std::string ConnectionTargetSummary(const RemoteConnectionConfig& config) {
    switch (config.type) {
        case RemoteConnectionType::Sftp:
            break;
        case RemoteConnectionType::Dropbox:
            return config.remote_root.empty() ? "/" : config.remote_root;
        case RemoteConnectionType::Ftps:
            break;
        case RemoteConnectionType::GoogleDrive:
        case RemoteConnectionType::MicrosoftDrive:
            return config.remote_root.empty() ? "/" : config.remote_root;
    }
    {
        std::string target = config.ssh_config_host.empty() ? config.host : config.ssh_config_host;
        if (!config.user.empty() && config.ssh_config_host.empty()) {
            target = config.user + "@" + target;
        }
        return target;
    }
}

std::string ConnectionKindLabel(const RemoteConnectionConfig& config) {
    switch (config.type) {
        case RemoteConnectionType::Sftp:
            return "SFTP";
        case RemoteConnectionType::Dropbox:
            return "Dropbox";
        case RemoteConnectionType::Ftps:
            return "FTPS";
        case RemoteConnectionType::GoogleDrive:
            return "Google Drive";
        case RemoteConnectionType::MicrosoftDrive:
            return "Microsoft Drive";
    }
    return "Remote";
}

} // namespace

#include "remote/modal_remote_connections/setup.cpp"
#include "remote/modal_remote_connections/render.cpp"
#include "remote/modal_remote_connections/tab_connections.cpp"
#include "remote/modal_remote_connections/tab_ftps.cpp"
#include "remote/modal_remote_connections/tab_sftp.cpp"
#include "remote/modal_remote_connections/tab_dropbox.cpp"
#include "remote/modal_remote_connections/events.cpp"
#include "remote/modal_remote_connections/actions.cpp"
#include "remote/modal_remote_connections/wrapper.cpp"

} // namespace textlt
