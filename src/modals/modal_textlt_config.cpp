#include "modals/modal_textlt_config.hpp"

#include <algorithm>

#include "connect_workspace.hpp"
#include "editor/clipboard_controller.hpp"
#include "ftxui/screen/terminal.hpp"
#include "modals/modal_window.hpp"
#include "textlt/connect/textlt_provider.hpp"
#include "ui_button.hpp"
#include "ui_selectable_input.hpp"

namespace textlt {

using namespace ftxui;

class TextLTConfigContent : public IModalContent {
    enum Tab { Connections, Network, Identity, Files };

public:
    TextLTConfigContent(const Theme& theme, connect_ui::ConnectWorkspace& workspace,
                       std::function<bool(const std::string&)> copy, std::function<void()> close)
        : theme_(theme), workspace_(workspace), copy_(std::move(copy)), close_(std::move(close)) {
        connections_tab_ = Action("Connections", [this] { SelectTab(Connections); });
        network_tab_ = Action("Network", [this] { SelectTab(Network); });
        identity_tab_ = Action("Identity", [this] { SelectTab(Identity); });
        files_tab_ = Action("Files", [this] { SelectTab(Files); });
        MenuOption menu_options = MenuOption::Vertical();
        menu_options.entries_option.transform = [this](const EntryState& entry) {
            auto line = text((entry.active ? "› " : "  ") + entry.label);
            return entry.active ? line | bgcolor(theme_.modal_selected_item_bg) |
                color(theme_.modal_selected_item_fg) : line;
        };
        connections_ = Menu(&labels_, &selected_, std::move(menu_options));
        import_json_ = InputField(json_, "Paste the other device's JSON", true);
        import_ = Action("Save connection", [this] {
            std::size_t index = 0;
            const auto result = workspace_.ImportNativeConnection(json_, index);
            status_ = result.reason;
            if (!result.accepted) return;
            selected_ = static_cast<int>(index);
            json_.clear();
            RefreshConnections();
            connections_->TakeFocus();
        }, ButtonRole::Primary, [this] { return !json_.empty(); });
        connect_ = Action("Connect", [this] {
            const auto result = workspace_.ActivateNativeConnection(static_cast<std::size_t>(selected_));
            status_ = result.reason;
            if (result.accepted) close_();
        }, ButtonRole::Primary, [this] { return SelectedConnection() != nullptr; });
        name_ = InputField(device_name_, "Device name");
        save_name_ = Action("Save name", [this] { SaveDeviceName(); }, ButtonRole::Utility,
                            [this] { return device_name_ != identity_.name; });
        peer_id_input_ = InputField(peer_id_, "Peer ID");
        save_peer_id_ = Action("Save ID", [this] {
            const auto result = workspace_.SaveNativePeerId(peer_id_);
            status_ = result.reason;
            if (result.accepted) identity_ = workspace_.NativeIdentity();
        }, ButtonRole::Utility, [this] { return peer_id_ != workspace_.Configuration().local_id; });
        copy_fingerprint_ = Action("Copy fingerprint", [this] {
            if (RefreshFingerprint()) {
                status_ = copy_(identity_.fingerprint) ? "Fingerprint copied." : ClipboardController::kWriteFailed;
            }
        });
        address_ = InputField(shared_address_, "IPv4 address");
        port_ = InputField(shared_port_, "TCP port");
        auto& config = workspace_.Configuration();
        listen_ip_ = InputField(config.listen_ip, "Listen IPv4 address", false,
                                [this] { SaveNetworkSettings(); });
        listen_port_ = InputField(config.listen_port, "Listen port (default 53327, 0 = off)", false,
                                  [this] { SaveNetworkSettings(); });
        discovery_enabled_ = NetworkCheckbox("Enable LAN discovery", config.discovery);
        discovery_ip_ = InputField(config.discovery_ip,
            "Discovery interface IPv4 address (blank uses listen address)", false,
            [this] { SaveNetworkSettings(); });
        certificate_input_ = InputField(certificate_, "Certificate PEM path");
        private_key_input_ = InputField(private_key_, "Private key PEM path");
        save_paths_ = Action("Save paths", [this] {
            const auto result = workspace_.SaveNativeIdentityPaths(certificate_, private_key_);
            status_ = result.reason;
            if (result.accepted) identity_ = workspace_.NativeIdentity();
        }, ButtonRole::Primary, [this] {
            const auto& config = workspace_.Configuration();
            return certificate_ != config.certificate || private_key_ != config.private_key;
        });
        copy_json_ = Action("Copy identity", [this] {
            if (device_name_ != identity_.name && !SaveDeviceName()) return;
            std::string encoded;
            const auto result = workspace_.ExportNativeIdentity(shared_address_, shared_port_, encoded);
            status_ = !result.accepted ? result.reason
                 : copy_(encoded) ? "Identity copied." : ClipboardController::kWriteFailed;
        }, ButtonRole::Primary);
        regenerate_ = Action("Regenerate identity", [this] {
            confirming_identity_ = 1;
            cancel_regenerate_->TakeFocus();
        });
        info_button_ = Action("Info", [this] {
            confirming_identity_ = 2;
            info_scroll_ = 0;
            close_info_->TakeFocus();
        });
        close_info_ = Action("Close", [this] {
            confirming_identity_ = 0;
            info_button_->TakeFocus();
        }, ButtonRole::Cancel);
        confirm_regenerate_ = Action("Regenerate now", [this] {
            const auto result = workspace_.RegenerateNativeIdentity(device_name_);
            status_ = result.reason;
            if (result.accepted) {
                identity_ = workspace_.NativeIdentity();
                device_name_ = identity_.name;
                peer_id_ = identity_.peer_id;
                LoadPaths();
            }
            confirming_identity_ = 0;
            (result.accepted ? copy_json_ : name_)->TakeFocus();
        }, ButtonRole::Primary);
        cancel_regenerate_ = Action("Cancel", [this] { CancelRegeneration(); }, ButtonRole::Cancel);
        close_button_ = Action("Close", close_, ButtonRole::Cancel);

        auto saved = Container::Vertical({connections_, connect_});
        auto incoming = Container::Vertical({import_json_, import_});
        auto tabs = Container::Tab({
            Container::Horizontal({saved, incoming}),
            Container::Vertical({listen_ip_, listen_port_, discovery_enabled_, discovery_ip_}),
            Container::Vertical({
                Container::Horizontal({name_, save_name_}),
                Container::Horizontal({peer_id_input_, save_peer_id_}),
                address_, port_,
            }),
            Container::Vertical({certificate_input_, private_key_input_}),
        }, &tab_);
        auto footer = Container::Tab({
            Renderer([] { return emptyElement(); }),
            Renderer([] { return emptyElement(); }),
            Container::Horizontal({copy_fingerprint_, copy_json_, regenerate_}),
            save_paths_,
        }, &tab_);
        auto footer_actions = Container::Horizontal({footer, info_button_});
        auto content = Container::Vertical({
            Container::Horizontal({connections_tab_, network_tab_, identity_tab_, files_tab_}), tabs,
            Container::Horizontal({footer_actions, close_button_}),
        });
        root_ = CatchEvent(Container::Tab({content, Container::Horizontal({cancel_regenerate_, confirm_regenerate_}),
                                           close_info_},
                                         &confirming_identity_), [this](Event event) {
            if (event == Event::Escape) {
                if (confirming_identity_ == 1) CancelRegeneration();
                else if (confirming_identity_ == 2) {
                    confirming_identity_ = 0;
                    info_button_->TakeFocus();
                }
                else close_();
                return true;
            }
            if (event != Event::Tab && event != Event::TabReverse) return false;
            Components controls;
            if (confirming_identity_ == 2) {
                controls = {close_info_};
            } else if (confirming_identity_ == 1) {
                controls = {cancel_regenerate_, confirm_regenerate_};
            } else {
                controls = {connections_tab_, network_tab_, identity_tab_, files_tab_};
                if (tab_ == Connections) {
                    if (!labels_.empty()) controls.insert(controls.end(), {connections_, connect_});
                    controls.insert(controls.end(), {import_json_, import_});
                } else if (tab_ == Identity) {
                    controls.insert(controls.end(), {name_, save_name_, peer_id_input_, save_peer_id_, address_, port_,
                        copy_fingerprint_, copy_json_, regenerate_});
                } else if (tab_ == Network) {
                    controls.insert(controls.end(), {listen_ip_, listen_port_, discovery_enabled_, discovery_ip_});
                } else {
                    controls.insert(controls.end(), {certificate_input_, private_key_input_, save_paths_});
                }
                controls.push_back(info_button_);
                controls.push_back(close_button_);
            }
            const auto current = std::find_if(controls.begin(), controls.end(),
                [](const auto& control) { return control->Focused(); });
            const int count = static_cast<int>(controls.size());
            const int index = current == controls.end() ? 0 : static_cast<int>(current - controls.begin());
            controls[(index + (event == Event::Tab ? 1 : -1) + count) % count]->TakeFocus();
            return true;
        });
    }

