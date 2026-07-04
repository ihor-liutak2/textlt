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
#include "remote/remote_dialog_theme.hpp"
#include "remote/remote_dropbox_provider.hpp"
#include "remote/remote_google_drive_provider.hpp"
#include "remote/remote_microsoft_drive_provider.hpp"
#include "remote/remote_oauth_flow.hpp"
#include "remote/remote_oauth_token_store.hpp"
#include "remote/remote_sftp_provider.hpp"

namespace textlt {
namespace {

std::string BracketLabel(const std::string& label) {
    return "[" + label + "]";
}

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

RemoteConnectionsModalContent::RemoteConnectionsModalContent(
    const Theme* theme,
    RemoteConfigStore* config_store,
    WriteClipboardCallback write_clipboard,
    CloseCallback on_close)
    : theme_(theme),
      config_store_(config_store),
      write_clipboard_(std::move(write_clipboard)),
      on_close_(std::move(on_close)) {
    auto input_transform = [this](ftxui::InputState state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        return RemoteDialogInputTransform(theme, std::move(state));
    };

    ftxui::InputOption base_option;
    base_option.multiline = false;
    base_option.transform = input_transform;

    ftxui::InputOption name_option = base_option;
    name_option.cursor_position = &name_cursor_;
    name_input_ = ftxui::Input(&name_value_, "connection name", name_option);

    ftxui::InputOption host_option = base_option;
    host_option.cursor_position = &host_cursor_;
    host_input_ = ftxui::Input(&host_value_, "host", host_option);

    ftxui::InputOption port_option = base_option;
    port_option.cursor_position = &port_cursor_;
    port_input_ = ftxui::Input(&port_value_, "22", port_option);

    ftxui::InputOption user_option = base_option;
    user_option.cursor_position = &user_cursor_;
    user_input_ = ftxui::Input(&user_value_, "user", user_option);

    ftxui::InputOption root_option = base_option;
    root_option.cursor_position = &remote_root_cursor_;
    remote_root_input_ = ftxui::Input(&remote_root_value_, "/remote/path", root_option);

    ftxui::InputOption identity_option = base_option;
    identity_option.cursor_position = &identity_file_cursor_;
    identity_file_input_ = ftxui::Input(&identity_file_value_, "~/.ssh/id_ed25519", identity_option);

    ftxui::InputOption ssh_alias_option = base_option;
    ssh_alias_option.cursor_position = &ssh_config_host_cursor_;
    ssh_config_host_input_ = ftxui::Input(&ssh_config_host_value_, "Host alias from ~/.ssh/config", ssh_alias_option);

    ftxui::InputOption account_option = base_option;
    account_option.cursor_position = &account_label_cursor_;
    account_label_input_ = ftxui::Input(&account_label_value_, "account label / email", account_option);

    ftxui::InputOption client_id_option = base_option;
    client_id_option.cursor_position = &client_id_cursor_;
    client_id_input_ = ftxui::Input(&client_id_value_, "OAuth client id", client_id_option);

    ftxui::InputOption client_secret_option = base_option;
    client_secret_option.cursor_position = &client_secret_cursor_;
    client_secret_input_ = ftxui::Input(&client_secret_value_, "OAuth client secret", client_secret_option);

    ftxui::InputOption tenant_option = base_option;
    tenant_option.cursor_position = &tenant_id_cursor_;
    tenant_id_input_ = ftxui::Input(&tenant_id_value_, "common / organizations / tenant id", tenant_option);

    ftxui::InputOption root_folder_option = base_option;
    root_folder_option.cursor_position = &root_folder_id_cursor_;
    root_folder_id_input_ = ftxui::Input(&root_folder_id_value_, "root folder id", root_folder_option);

    ftxui::InputOption site_option = base_option;
    site_option.cursor_position = &site_id_cursor_;
    site_id_input_ = ftxui::Input(&site_id_value_, "SharePoint site id", site_option);

    ftxui::InputOption drive_option = base_option;
    drive_option.cursor_position = &drive_id_cursor_;
    drive_id_input_ = ftxui::Input(&drive_id_value_, "OneDrive / SharePoint drive id", drive_option);

    ftxui::InputOption app_key_option = base_option;
    app_key_option.cursor_position = &app_key_cursor_;
    app_key_input_ = ftxui::Input(&app_key_value_, "Dropbox app key", app_key_option);

    ftxui::InputOption app_secret_option = base_option;
    app_secret_option.cursor_position = &app_secret_cursor_;
    app_secret_input_ = ftxui::Input(&app_secret_value_, "Dropbox app secret", app_secret_option);

    add_button_ = MakeTextButton("Add", [this] { AddConnection(); });
    delete_button_ = MakeTextButton("Delete", [this] { DeleteSelected(); });
    save_button_ = MakeTextButton("Save", [this] { SaveFormToSelected(); });
    test_button_ = MakeTextButton("Test", [this] { TestSelected(); });
    token_button_ = MakeTextButton("Token", [this] { PrepareTokenFile(); });
    sftp_type_button_ = MakeTextButton("SFTP", [this] { SelectType(RemoteConnectionType::Sftp); });
    google_type_button_ = MakeTextButton("Google", [this] { SelectType(RemoteConnectionType::GoogleDrive); });
    microsoft_type_button_ = MakeTextButton("Microsoft", [this] { SelectType(RemoteConnectionType::MicrosoftDrive); });
    dropbox_type_button_ = MakeTextButton("Dropbox", [this] { SelectType(RemoteConnectionType::Dropbox); });
    reload_button_ = MakeTextButton("Reload", [this] { Reload(); });
    help_button_ = MakeTextButton("Help Connect", [this] { OpenHelp(); });
    authorize_button_ = MakeTextButton("Authorize", [this] { AuthorizeConnection(); });

    auto redirect_option = base_option;
    redirect_option.cursor_position = &redirect_url_cursor_;
    redirect_url_input_ = ftxui::Input(&redirect_url_value_, "paste redirect URL here", redirect_option);
    submit_redirect_button_ = MakeTextButton("Submit", [this] { SubmitRedirectUrl(); });
    cancel_authorize_button_ = MakeTextButton("Cancel", [this] {
        authorize_pending_ = false;
        authorize_layer_index_ = 0;
        SetStatus("Authorization cancelled.");
    });
    close_button_ = MakeTextButton("Close", [this] {
        if (on_close_) {
            on_close_();
        }
    });

    list_component_ = ftxui::CatchEvent(
        ftxui::Renderer([this] { return RenderConnectionList(); }),
        [this](ftxui::Event event) { return HandleListEvent(std::move(event)); });

    ftxui::Components toolbar_buttons;
    toolbar_buttons.push_back(add_button_);
    toolbar_buttons.push_back(delete_button_);
    toolbar_buttons.push_back(save_button_);
    toolbar_buttons.push_back(test_button_);
    toolbar_buttons.push_back(token_button_);
    toolbar_buttons.push_back(reload_button_);
    toolbar_buttons.push_back(close_button_);

    ftxui::Components type_buttons;
    type_buttons.push_back(sftp_type_button_);
    type_buttons.push_back(google_type_button_);
    type_buttons.push_back(microsoft_type_button_);
    type_buttons.push_back(dropbox_type_button_);

    ftxui::Components form_fields;
    form_fields.push_back(name_input_);
    form_fields.push_back(host_input_);
    form_fields.push_back(port_input_);
    form_fields.push_back(user_input_);
    form_fields.push_back(remote_root_input_);
    form_fields.push_back(identity_file_input_);
    form_fields.push_back(ssh_config_host_input_);
    form_fields.push_back(account_label_input_);
    form_fields.push_back(client_id_input_);
    form_fields.push_back(client_secret_input_);
    form_fields.push_back(tenant_id_input_);
    form_fields.push_back(root_folder_id_input_);
    form_fields.push_back(site_id_input_);
    form_fields.push_back(drive_id_input_);
    form_fields.push_back(app_key_input_);
    form_fields.push_back(app_secret_input_);
    form_fields.push_back(help_button_);
    form_fields.push_back(authorize_button_);

    ftxui::Components form_area;
    form_area.push_back(list_component_);
    form_area.push_back(ftxui::Container::Vertical(form_fields));

    ftxui::Components main_children;
    main_children.push_back(ftxui::Container::Horizontal(toolbar_buttons));
    main_children.push_back(ftxui::Container::Horizontal(type_buttons));
    main_children.push_back(ftxui::Container::Horizontal(form_area));
    auto main_container = ftxui::Container::Vertical(main_children);
    main_container = ftxui::CatchEvent(main_container, [this](ftxui::Event event) {
        if (event == ftxui::Event::ArrowDown || event == ftxui::Event::ArrowUp) {
            auto inputs = GetVisibleInputs();
            if (inputs.empty()) {
                return false;
            }
            int focused = FindFocusedInputIndex(inputs);
            if (event == ftxui::Event::ArrowDown) {
                int next = (focused + 1) % static_cast<int>(inputs.size());
                inputs[next]->TakeFocus();
            } else {
                int prev = (focused <= 0)
                    ? static_cast<int>(inputs.size()) - 1
                    : focused - 1;
                inputs[prev]->TakeFocus();
            }
            return true;
        }
        return false;
    });

    help_close_button_ = MakeTextButton("Close", [this] { CloseHelp(); });
    copy_url_button_ = MakeTextButton("Copy URL", [this] { CopyHelpUrl(); });
    ftxui::Components help_children;
    help_children.push_back(copy_url_button_);
    help_children.push_back(help_close_button_);
    help_container_ = ftxui::Container::Horizontal(help_children);

    ftxui::Components authorize_children;
    authorize_children.push_back(redirect_url_input_);
    authorize_children.push_back(submit_redirect_button_);
    authorize_children.push_back(cancel_authorize_button_);
    authorize_container_ = ftxui::Container::Vertical({
        ftxui::Container::Horizontal(authorize_children),
    });

    ftxui::Components tab_children;
    tab_children.push_back(main_container);
    tab_children.push_back(help_container_);
    tab_children.push_back(authorize_container_);
    container_ = ftxui::Container::Tab(
        tab_children, &help_layer_index_);
    Reload();
}

ftxui::Component RemoteConnectionsModalContent::MakeTextButton(
    std::string label,
    std::function<void()> on_click) {
    ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
    option.label = std::move(label);
    option.on_click = std::move(on_click);
    option.transform = [this](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        ftxui::Element button = ftxui::text(BracketLabel(state.label));
        if (state.focused || state.active) {
            return button |
                ftxui::bgcolor(theme.modal_selected_item_bg) |
                ftxui::color(theme.modal_selected_item_fg);
        }
        return button | ftxui::color(theme.modal_accent);
    };
    return ftxui::Button(option);
}

void RemoteConnectionsModalContent::Open() {
    Reload();
    help_active_ = false;
    authorize_pending_ = false;
    help_layer_index_ = 0;
    authorize_layer_index_ = 0;
    if (name_input_) {
        name_input_->TakeFocus();
    }
}

void RemoteConnectionsModalContent::Close() {
    output_.clear();
    status_ = "Ready.";
    status_is_error_ = false;
    help_active_ = false;
    authorize_pending_ = false;
    help_layer_index_ = 0;
    authorize_layer_index_ = 0;
}

void RemoteConnectionsModalContent::Reload() {
    std::string error;
    if (config_store_ && !config_store_->Load(error)) {
        SetStatus(error, true);
    }
    connections_ = config_store_ ? config_store_->Connections() : std::vector<RemoteConnectionConfig>{};
    connection_boxes_.resize(connections_.size());
    if (connections_.empty()) {
        selected_connection_ = 0;
        id_value_.clear();
        name_value_.clear();
        type_value_ = "sftp";
        host_value_.clear();
        port_value_ = "22";
        user_value_.clear();
        remote_root_value_ = "/";
        identity_file_value_.clear();
        ssh_config_host_value_.clear();
        account_label_value_.clear();
        client_id_value_.clear();
        client_secret_value_.clear();
        tenant_id_value_.clear();
        token_file_value_.clear();
        root_folder_id_value_.clear();
        site_id_value_.clear();
        drive_id_value_.clear();
        app_key_value_.clear();
        app_secret_value_.clear();
        SetStatus("No remote connections. Choose a type and press Add.");
        return;
    }
    selected_connection_ = std::clamp(selected_connection_, 0, static_cast<int>(connections_.size()) - 1);
    LoadSelectedIntoForm();
    SetStatus("Loaded " + std::to_string(connections_.size()) + " connection(s).");
}

ftxui::Element RemoteConnectionsModalContent::Render() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    Element body = vbox({
        hbox({
            add_button_->Render(), text(" "),
            delete_button_->Render(), text(" "),
            save_button_->Render(), text(" "),
            test_button_->Render(), text(" "),
            token_button_->Render(), text(" "),
            reload_button_->Render(), text(" "),
            close_button_->Render(),
        }) | color(theme.modal_accent),
        RenderTypeButtons(),
        separator() | color(theme.modal_border),
        hbox({
            vbox({
                text(" Connections ") | bold | color(theme.modal_accent),
                list_component_->Render() |
                    size(WIDTH, EQUAL, 37) |
                    size(HEIGHT, EQUAL, 14) |
                    border,
            }),
            separator(),
            RenderForm() | size(WIDTH, EQUAL, 70) | yframe | vscroll_indicator,
        }),
        separator() | color(theme.modal_border),
        RenderOutput(),
    }) | bgcolor(theme.modal_background) | color(theme.modal_text_color);

