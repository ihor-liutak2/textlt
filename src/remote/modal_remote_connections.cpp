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
#include "ui_button.hpp"
#include "remote/remote_dialog_theme.hpp"
#include "remote/remote_dropbox_provider.hpp"
#include "remote/remote_google_drive_provider.hpp"
#include "remote/remote_microsoft_drive_provider.hpp"
#include "remote/remote_oauth_flow.hpp"
#include "remote/remote_oauth_token_store.hpp"
#include "remote/remote_sftp_provider.hpp"

namespace textlt {
namespace {

std::string TrimForDisplay(const std::string& value, size_t width) {
    if (value.size() <= width) {
        return value;
    }
    if (width <= 3) {
        return value.substr(0, width);
    }
    return value.substr(0, width - 3) + "...";
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
            return "SFTP / SSH";
        case RemoteConnectionType::GoogleDrive:
            return "Google Drive";
        case RemoteConnectionType::MicrosoftDrive:
            return "Microsoft OneDrive / SharePoint";
        case RemoteConnectionType::Dropbox:
            return "Dropbox";
    }
    return "SFTP / SSH";
}

std::string DefaultNameForType(RemoteConnectionType type) {
    switch (type) {
        case RemoteConnectionType::Sftp:
            return "New SFTP";
        case RemoteConnectionType::GoogleDrive:
            return "New Google Drive";
        case RemoteConnectionType::MicrosoftDrive:
            return "New Microsoft Drive";
        case RemoteConnectionType::Dropbox:
            return "New Dropbox";
    }
    return "New Remote";
}

std::string ConnectionTargetSummary(const RemoteConnectionConfig& config) {
    switch (config.type) {
        case RemoteConnectionType::Sftp: {
            std::string target = config.ssh_config_host.empty() ? config.host : config.ssh_config_host;
            if (!config.user.empty() && config.ssh_config_host.empty()) {
                target = config.user + "@" + target;
            }
            return target;
        }
        case RemoteConnectionType::GoogleDrive:
            if (!config.account_label.empty()) {
                return config.account_label;
            }
            if (!config.root_folder_id.empty()) {
                return "folder " + config.root_folder_id;
            }
            return "Google Drive";
        case RemoteConnectionType::MicrosoftDrive:
            if (!config.account_label.empty()) {
                return config.account_label;
            }
            if (!config.site_id.empty()) {
                return "site " + config.site_id;
            }
            if (!config.drive_id.empty()) {
                return "drive " + config.drive_id;
            }
            return "OneDrive / SharePoint";
        case RemoteConnectionType::Dropbox:
            if (!config.account_label.empty()) {
                return config.account_label;
            }
            return config.remote_root.empty() ? "/" : config.remote_root;
    }
    return {};
}

} // namespace

#include "remote/modal_remote_connections/setup.cpp"
#include "remote/modal_remote_connections/render.cpp"
#include "remote/modal_remote_connections/events.cpp"
#include "remote/modal_remote_connections/actions.cpp"
#include "remote/modal_remote_connections/wrapper.cpp"

} // namespace textlt