    void Open() {
        confirming_identity_ = 0;
        workspace_.RefreshLocalIPv4();
        identity_ = workspace_.NativeIdentity();
        device_name_ = identity_.name;
        peer_id_ = workspace_.Configuration().local_id;
        shared_address_ = identity_.address;
        shared_port_ = identity_.port ? std::to_string(identity_.port) : "";
        LoadPaths();
        RefreshConnections();
        SelectTab(Connections);
    }

    std::string GetTitle() override { return "TextLT Config"; }
    Component GetMainComponent() override { return root_; }
    ModalFrameStyle GetModalFrameStyle() const override { return ModalFrameStyle::TitleInBorder; }
    ModalSizePreference GetModalSizePreference() const override {
        const auto terminal = Terminal::Size();
        return {std::clamp(terminal.dimx - 4, 30, 96), std::clamp(terminal.dimy - 4, 14, 29)};
    }
    bool HasCustomFooter() const override { return true; }
    int GetCustomFooterHeight() const override {
        if (confirming_identity_ == 2) return 3;
        return tab_ == Identity && confirming_identity_ == 0 && Terminal::Size().dimx < 84 ? 4 : 3;
    }

    Element Render() override {
        if (confirming_identity_ == 2) return RenderInfo();
        if (confirming_identity_ == 1) {
            return vbox({
                text("Regenerate TextLT identity?") | bold | color(theme_.modal_accent),
                separator() | color(theme_.modal_border),
                paragraph("This creates a new peer ID, certificate, and fingerprint. It stops any active TextLT connection. "
                          "Share the new JSON with your peers before connecting again."),
                filler(),
            }) | color(theme_.modal_foreground);
        }
        return vbox({
            hbox({connections_tab_->Render(), text(" "), network_tab_->Render(), text(" "),
                  identity_tab_->Render(), text(" "), files_tab_->Render(), filler()}),
            separator() | color(theme_.modal_border),
            tab_ == Connections ? RenderConnections() : tab_ == Network ? RenderNetwork() :
                tab_ == Identity ? RenderIdentity() : RenderFiles(),
        }) | color(theme_.modal_foreground);
    }