    if (help_active_) {
        ftxui::Element overlay = RenderHelpOverlay();
        ftxui::Element centered = ftxui::vbox(
            ftxui::filler(),
            ftxui::hbox(filler(), overlay, filler()),
            ftxui::filler());
        ftxui::Elements layers;
        layers.push_back(body);
        layers.push_back(centered);
        return dbox(std::move(layers));
    }

    if (authorize_pending_) {
        ftxui::Element auth_overlay = ftxui::vbox({
            ftxui::text(" Paste the redirect URL or access token ") |
                bold | color(theme.modal_accent),
            ftxui::separator() | color(theme.modal_border),
            ftxui::text(" The authorization URL was copied to your clipboard.") | color(theme.modal_text_color),
            ftxui::text(" Open it in your browser, authorize, then copy the") | color(theme.modal_text_color),
            ftxui::text(" redirect URL from the address bar and paste it below.") | color(theme.modal_text_color),
            ftxui::text(" Or paste a raw access token from the Dropbox app console.") | color(theme.modal_text_color),
            ftxui::text(""),
            redirect_url_input_->Render() |
                bgcolor(theme.modal_input_bg) |
                color(theme.modal_input_fg),
            ftxui::text(""),
            ftxui::hbox({
                ftxui::filler(),
                submit_redirect_button_->Render(),
                ftxui::text(" "),
                cancel_authorize_button_->Render(),
            }),
        }) |
            ftxui::size(WIDTH, LESS_THAN, 70) |
            ftxui::border |
            bgcolor(theme.modal_background) |
            color(theme.modal_border) |
            ftxui::clear_under;

        ftxui::Element centered = ftxui::vbox(
            ftxui::filler(),
            ftxui::hbox(filler(), auth_overlay, filler()),
            ftxui::filler());
        ftxui::Elements layers;
        layers.push_back(body);
        layers.push_back(centered);
        return dbox(std::move(layers));
    }