    Element RenderCustomFooter() override {
        Elements rows{
            text(status_) | color(theme_.modal_text_color) | xframe,
            separator() | color(theme_.modal_border),
        };
        if (confirming_identity_ == 2) {
            rows.push_back(hbox({filler(), close_info_->Render()}));
        } else if (confirming_identity_ == 1) {
            rows.push_back(hbox({cancel_regenerate_->Render(), filler(), confirm_regenerate_->Render()}));
        } else if (tab_ == Identity && Terminal::Size().dimx < 84) {
            rows.push_back(hbox({copy_fingerprint_->Render(), copy_json_->Render(), filler(), close_button_->Render()}));
            rows.push_back(hbox({regenerate_->Render(), filler(), info_button_->Render()}));
        } else {
            rows.push_back(hbox({
                tab_ == Identity ? copy_fingerprint_->Render() : tab_ == Files ? save_paths_->Render() : emptyElement(),
                tab_ == Identity ? copy_json_->Render() : emptyElement(),
                tab_ == Identity ? regenerate_->Render() : emptyElement(),
                filler(),
                info_button_->Render(),
                close_button_->Render(),
            }));
        }
        return vbox(std::move(rows));
    }

private:
    Component Action(const std::string& label, std::function<void()> callback,
                     ButtonRole role = ButtonRole::Utility, std::function<bool()> enabled = {}) {
        auto option = ButtonOption::Simple();
        option.label = label;
        option.on_click = [callback = std::move(callback), enabled] {
            if (!enabled || enabled()) callback();
        };
        option.transform = [this, enabled, spec = ButtonSpecFromLabel(label, role)](const EntryState& entry) {
            auto style = spec;
            style.enabled = !enabled || enabled();
            return RenderModalFlatButton(theme_, style, entry.focused || entry.active);
        };
        return Button(std::move(option));
    }

    Component InputField(std::string& value, std::string placeholder, bool multiline = false,
                         std::function<void()> on_change = {}) {
        InputOption option;
        option.multiline = multiline;
        option.on_change = std::move(on_change);
        option.transform = [this](InputState state) { return theme_.InputTransform(std::move(state)); };
        return MakeSelectableInput(value, std::move(placeholder), std::move(option));
    }

    Component NetworkCheckbox(const std::string& label, bool& value) {
        auto option = CheckboxOption::Simple();
        option.on_change = [this] { SaveNetworkSettings(); };
        option.transform = [this](const EntryState& state) {
            auto checkbox = text(std::string(state.state ? "[X] " : "[ ] ") + state.label);
            return state.focused ? checkbox | bgcolor(theme_.modal_selected_item_bg) |
                color(theme_.modal_selected_item_fg) : checkbox | color(theme_.modal_text_color);
        };
        return Checkbox(label, &value, std::move(option));
    }

    void SelectTab(Tab tab) {
        tab_ = tab;
        status_.clear();
        if (tab_ == Files) certificate_input_->TakeFocus();
        else if (tab_ == Network) listen_ip_->TakeFocus();
        else if (tab_ == Identity) name_->TakeFocus();
        else if (labels_.empty()) import_json_->TakeFocus();
        else connections_->TakeFocus();
    }

    bool SaveDeviceName() {
        const auto result = workspace_.SaveNativeDeviceName(device_name_);
        status_ = result.reason;
        if (result.accepted) identity_.name = device_name_;
        return result.accepted;
    }

    void SaveNetworkSettings() {
        status_ = workspace_.SaveSettings() ?
            "Network settings saved. Restart TextLT from the Connect panel to apply listener changes." :
            "Could not save network settings.";
    }

    bool RefreshFingerprint() {
        identity_ = workspace_.NativeIdentity();
        if (!identity_.fingerprint.empty()) return true;
        status_ = "Could not read the certificate fingerprint.";
        return false;
    }

    void CancelRegeneration() {
        confirming_identity_ = 0;
        regenerate_->TakeFocus();
    }

    void LoadPaths() {
        const auto& config = workspace_.Configuration();
        certificate_ = config.certificate;
        private_key_ = config.private_key;
    }

    void RefreshConnections() {
        labels_.clear();
        for (const auto& peer : workspace_.NativeConnections()) labels_.push_back(peer.name);
        selected_ = std::clamp(selected_, 0, std::max(0, static_cast<int>(labels_.size()) - 1));
    }

    const connect_ui::NativeConnectionProfile* SelectedConnection() const {
        const auto& peers = workspace_.NativeConnections();
        return selected_ >= 0 && static_cast<std::size_t>(selected_) < peers.size() ? &peers[selected_] : nullptr;
    }