    return body;
}

ftxui::Element RemoteConnectionsModalContent::RenderConnectionList() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    if (connections_.empty()) {
        return vbox({
            filler(),
            text("No connections") | center | color(theme.modal_text_color),
            filler(),
        });
    }

    Elements rows;
    rows.reserve(connections_.size());
    for (size_t index = 0; index < connections_.size(); ++index) {
        const RemoteConnectionConfig& config = connections_[index];
        const bool selected = static_cast<int>(index) == selected_connection_;
        std::string label = config.name.empty() ? config.id : config.name;
        if (label.empty()) {
            label = "Unnamed";
        }
        const std::string target = ConnectionTargetSummary(config);
        Element row = hbox({
            text(" " + TrimForDisplay(label, 17)) | bold,
            filler(),
            text(RemoteConnectionTypeToString(config.type)) | dim,
        }) | reflect(connection_boxes_[index]);
        row = vbox({
            row,
            text("  " + TrimForDisplay(target, 30)) | dim,
        });
        if (selected) {
            row = row |
                bgcolor(theme.modal_selected_item_bg) |
                color(theme.modal_selected_item_fg);
        } else {
            row = row | color(theme.modal_text_color);
        }
        rows.push_back(row);
    }
    return vbox(std::move(rows)) | yframe | vscroll_indicator;
}

ftxui::Element RemoteConnectionsModalContent::RenderTypeButtons() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    auto chip = [&](RemoteConnectionType type, const ftxui::Component& component) {
        Element rendered = component->Render();
        if (CurrentType() == type) {
            rendered = rendered |
                bgcolor(theme.modal_selected_item_bg) |
                color(theme.modal_selected_item_fg);
        }
        return rendered;
    };

    return hbox({
        text(" Type: ") | color(theme.modal_accent),
        chip(RemoteConnectionType::Sftp, sftp_type_button_), text(" "),
        chip(RemoteConnectionType::GoogleDrive, google_type_button_), text(" "),
        chip(RemoteConnectionType::MicrosoftDrive, microsoft_type_button_), text(" "),
        chip(RemoteConnectionType::Dropbox, dropbox_type_button_),
        filler(),
        text(" Current: " + TypeDisplayName(CurrentType()) + " ") | dim | color(theme.modal_text_color),
    });
}