    Element RenderConnections() {
        const auto* peer = SelectedConnection();
        auto left = vbox({
            text("Saved connections") | bold | color(theme_.modal_accent),
            labels_.empty() ? paragraph("No saved connections") | dim
                            : connections_->Render() | vscroll_indicator | yframe | flex,
            peer ? paragraph(peer->address + ":" + std::to_string(peer->port)) : emptyElement(),
            connect_->Render(),
        }) | flex;
        auto right = vbox({
            text("Connection JSON") | bold | color(theme_.modal_accent),
            import_json_->Render() | vscroll_indicator | yframe | flex | borderStyled(LIGHT, theme_.modal_border),
            import_->Render(),
        }) | flex;
        if (Terminal::Size().dimx < 62) {
            return vbox({std::move(left), separator() | color(theme_.modal_border), std::move(right)}) | flex;
        }
        return hbox({std::move(left), separator() | color(theme_.modal_border), std::move(right)}) | flex;
    }

    Element RenderIdentity() {
        const auto line = [this](const std::string& label, const std::string& value) {
            return vbox({text(label) | bold | color(theme_.modal_accent), paragraph(value.empty() ? "—" : value)});
        };
        return vbox({
            text("Device name") | bold | color(theme_.modal_accent),
            hbox({name_->Render() | borderStyled(LIGHT, theme_.modal_border) | flex, save_name_->Render()}),
            text("Peer ID") | bold | color(theme_.modal_accent),
            hbox({peer_id_input_->Render() | borderStyled(LIGHT, theme_.modal_border) | flex, save_peer_id_->Render()}),
            line("Certificate fingerprint", identity_.fingerprint),
            line("JSON version", "1"),
            text("IPv4 address to share") | bold | color(theme_.modal_accent),
            address_->Render() | borderStyled(LIGHT, theme_.modal_border),
            text("TCP port to share") | bold | color(theme_.modal_accent),
            port_->Render() | borderStyled(LIGHT, theme_.modal_border),
        }) | vscroll_indicator | yframe | flex;
    }

    Element RenderNetwork() {
        const auto& local_ip = workspace_.LocalIPv4();
        return vbox({
            text("Local IPv4: " + (local_ip.empty() ? std::string("Unavailable") : local_ip)) |
                bold | color(theme_.modal_accent),
            text("TextLT TCP listener") | bold | color(theme_.modal_accent),
            text("Listen IPv4 address") | bold | color(theme_.modal_accent),
            listen_ip_->Render() | borderStyled(LIGHT, theme_.modal_border),
            text("Listen port (default 53327, 0 = off)") | bold | color(theme_.modal_accent),
            listen_port_->Render() | borderStyled(LIGHT, theme_.modal_border),
            discovery_enabled_->Render(),
            text("LAN discovery") | bold | color(theme_.modal_accent),
            text("Discovery interface IPv4 address (blank uses listen address)") |
                bold | color(theme_.modal_accent),
            discovery_ip_->Render() | borderStyled(LIGHT, theme_.modal_border),
            paragraph("Set the port to 0 to disable the native TCP listener. Restart TextLT from the Connect panel after changing network settings."),
        }) | vscroll_indicator | yframe | flex;
    }

    Element RenderInfo() {
        struct Help {
            const char* title;
            std::vector<std::pair<const char*, const char*>> fields;
        };
        static const Help help[] = {
            {"Connections", {
                {"Saved connections", "Select a saved TextLT identity. The address and port below the list show where this device will connect."},
                {"Connect", "Apply the selected identity as the active TextLT peer. TextLT restarts its native connection with that peer."},
                {"Connection JSON", "Paste the other device's identity JSON. It must include version 1, name, peer ID, address, port, fingerprint, and a pairing_code matching the peer ID and fingerprint."},
                {"Save connection", "Validate and save the pasted identity. The new connection appears in the list on the left."},
            }},
            {"Network", {
                {"Local IPv4", "The detected IPv4 address on this computer."},
                {"Listen IPv4 address", "The local address where TextLT accepts incoming TCP connections. Leave blank to use the detected local address."},
                {"Listen port", "The local TCP port. The default is 53327. Set it to 0 to disable the listener."},
                {"Enable LAN discovery", "Allow TextLT to find and advertise peers on the local network."},
                {"Discovery interface IPv4 address", "The address used for LAN discovery. Leave blank to use the listen address."},
                {"Applying changes", "Network settings save as you edit them. Restart TextLT from the Connect panel to apply listener changes."},
            }},
            {"Identity", {
                {"Device name", "The name other TextLT devices see. Save name applies an edited name."},
                {"Peer ID", "This device's TextLT identifier. Save ID applies an edited identifier."},
                {"Certificate fingerprint", "The SHA-256 fingerprint of this device's certificate. Copy fingerprint reads the current certificate and copies its fingerprint."},
                {"JSON version", "The identity export format version. This app currently exports version 1."},
                {"IPv4 address and TCP port to share", "The endpoint that other devices should use to reach this TextLT listener. Copy identity puts these values and the identity fields into JSON."},
                {"Regenerate identity", "Create a new certificate and peer ID. This changes the fingerprint and requires sharing the new identity JSON with peers."},
            }},
            {"Files", {
                {"Certificate PEM path", "Path to the certificate PEM file used by the TextLT identity."},
                {"Private key PEM path", "Path to the matching private key PEM file. The certificate and key must form a valid pair."},
                {"Save paths", "Check that the certificate and key match, then save both paths. Restart a running TextLT listener to use changed files."},
            }},
        };
        const auto& tab = help[tab_];
        Elements rows;
        for (const auto& [label, details] : tab.fields) {
            rows.push_back(text(label) | bold | color(theme_.modal_accent));
            rows.push_back(paragraph(details));
        }
        const auto terminal = Terminal::Size();
        const int height = std::clamp(terminal.dimy - 13, 4, 20);
        return vbox({
            text(std::string(tab.title) + " Info") | bold | color(theme_.modal_accent),
            separator() | color(theme_.modal_border),
            vbox(std::move(rows)) | focusPosition(0, info_scroll_) | vscroll_indicator | yframe |
                size(HEIGHT, LESS_THAN, height),
        }) | vscroll_indicator | yframe | color(theme_.modal_foreground);
    }

    Element RenderFiles() {
        return vbox({
            text("Certificate PEM path") | bold | color(theme_.modal_accent),
            certificate_input_->Render() | borderStyled(LIGHT, theme_.modal_border),
            text("Private key PEM path") | bold | color(theme_.modal_accent),
            private_key_input_->Render() | borderStyled(LIGHT, theme_.modal_border),
        }) | vscroll_indicator | yframe | flex;
    }

    const Theme& theme_;
    connect_ui::ConnectWorkspace& workspace_;
    std::function<bool(const std::string&)> copy_;
    std::function<void()> close_;
    connect_ui::NativeConnectionProfile identity_;
    std::vector<std::string> labels_;
    std::string json_, device_name_, peer_id_, shared_address_, shared_port_, status_;
    std::string certificate_, private_key_;
    int tab_ = Connections;
    int selected_ = 0;
    int confirming_identity_ = 0;
    int info_scroll_ = 0;
    Component root_, connections_tab_, network_tab_, identity_tab_, files_tab_, connections_, import_json_, import_, connect_;
    Component name_, save_name_, address_, port_, copy_json_, close_button_;
    Component info_button_, close_info_;
    Component listen_ip_, listen_port_, discovery_enabled_, discovery_ip_;
    Component peer_id_input_, save_peer_id_, copy_fingerprint_;
    Component certificate_input_, private_key_input_, save_paths_;
    Component regenerate_, confirm_regenerate_, cancel_regenerate_;
};

void TextLTConfigModal::Configure(const Theme& theme, connect_ui::ConnectWorkspace& workspace,
                                 std::function<bool(const std::string&)> copy, std::function<void()> close) {
    content_ = std::make_shared<TextLTConfigContent>(theme, workspace, std::move(copy), close);
    modal_ = std::make_shared<ModalWindow>(content_, &theme, std::move(close));
}

void TextLTConfigModal::Open() {
    open_ = true;
    content_->Open();
}

Component TextLTConfigModal::View() const { return modal_; }

} // namespace textlt