ftxui::Element RemoteConnectionsModalContent::RenderForm() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    auto field = [&](const std::string& label, ftxui::Component component) {
        return hbox({
            text(" " + label) | size(WIDTH, EQUAL, 18) | color(theme.modal_accent),
            component->Render() | flex,
        });
    };
    auto note = [&](const std::string& text_value) {
        return text(" " + text_value) | dim | color(theme.modal_text_color);
    };

    Elements rows;
    rows.push_back(text(" Connection settings ") | bold | color(theme.modal_accent));
    rows.push_back(field("Name", name_input_));

    switch (CurrentType()) {
        case RemoteConnectionType::Sftp:
            rows.push_back(field("Host", host_input_));
            rows.push_back(field("Port", port_input_));
            rows.push_back(field("User", user_input_));
            rows.push_back(field("Remote root", remote_root_input_));
            rows.push_back(field("Identity file", identity_file_input_));
            rows.push_back(field("SSH config", ssh_config_host_input_));
            rows.push_back(note("Passwords are not stored. Use SSH keys, ssh-agent or ~/.ssh/config."));
            rows.push_back(note("SFTP is the active file-manager backend."));
            break;
        case RemoteConnectionType::GoogleDrive:
            rows.push_back(field("Account label", account_label_input_));
            rows.push_back(field("Client ID", client_id_input_));
            rows.push_back(field("Client secret", client_secret_input_));
            rows.push_back(field("Root folder ID", root_folder_id_input_));
            rows.push_back(note("Press Authorize to open Google login in your browser."));
            rows.push_back(note("Leave Root folder ID empty to use the Drive root. Google Drive file operations are active now."));
            rows.push_back(ftxui::hbox({
                ftxui::filler(),
                authorize_button_->Render(),
            }));
            break;
        case RemoteConnectionType::MicrosoftDrive:
            rows.push_back(field("Account label", account_label_input_));
            rows.push_back(field("Tenant ID", tenant_id_input_));
            rows.push_back(field("Client ID", client_id_input_));
            rows.push_back(field("Client secret", client_secret_input_));
            rows.push_back(field("Site ID", site_id_input_));
            rows.push_back(field("Drive ID", drive_id_input_));
            rows.push_back(field("Remote root", remote_root_input_));
            rows.push_back(note("Use Tenant ID 'common' for personal accounts, or a tenant id for organization accounts."));
            rows.push_back(note("Press Authorize to open Microsoft login in your browser."));
            rows.push_back(ftxui::hbox({
                ftxui::filler(),
                authorize_button_->Render(),
            }));
            break;
        case RemoteConnectionType::Dropbox:
            rows.push_back(field("Account label", account_label_input_));
            rows.push_back(field("App key", app_key_input_));
            rows.push_back(field("App secret", app_secret_input_));
            rows.push_back(field("Remote root", remote_root_input_));
            rows.push_back(note("Press Authorize to open Dropbox login in your browser."));
            rows.push_back(note("Remote root can be / or /TextLT. Dropbox file operations are active now."));
            rows.push_back(ftxui::hbox({
                ftxui::filler(),
                authorize_button_->Render(),
                text(" "),
                help_button_->Render(),
            }));
            break;
    }

    return vbox(std::move(rows));
}

ftxui::Element RemoteConnectionsModalContent::RenderOutput() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    Elements rows;
    rows.push_back(
        text(" " + status_) |
        (status_is_error_ ? color(ftxui::Color::Red) : color(theme.modal_text_color)));

    if (!output_.empty()) {
        std::istringstream lines(output_);
        std::string line;
        int count = 0;
        while (count < 6 && std::getline(lines, line)) {
            rows.push_back(text(" " + TrimForDisplay(line, 104)) | dim | color(theme.modal_text_color));
            ++count;
        }
    }

    return vbox(std::move(rows)) |
        size(HEIGHT, EQUAL, 7) |
        yframe |
        vscroll_indicator |
        bgcolor(theme.modal_input_bg);
}

bool RemoteConnectionsModalContent::HandleListEvent(ftxui::Event event) {
    if (event == ftxui::Event::ArrowDown) {
        SelectConnection(selected_connection_ + 1);
        return true;
    }
    if (event == ftxui::Event::ArrowUp) {
        SelectConnection(selected_connection_ - 1);
        return true;
    }
    if (event.is_mouse() &&
        event.mouse().button == ftxui::Mouse::Left &&
        event.mouse().motion == ftxui::Mouse::Pressed) {
        const int index = EntryIndexAtMouse(event.mouse());
        if (index >= 0) {
            SelectConnection(index);
            return true;
        }
    }
    return false;
}

int RemoteConnectionsModalContent::EntryIndexAtMouse(const ftxui::Mouse& mouse) const {
    for (size_t index = 0; index < connection_boxes_.size(); ++index) {
        if (connection_boxes_[index].Contain(mouse.x, mouse.y)) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

RemoteConnectionType RemoteConnectionsModalContent::CurrentType() const {
    return RemoteConnectionTypeFromString(type_value_);
}

void RemoteConnectionsModalContent::SelectType(RemoteConnectionType type) {
    type_value_ = RemoteConnectionTypeToString(type);
    ApplyTypeDefaults(type);
    output_.clear();
    SetStatus("Editing " + TypeDisplayName(type) + " connection fields.");
    help_active_ = false;
    authorize_pending_ = false;
    help_layer_index_ = 0;
    authorize_layer_index_ = 0;
}

void RemoteConnectionsModalContent::ApplyTypeDefaults(RemoteConnectionType type) {
    if (remote_root_value_.empty()) {
        remote_root_value_ = "/";
    }
    switch (type) {
        case RemoteConnectionType::Sftp:
            if (port_value_.empty() || port_value_ == "0") {
                port_value_ = "22";
            }
            break;
        case RemoteConnectionType::GoogleDrive:
            if (token_file_value_.empty()) {
                token_file_value_ = SuggestedTokenFile(type);
            }
            break;
        case RemoteConnectionType::MicrosoftDrive:
            if (tenant_id_value_.empty()) {
                tenant_id_value_ = "common";
            }
            if (token_file_value_.empty()) {
                token_file_value_ = SuggestedTokenFile(type);
            }
            break;
        case RemoteConnectionType::Dropbox:
            if (token_file_value_.empty()) {
                token_file_value_ = SuggestedTokenFile(type);
            }
            break;
    }
}

void RemoteConnectionsModalContent::LoadSelectedIntoForm() {
    if (connections_.empty() || selected_connection_ < 0 ||
        selected_connection_ >= static_cast<int>(connections_.size())) {
        return;
    }
    const RemoteConnectionConfig& config = connections_[selected_connection_];
    id_value_ = config.id;
    name_value_ = config.name;
    type_value_ = RemoteConnectionTypeToString(config.type);
    host_value_ = config.host;
    port_value_ = std::to_string(config.port <= 0 ? 22 : config.port);
    user_value_ = config.user;
    remote_root_value_ = config.remote_root.empty() ? "/" : config.remote_root;
    identity_file_value_ = config.identity_file;
    ssh_config_host_value_ = config.ssh_config_host;
    account_label_value_ = config.account_label;
    client_id_value_ = config.client_id;
    client_secret_value_ = config.client_secret;
    tenant_id_value_ = config.tenant_id;
    token_file_value_ = config.token_file;
    root_folder_id_value_ = config.root_folder_id;
    site_id_value_ = config.site_id;
    drive_id_value_ = config.drive_id;
    app_key_value_ = config.app_key;
    app_secret_value_ = config.app_secret;
    ApplyTypeDefaults(config.type);

    name_cursor_ = static_cast<int>(name_value_.size());
    host_cursor_ = static_cast<int>(host_value_.size());
    port_cursor_ = static_cast<int>(port_value_.size());
    user_cursor_ = static_cast<int>(user_value_.size());
    remote_root_cursor_ = static_cast<int>(remote_root_value_.size());
    identity_file_cursor_ = static_cast<int>(identity_file_value_.size());
    ssh_config_host_cursor_ = static_cast<int>(ssh_config_host_value_.size());
    account_label_cursor_ = static_cast<int>(account_label_value_.size());
    client_id_cursor_ = static_cast<int>(client_id_value_.size());
    client_secret_cursor_ = static_cast<int>(client_secret_value_.size());
    tenant_id_cursor_ = static_cast<int>(tenant_id_value_.size());
    root_folder_id_cursor_ = static_cast<int>(root_folder_id_value_.size());
    site_id_cursor_ = static_cast<int>(site_id_value_.size());
    drive_id_cursor_ = static_cast<int>(drive_id_value_.size());
    app_key_cursor_ = static_cast<int>(app_key_value_.size());
    app_secret_cursor_ = static_cast<int>(app_secret_value_.size());
}

void RemoteConnectionsModalContent::SaveFormToSelected() {
    if (!config_store_) {
        SetStatus("Remote config store is not available.", true);
        return;
    }
    RemoteConnectionConfig config = FormConfig();
    if (config.id.empty()) {
        config.id = MakeRemoteConnectionId();
    }
    if (config.name.empty()) {
        SetStatus("Connection name is required.", true);
        return;
    }
    if (config.type == RemoteConnectionType::Sftp &&
        config.ssh_config_host.empty() && config.host.empty()) {
        SetStatus("SFTP connection needs Host or SSH config alias.", true);
        return;
    }
    if (IsCloudRemoteConnectionType(config.type) && config.token_file.empty()) {
        config.token_file = SuggestedTokenFile(config.type);
        token_file_value_ = config.token_file;
    }
    config_store_->AddOrUpdate(config);
    std::string error;
    if (!config_store_->Save(error)) {
        SetStatus(error, true);
        return;
    }
    Reload();
    for (size_t index = 0; index < connections_.size(); ++index) {
        if (connections_[index].id == config.id) {
            SelectConnection(static_cast<int>(index));
            break;
        }
    }
    if (config.type == RemoteConnectionType::Sftp) {
        SetStatus("Saved connection: " + config.name);
    } else {
        SetStatus("Saved " + TypeDisplayName(config.type) + " configuration: " + config.name);
    }
}

void RemoteConnectionsModalContent::AddConnection() {
    const RemoteConnectionType type = CurrentType();
    id_value_ = MakeRemoteConnectionId();
    name_value_ = DefaultNameForType(type);
    type_value_ = RemoteConnectionTypeToString(type);
    host_value_.clear();
    port_value_ = "22";
    user_value_.clear();
    remote_root_value_ = "/";
    identity_file_value_.clear();
    ssh_config_host_value_.clear();
    account_label_value_.clear();
    client_id_value_.clear();
    client_secret_value_.clear();
    tenant_id_value_.clear();
    token_file_value_.clear();
    root_folder_id_value_.clear();
    site_id_value_.clear();
    drive_id_value_.clear();
    app_key_value_.clear();
    app_secret_value_.clear();
    ApplyTypeDefaults(type);
    SetStatus("Fill " + TypeDisplayName(type) + " fields and press Save.");
}

void RemoteConnectionsModalContent::DeleteSelected() {
    if (!config_store_ || connections_.empty() || selected_connection_ < 0 ||
        selected_connection_ >= static_cast<int>(connections_.size())) {
        SetStatus("No connection selected.", true);
        return;
    }
    const std::string id = connections_[selected_connection_].id;
    const std::string name = connections_[selected_connection_].name;
    if (!config_store_->RemoveById(id)) {
        SetStatus("Connection was not removed.", true);
        return;
    }
    std::string error;
    if (!config_store_->Save(error)) {
        SetStatus(error, true);
        return;
    }
    Reload();
    SetStatus("Deleted connection: " + name);
}

void RemoteConnectionsModalContent::TestSelected() {
    RemoteConnectionConfig config = FormConfig();
    if (config.type != RemoteConnectionType::Sftp && config.token_file.empty()) {
        config.token_file = SuggestedTokenFile(config.type);
        token_file_value_ = config.token_file;
    }

    if (config.type == RemoteConnectionType::Dropbox) {
        RemoteDropboxProvider provider;
        std::string error;
        if (!provider.Connect(config, error)) {
            output_ = DescribeRemoteOAuthTokenStatus(config) + "\n" + error;
            SetStatus("Dropbox token is not ready.", true);
            return;
        }
        std::string output;
        if (!provider.TestConnection(output, error)) {
            output_ = error;
            SetStatus("Dropbox connection test failed.", true);
            return;
        }
        output_ = output;
        SetStatus("Dropbox connection test succeeded.");
        return;
    }

    if (config.type == RemoteConnectionType::GoogleDrive) {
        RemoteGoogleDriveProvider provider;
        std::string error;
        if (!provider.Connect(config, error)) {
            output_ = DescribeRemoteOAuthTokenStatus(config) + "\n" + error;
            SetStatus("Google Drive token is not ready.", true);
            return;
        }
        std::string output;
        if (!provider.TestConnection(output, error)) {
            output_ = error;
            SetStatus("Google Drive connection test failed.", true);
            return;
        }
        output_ = output;
        SetStatus("Google Drive connection test succeeded.");
        return;
    }

    if (config.type == RemoteConnectionType::MicrosoftDrive) {
        RemoteMicrosoftDriveProvider provider;
        std::string error;
        if (!provider.Connect(config, error)) {
            output_ = DescribeRemoteOAuthTokenStatus(config) + "\n" + error;
            SetStatus("Microsoft token is not ready.", true);
            return;
        }
        std::string output;
        if (!provider.TestConnection(output, error)) {
            output_ = error;
            SetStatus("Microsoft connection test failed.", true);
            return;
        }
        output_ = output;
        SetStatus("Microsoft connection test succeeded.");
        return;
    }

    if (config.type != RemoteConnectionType::Sftp) {
        output_ = DescribeRemoteOAuthTokenStatus(config) +
            "\nOAuth login and REST file operations for this provider will use this token file in a later API patch.";
        SetStatus("Checked token file for " + TypeDisplayName(config.type) + ".");
        return;
    }

    RemoteSftpProvider provider;
    std::string error;
    if (!provider.Connect(config, error)) {
        output_.clear();
        SetStatus(error, true);
        return;
    }
    std::string output;
    if (!provider.TestConnection(output, error)) {
        output_ = error;
        SetStatus("Connection test failed.", true);
        return;
    }
    output_ = output;
    SetStatus("Connection test succeeded.");
}

void RemoteConnectionsModalContent::PrepareTokenFile() {
    RemoteConnectionConfig config = FormConfig();
    if (!IsCloudRemoteConnectionType(config.type)) {
        output_ = "SFTP uses SSH keys / ssh-agent / ~/.ssh/config and does not need an OAuth token file.";
        SetStatus("Token files are only used by Google, Microsoft, and Dropbox.");
        return;
    }
    if (config.token_file.empty()) {
        config.token_file = SuggestedTokenFile(config.type);
        token_file_value_ = config.token_file;
    }

    const std::filesystem::path token_path = ExpandRemoteUserPath(config.token_file);
    RemoteOAuthTokenStore token_store(token_path);
    RemoteOAuthToken token;
    std::string error;
    if (token_store.Exists()) {
        if (!token_store.Load(token, error)) {
            output_ = error;
            SetStatus("Token file is invalid.", true);
            return;
        }
        output_ = DescribeRemoteOAuthTokenStatus(config);
        SetStatus("Token file already exists.");
        return;
    }

    token.provider = RemoteTokenProviderName(config.type);
    token.account_label = config.account_label.empty() ? config.name : config.account_label;
    token.token_type = "Bearer";
    if (!token_store.Save(token, error)) {
        output_ = error;
        SetStatus("Cannot create token file.", true);
        return;
    }

    output_ = "Created placeholder token file:\n" + token_path.string() +
        "\nThe next OAuth/API patch will fill access_token and refresh_token.";
    SetStatus("Token file prepared for " + TypeDisplayName(config.type) + ".");
}

void RemoteConnectionsModalContent::AuthorizeConnection() {
    RemoteConnectionConfig config = FormConfig();
    if (!IsCloudRemoteConnectionType(config.type)) {
        SetStatus("Authorization is only available for Google, Microsoft, and Dropbox.", true);
        return;
    }

    OAuthFlowConfig oauth;

    switch (config.type) {
        case RemoteConnectionType::Dropbox:
            oauth.authorize_url = "https://www.dropbox.com/oauth2/authorize";
            oauth.token_url = "https://api.dropboxapi.com/oauth2/token";
            oauth.client_id = config.app_key;
            oauth.client_secret = config.app_secret;
            oauth.scope = "account_info.read files.metadata.read files.content.read files.content.write";
            break;
        case RemoteConnectionType::GoogleDrive:
            oauth.authorize_url = "https://accounts.google.com/o/oauth2/v2/auth";
            oauth.token_url = "https://oauth2.googleapis.com/token";
            oauth.client_id = config.client_id;
            oauth.client_secret = config.client_secret;
            oauth.scope = "https://www.googleapis.com/auth/drive";
            break;
        case RemoteConnectionType::MicrosoftDrive: {
            const std::string tenant = config.tenant_id.empty() ? "common" : config.tenant_id;
            oauth.authorize_url = "https://login.microsoftonline.com/" + tenant + "/oauth2/v2.0/authorize";
            oauth.token_url = "https://login.microsoftonline.com/" + tenant + "/oauth2/v2.0/token";
            oauth.client_id = config.client_id;
            oauth.client_secret = config.client_secret;
            oauth.scope = "offline_access Files.ReadWrite.All";
            break;
        }
        default:
            return;
    }

    if (oauth.client_id.empty() || oauth.client_secret.empty()) {
        SetStatus("Fill in the credentials (app key/secret or client id/secret) first.", true);
        return;
    }

    if (config.token_file.empty()) {
        config.token_file = SuggestedTokenFile(config.type);
        token_file_value_ = config.token_file;
    }

    OAuthFlow flow;
    std::string auth_url = flow.BuildAuthorizeUrl(oauth);

    if (write_clipboard_) {
        write_clipboard_(auth_url);
    }

    pending_oauth_config_ = oauth;
    authorize_pending_ = true;
    authorize_layer_index_ = 2;
    redirect_url_value_.clear();
    if (redirect_url_input_) {
        redirect_url_input_->TakeFocus();
    }

    output_ = "Authorization URL copied to clipboard.\n"
              "Steps:\n"
              "1. In Dropbox app settings, add redirect URI:\n"
              "  " + oauth.redirect_uri + "\n"
              "2. Add scopes: account_info.read files.metadata.read\n"
              "   files.content.read files.content.write\n"
              "3. Generate access token on the Dropbox site\n"
              "4. Open the URL below in your browser, authorize,\n"
              "   and paste the redirect URL from the address bar.\n"
              "URL: " + auth_url;
    SetStatus("Paste the redirect URL from your browser and press Submit.");
}

void RemoteConnectionsModalContent::SubmitRedirectUrl() {
    if (!authorize_pending_) {
        return;
    }

    const std::string& input = redirect_url_value_;

    // Check if the input looks like a raw access token (not a URL).
    // Dropbox generated access tokens are short strings like "sl.xxxxx..."
    // with no scheme or query parameters.  Authorization redirect URLs contain
    // "://".
    const bool looks_like_url = input.find("://") != std::string::npos;

    authorize_pending_ = false;
    authorize_layer_index_ = 0;

    RemoteConnectionConfig config = FormConfig();
    if (config.token_file.empty()) {
        config.token_file = SuggestedTokenFile(config.type);
        token_file_value_ = config.token_file;
    }

    std::string account = config.account_label.empty() ? config.name : config.account_label;
    const std::string provider_name = RemoteTokenProviderName(config.type);

    if (looks_like_url) {
        // Normal OAuth redirect URL — extract the authorization code and exchange it.
        OAuthFlow flow;
        std::string code = flow.ExtractCodeFromRedirectUrl(input);
        if (code.empty()) {
            SetStatus("Could not extract authorization code from the URL. Paste the full redirect URL.", true);
            return;
        }

        SetStatus("Exchanging authorization code for tokens...");

        OAuthTokenExchangeResult exchange = flow.ExchangeCodeForToken(pending_oauth_config_, code);
        if (!exchange.ok) {
            output_ = exchange.error;
            SetStatus("Token exchange failed: " + exchange.error, true);
            return;
        }

        if (!flow.SaveToken(pending_oauth_config_, provider_name,
                            account, exchange, config.token_file)) {
            SetStatus("Failed to save token file.", true);
            return;
        }
    } else {
        // Treat the input as a raw access token (e.g. from the Dropbox app console).
        if (input.empty()) {
            SetStatus("Paste a redirect URL or a raw access token.", true);
            return;
        }

        RemoteOAuthToken token;
        token.provider = provider_name;
        token.account_label = account;
        token.access_token = input;
        token.token_type = "Bearer";

        RemoteOAuthTokenStore store(ExpandRemoteUserPath(config.token_file));
        std::string error;
        if (!store.Save(token, error)) {
            output_ = error;
            SetStatus("Failed to save token file.", true);
            return;
        }
    }

    output_ = DescribeRemoteOAuthTokenStatus(config);
    SetStatus("Authorization complete for " + TypeDisplayName(config.type) + ".");
}

void RemoteConnectionsModalContent::OpenHelp() {
    help_active_ = true;
    help_layer_index_ = 1;
    if (help_close_button_) {
        help_close_button_->TakeFocus();
    }
}

void RemoteConnectionsModalContent::CloseHelp() {
    help_active_ = false;
    help_layer_index_ = 0;
}

void RemoteConnectionsModalContent::CopyHelpUrl() {
    if (write_clipboard_) {
        write_clipboard_("https://www.dropbox.com/developers/apps");
        SetStatus("Copied Dropbox developer apps URL to clipboard.");
    } else {
        SetStatus("Clipboard is not available.", true);
    }
}

bool RemoteConnectionsModalContent::HandleHelpEvent(ftxui::Event event) {
    if (event == ftxui::Event::Escape) {
        CloseHelp();
        return true;
    }
    return help_container_ ? help_container_->OnEvent(event) : true;
}

bool RemoteConnectionsModalContent::HandleEvent(ftxui::Event event) {
    if (help_active_) {
        return HandleHelpEvent(std::move(event));
    }

    if (authorize_pending_) {
        if (event == ftxui::Event::Escape) {
            authorize_pending_ = false;
            authorize_layer_index_ = 0;
            SetStatus("Authorization cancelled.");
            return true;
        }
        return authorize_container_ ? authorize_container_->OnEvent(event) : false;
    }

    if (event == ftxui::Event::ArrowDown || event == ftxui::Event::ArrowUp) {
        auto inputs = GetVisibleInputs();
        if (inputs.empty()) {
            return false;
        }
        int focused = FindFocusedInputIndex(inputs);
        if (event == ftxui::Event::ArrowDown) {
            int next = (focused + 1) % static_cast<int>(inputs.size());
            inputs[next]->TakeFocus();
        } else {
            int prev = (focused <= 0)
                ? static_cast<int>(inputs.size()) - 1
                : focused - 1;
            inputs[prev]->TakeFocus();
        }
        return true;
    }

    return false;
}

std::vector<ftxui::Component> RemoteConnectionsModalContent::GetVisibleInputs() {
    std::vector<ftxui::Component> inputs;
    inputs.push_back(name_input_);

    switch (CurrentType()) {
        case RemoteConnectionType::Sftp:
            inputs.push_back(host_input_);
            inputs.push_back(port_input_);
            inputs.push_back(user_input_);
            inputs.push_back(remote_root_input_);
            inputs.push_back(identity_file_input_);
            inputs.push_back(ssh_config_host_input_);
            break;
        case RemoteConnectionType::GoogleDrive:
            inputs.push_back(account_label_input_);
            inputs.push_back(client_id_input_);
            inputs.push_back(client_secret_input_);
            inputs.push_back(root_folder_id_input_);
            break;
        case RemoteConnectionType::MicrosoftDrive:
            inputs.push_back(account_label_input_);
            inputs.push_back(tenant_id_input_);
            inputs.push_back(client_id_input_);
            inputs.push_back(client_secret_input_);
            inputs.push_back(site_id_input_);
            inputs.push_back(drive_id_input_);
            inputs.push_back(remote_root_input_);
            break;
        case RemoteConnectionType::Dropbox:
            inputs.push_back(account_label_input_);
            inputs.push_back(app_key_input_);
            inputs.push_back(app_secret_input_);
            inputs.push_back(remote_root_input_);
            break;
    }
    return inputs;
}

int RemoteConnectionsModalContent::FindFocusedInputIndex(
    const std::vector<ftxui::Component>& inputs) {
    for (int i = 0; i < static_cast<int>(inputs.size()); ++i) {
        if (inputs[i] && inputs[i]->Focused()) {
            return i;
        }
    }
    return -1;
}

void RemoteConnectionsModalContent::SelectConnection(int index) {
    if (connections_.empty()) {
        selected_connection_ = 0;
        return;
    }
    selected_connection_ = std::clamp(index, 0, static_cast<int>(connections_.size()) - 1);
    LoadSelectedIntoForm();
}

RemoteConnectionConfig RemoteConnectionsModalContent::FormConfig() const {
    RemoteConnectionConfig config;
    config.id = id_value_;
    config.name = name_value_;
    config.type = CurrentType();
    config.host = host_value_;
    config.port = ParsePort(port_value_);
    config.user = user_value_;
    config.remote_root = NormalizeRemoteDirectory(remote_root_value_);
    config.identity_file = identity_file_value_;
    config.ssh_config_host = ssh_config_host_value_;
    config.account_label = account_label_value_;
    config.client_id = client_id_value_;
    config.client_secret = client_secret_value_;
    config.tenant_id = tenant_id_value_;
    config.token_file = token_file_value_;
    config.root_folder_id = root_folder_id_value_;
    config.site_id = site_id_value_;
    config.drive_id = drive_id_value_;
    config.app_key = app_key_value_;
    config.app_secret = app_secret_value_;
    return config;
}

std::string RemoteConnectionsModalContent::SuggestedTokenFile(RemoteConnectionType type) const {
    std::string stable_name = account_label_value_;
    if (stable_name.empty()) {
        stable_name = name_value_;
    }
    if (stable_name.empty()) {
        stable_name = id_value_;
    }
    return DefaultRemoteTokenPath(type, stable_name).string();
}

void RemoteConnectionsModalContent::SetStatus(std::string status, bool is_error) {
    status_ = std::move(status);
    status_is_error_ = is_error;
}

ftxui::Element RemoteConnectionsModalContent::RenderHelpOverlay() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    Elements rows;
    rows.push_back(text(" Dropbox help ") | bold | color(theme.modal_accent));
    rows.push_back(separator() | color(theme.modal_border));
    rows.push_back(text(" To connect to Dropbox:") | color(theme.modal_text_color));
    rows.push_back(text(" 1. Go to the Dropbox Developer Console.") | color(theme.modal_text_color));
    rows.push_back(text(" 2. Create a new app with full access.") | color(theme.modal_text_color));
    rows.push_back(text(" 3. Copy the App key and App secret.") | color(theme.modal_text_color));
    rows.push_back(text(" 4. Paste them into the fields above.") | color(theme.modal_text_color));
    rows.push_back(text(" 5. Press Token to generate the token file.") | color(theme.modal_text_color));
    rows.push_back(text(" 6. Press Authorize to paste a token or redirect URL.") | color(theme.modal_text_color));
    rows.push_back(text("    You can paste a generated access token from the app console.") | color(theme.modal_text_color));
    rows.push_back(text(" 7. Press Test to verify the connection.") | color(theme.modal_text_color));
    rows.push_back(text(""));
    rows.push_back(text(" Dropbox app console: dropbox.com/developers/apps") |
                   color(theme.modal_text_color));
    rows.push_back(text(""));
    rows.push_back(hbox({
        filler(),
        copy_url_button_ ? copy_url_button_->Render() : text(""),
        text(" "),
        help_close_button_ ? help_close_button_->Render() : text(""),
    }));
    return vbox(std::move(rows)) |
        size(WIDTH, LESS_THAN, 60) |
        border |
        bgcolor(theme.modal_background) |
        color(theme.modal_border) |
        clear_under;
}

RemoteConnectionsModal::RemoteConnectionsModal(
    const Theme* theme,
    RemoteConfigStore* config_store,
    WriteClipboardCallback write_clipboard)
    : theme_(theme) {
    content_ = std::make_shared<RemoteConnectionsModalContent>(
        theme_,
        config_store,
        std::move(write_clipboard),
        [this] { Close(); });
    modal_ = std::make_shared<ModalWindow>(content_, theme_, [this] { Close(); });
    modal_->SetBodyFrameScrolling(false);
    modal_->SetFooterText("Choose SFTP, Google, Microsoft, or Dropbox. SFTP, Dropbox, Google Drive, and Microsoft file operations are active now.");
}

ftxui::Component RemoteConnectionsModal::View() const {
    return modal_;
}

void RemoteConnectionsModal::Open() {
    open_ = true;
    content_->Open();
    if (content_) {
        content_->GetMainComponent()->TakeFocus();
    }
}

void RemoteConnectionsModal::Close() {
    open_ = false;
    content_->Close();
}

bool RemoteConnectionsModal::IsOpen() const {
    return open_;
}

bool RemoteConnectionsModal::OnEvent(ftxui::Event event) {
    if (!open_) {
        return false;
    }
    if (content_ && content_->HandleEvent(event)) {
        return true;
    }
    return modal_ ? modal_->OnEvent(std::move(event)) : false;
}

} // namespace textlt
