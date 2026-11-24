#include "modals/modal_connect_settings.hpp"

#include <algorithm>

#include "connect_workspace.hpp"
#include "editor/clipboard_controller.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/screen/terminal.hpp"
#include "modals/modal_window.hpp"
#include "textlt/connect/textlt_provider.hpp"
#include "ui_button.hpp"
#include "ui_selectable_input.hpp"

namespace textlt {

using namespace ftxui;

class ConnectSettingsContent : public IModalContent {
public:
    ConnectSettingsContent(const Theme& theme, connect_ui::ConnectWorkspace& workspace,
                           std::function<bool(const std::string&)> copy,
                           std::function<void()> close)
        : theme_(theme), workspace_(workspace), copy_(std::move(copy)) {
        auto& config = workspace_.Configuration();
        for (const auto* label : {"Connections", "Network", "Identity", "LocalSend", "Diagnostics"}) {
            const int tab = static_cast<int>(tabs_.size());
            auto option = ButtonOption::Simple();
            option.label = label;
            option.on_click = [this, tab] { SelectTab(tab); };
            option.transform = [this, label, tab](const EntryState& state) {
                return RenderModalTabButton(theme_, label, selected_tab_ == tab, state.focused);
            };
            tabs_.push_back(Button(std::move(option)));
        }

        auto peer_option = MenuOption::Vertical();
        peer_option.on_enter = [this] { UsePeer(); };
        peer_menu_ = Menu(&peer_labels_, &selected_peer_, std::move(peer_option));
        peer_box_ = Renderer(peer_menu_, [this] {
            peers_ = workspace_.DiscoveredPeers();
            peer_labels_.clear();
            for (const auto& [id, name] : peers_) peer_labels_.push_back(name + " [" + id + "]");
            selected_peer_ = std::clamp(selected_peer_, 0,
                                       std::max(0, static_cast<int>(peer_labels_.size()) - 1));
            return vbox({
                text("Discovered peers") | bold,
                peer_labels_.empty() ? text("No discovered peers") | dim :
                    peer_menu_->Render() | vscroll_indicator | yframe | size(HEIGHT, LESS_THAN, 5),
            });
        });
        use_peer_ = Action("Use selected peer ID", [this] { UsePeer(); });
        body_.push_back(Container::Vertical({
            Field("Peer pairing code from the other device", pairing_code_, false),
            Action("Pair peer", [this] {
                const auto result = workspace_.PairNativePeer(pairing_code_);
                notice_ = result.reason;
                if (result.accepted) pairing_code_.clear();
            }, ButtonRole::Primary),
            peer_box_, use_peer_,
            Field("Trusted discovery peer ID", config.peer_id),
            Field("Manual peer IPv4 address", config.peer_ip),
            Field("Manual peer port (default 53327)", config.peer_port),
            Field("Expected peer SHA-256 fingerprint", config.peer_fingerprint),
        }));
        auto discovery = SettingsCheckbox("Enable LAN discovery", config.discovery);
        body_.push_back(Container::Vertical({
            Renderer([this] {
                const auto& address = workspace_.LocalIPv4();
                return text("Local IPv4: " + (address.empty() ? std::string("Unavailable") : address)) |
                    color(theme_.modal_accent) | bold;
            }),
            Field("Listen IPv4 address", config.listen_ip),
            Field("Listen port (default 53327, 0 = off)", config.listen_port),
            discovery,
            Field("Discovery interface IPv4 address (blank uses listen address)", config.discovery_ip),
        }));
        copy_fingerprint_ = Action("Copy fingerprint", [this] {
            RefreshFingerprint();
            if (!fingerprint_.empty()) notice_ = copy_(fingerprint_)
                ? "Fingerprint copied" : ClipboardController::kWriteFailed;
        });
        refresh_fingerprint_ = Action("Read fingerprint", [this] {
            RefreshFingerprint();
            notice_ = fingerprint_.empty() ? "Could not read a certificate at that path" : "Fingerprint read";
        });
        body_.push_back(Container::Vertical({
            Action("Copy pairing code", [this] {
                RefreshFingerprint();
                const auto code = connect::MakeTextLTPairingCode(workspace_.Configuration().local_id, fingerprint_);
                notice_ = !code ? "A valid local peer ID and certificate are required"
                    : copy_(*code) ? "Pairing code copied" : ClipboardController::kWriteFailed;
            }, ButtonRole::Primary),
            Field("Local peer ID", config.local_id),
            Field("Display name", config.display_name),
            Field("Local certificate PEM path", config.certificate),
            Field("Local private key PEM path", config.private_key),
            Renderer([this] {
                return paragraph(fingerprint_.empty() ? "SHA-256 fingerprint unavailable" : fingerprint_);
            }),
        }));
        body_.push_back(Container::Vertical({
            Field("Peer IPv4 address or IPv4:port (default 53317)", config.localsend_peer_endpoint),
            Field("Discovery UDP port (default 53317)", config.localsend_udp_port),
            Field("HTTPS port (default 53317, 0 = free port)", config.localsend_https_port),
            Field("LocalSend alias", config.localsend_alias),
            Field("LocalSend interface IPv4 address (blank = Auto)", config.localsend_interface),
            Field("LocalSend multicast group", config.localsend_group),
        }));
        auto address_option = MenuOption::Vertical();
        address_option.on_change = [this] {
            notice_.clear();
            if (const auto* row = SelectedAddress()) selected_ip_ = row->ip;
        };
        address_option.entries_option.transform = [this](const EntryState& state) {
            auto row = hbox({text(state.active ? "› " : "  "), text(state.label) | flex});
            return state.active ? row | bgcolor(theme_.modal_selected_item_bg) |
                color(theme_.modal_selected_item_fg) : row;
        };
        diagnostic_menu_ = Menu(&diagnostic_labels_, &selected_address_, std::move(address_option));
        body_.push_back(Container::Vertical({
            Renderer(diagnostic_menu_, [this] { return RenderDiagnostics(); }),
        }));
        refresh_diagnostics_ = Action("Refresh", [this] {
            notice_.clear();
            workspace_.RefreshDiagnostics();
        });
        copy_ip_ = Action("Copy IP", [this] {
            if (const auto* row = SelectedAddress()) {
                notice_ = copy_(row->ip) ? "IP address copied" : ClipboardController::kWriteFailed;
            }
        }, ButtonRole::Utility, [this] { return SelectedAddress() != nullptr; });
        use_ip_ = Action("Use for LocalSend", [this] {
            const auto* row = SelectedAddress();
            if (row && workspace_.UseLocalSendAddress(row->ip)) {
                notice_ = workspace_.LocalSendRuntime().listening
                    ? "Override saved. Stop and start LocalSend in the left column to apply it."
                    : "Override saved for the next LocalSend start.";
            } else {
                notice_ = "Could not set the LocalSend interface. Refresh and check the address.";
            }
        }, ButtonRole::Utility, [this] {
            const auto* row = SelectedAddress();
            return row && row->can_bind;
        });
        close_ = Action("Close", close, ButtonRole::Cancel);
        info_ = Action("Info", [this] {
            info_open_ = 1;
            info_scroll_ = 0;
            close_info_->TakeFocus();
        });
        close_info_ = Action("Close", [this] { CloseInfo(); }, ButtonRole::Cancel);
        tabs_row_ = Container::Horizontal(tabs_);
        body_tab_ = Container::Tab(body_, &selected_tab_);
        footer_tab_ = Container::Tab({
            Renderer([] { return emptyElement(); }),
            Renderer([] { return emptyElement(); }),
            Container::Horizontal({refresh_fingerprint_, copy_fingerprint_}),
            Renderer([] { return emptyElement(); }),
            Container::Horizontal({refresh_diagnostics_, copy_ip_, use_ip_}),
        }, &selected_tab_);
        auto main = Container::Vertical({
            tabs_row_, body_tab_, Container::Horizontal({footer_tab_, info_, close_}),
        });
        tree_ = CatchEvent(Container::Tab({main, close_info_}, &info_open_),
                          [this, close = std::move(close)](Event event) {
            if (info_open_) {
                if (event == Event::Escape) {
                    CloseInfo();
                    return true;
                }
                if (event == Event::ArrowUp || event == Event::ArrowDown ||
                    event == Event::PageUp || event == Event::PageDown || event.is_mouse()) {
                    const int step = event == Event::PageUp ? -8 : event == Event::PageDown ? 8
                        : event == Event::ArrowUp ? -1 : event == Event::ArrowDown ? 1
                        : event.mouse().button == Mouse::WheelUp ? -3
                        : event.mouse().button == Mouse::WheelDown ? 3 : 0;
                    if (step) {
                        info_scroll_ = std::clamp(info_scroll_ + step, 0, 200);
                        return true;
                    }
                }
                return false;
            }
            if (event == Event::Escape) {
                close();
                return true;
            }
            if (event != Event::Tab && event != Event::TabReverse) return false;
            CycleFocus(event == Event::Tab ? 1 : -1);
            return true;
        });
    }

    void Open(ConnectSettingsTab tab) {
        notice_.clear();
        info_open_ = 0;
        workspace_.RefreshLocalIPv4();
        SelectTab(static_cast<int>(tab));
    }
    Component GetMainComponent() override { return tree_; }
    std::string GetTitle() override { return "Connect Settings"; }
    ModalSizePreference GetModalSizePreference() const override {
        const auto terminal = Terminal::Size();
        return {std::clamp(terminal.dimx, 20, 104), std::clamp(terminal.dimy, 17, 35)};
    }
    ModalFrameStyle GetModalFrameStyle() const override { return ModalFrameStyle::TitleInBorder; }
    std::string GetFooterText() const override { return {}; }
    bool HasCustomFooter() const override { return true; }
    int GetCustomFooterHeight() const override {
        const int width = Terminal::Size().dimx;
        if (ShowingDiagnostics()) return width < 40 ? 3 : width < 64 ? 2 : 1;
        return 3;
    }

    Element Render() override {
        Elements tabs;
        const auto focused = std::find_if(tabs_.begin(), tabs_.end(), [](const Component& tab) {
            return tab->Focused();
        });
        const auto visible = focused == tabs_.end() ? tabs_.begin() + selected_tab_ : focused;
        for (auto tab = tabs_.begin(); tab != tabs_.end(); ++tab) {
            if (!tabs.empty()) tabs.push_back(text(" "));
            auto label = (*tab)->Render();
            tabs.push_back(tab == visible ? label | focus : label);
        }
        tabs.push_back(filler());
        auto settings = vbox({
            hbox(std::move(tabs)) | xframe,
            separator() | color(theme_.modal_border),
            ShowingDiagnostics()
                ? body_[selected_tab_]->Render() | flex
                : body_[selected_tab_]->Render() | vscroll_indicator | yframe | flex,
        }) | bgcolor(theme_.modal_background) | color(theme_.modal_foreground);
        return info_open_ ? dbox({settings, RenderInfo() | clear_under | center}) : settings;
    }

    Element RenderCustomFooter() override {
        if (ShowingDiagnostics()) {
            const int width = Terminal::Size().dimx;
            if (info_open_) return emptyElement();
            Elements rows;
            auto close = hbox({info_->Render(), text(" "), close_->Render()});
            if (width < 64) {
                rows.push_back(hbox({refresh_diagnostics_->Render(), copy_ip_->Render()}));
                if (width < 40) {
                    rows.push_back(use_ip_->Render());
                    rows.push_back(std::move(close));
                } else {
                    rows.push_back(hbox({use_ip_->Render(), filler(), std::move(close)}));
                }
            } else {
                rows.push_back(hbox({footer_tab_->Render(), filler(), std::move(close)}));
            }
            return vbox(std::move(rows));
        }
        return vbox({
            text(notice_) | dim,
            separator() | color(theme_.modal_border),
            info_open_ ? emptyElement() :
                hbox({footer_tab_->Render(), filler(), info_->Render(), text(" "), close_->Render()}),
        });
    }

private:
    bool ShowingDiagnostics() const {
        return selected_tab_ == static_cast<int>(ConnectSettingsTab::Diagnostics);
    }

    Component SettingsCheckbox(const std::string& label, bool& value) {
        auto option = CheckboxOption::Simple();
        option.on_change = [this] { SaveSettings(); };
        option.transform = [this](const EntryState& state) {
            auto checkbox = text(std::string(state.state ? "[X] " : "[ ] ") + state.label);
            return state.focused
                ? checkbox | bgcolor(theme_.modal_selected_item_bg) | color(theme_.modal_selected_item_fg)
                : checkbox | color(theme_.modal_text_color);
        };
        return Checkbox(label, &value, std::move(option));
    }

    void CloseInfo() {
        info_open_ = 0;
        info_->TakeFocus();
    }

    Element RenderInfo() {
        struct Help {
            const char* title;
            std::vector<std::pair<const char*, const char*>> fields;
        };
        static const Help help[] = {
            {"Connections", {
                {"Pair peer", "Paste the code copied directly from the other device's Identity tab. Both devices must save each other's code before starting TextLT."},
                {"Discovered peers", "Lists TextLT devices seen on your LAN. Use selected peer ID copies the highlighted ID into the field below."},
                {"Trusted discovery peer ID", "The peer's Local peer ID. Use it to connect by discovery instead of entering a manual address and port."},
                {"Manual peer IPv4 address", "The target device's non-loopback IPv4 address. Leave it blank when using discovery."},
                {"Manual peer port", "The target's TextLT listen port. Defaults to 53327. Clear the peer IPv4 address to use discovery."},
                {"Expected peer SHA-256 fingerprint", "Enter the fingerprint you verified directly with the peer. Start networking again to apply connection changes."},
            }},
            {"Network", {
                {"Listen IPv4 address", "The local IPv4 address accepting TextLT connections. The detected address appears above this input."},
                {"Listen port", "The local TextLT TCP port defaults to 53327. Set it to 0 to run without a native listener."},
                {"Enable LAN discovery", "Find TextLT peers on the LAN. A TLS listener with a local ID and display name also advertises itself."},
                {"Discovery interface IPv4 address", "The local LAN adapter address used for multicast. Leave it blank to use the listen address."},
                {"Apply settings", "Start TextLT in the left column. Restart TextLT there after editing settings for a running provider."},
            }},
            {"Identity", {
                {"Copy pairing code", "Share this code directly with the device you want to pair. It contains your local peer ID and public certificate fingerprint."},
                {"Local peer ID", "Choose a unique ID for native TextLT discovery. TextLT advertises it when LAN discovery is enabled and a TLS listener and display name are configured. LocalSend uses its own certificate fingerprint."},
                {"Display name", "The name peers see in their Connections list."},
                {"Local certificate PEM path", "The certificate your TextLT peers verify."},
                {"Local private key PEM path", "The private key matching that certificate. Keep it on this device."},
                {"Read / Copy fingerprint", "Read the certificate's SHA-256 fingerprint and copy it for independent verification with a peer."},
            }},
            {"LocalSend", {
                {"Peer address", "Enter the peer's IPv4 address or IPv4:port. Start LocalSend and choose Connect by IP in the left column. The peer must have HTTPS enabled."},
                {"LocalSend alias", "The name shown to other LocalSend devices."},
                {"Interface IPv4 address", "Leave this blank for Auto, or choose an eligible address in Diagnostics. This address controls the HTTPS listener and the multicast interface."},
                {"Multicast group and discovery UDP port", "Use the same values as the other LocalSend devices. Defaults are 224.0.0.167 and 53317."},
                {"HTTPS port", "The LocalSend receive port. Default is 53317. Use 0 to select a free port or choose another port after a bind conflict."},
            }},
            {"Diagnostics", {}},
        };
        const auto& tab = help[selected_tab_];
        Elements rows = ShowingDiagnostics() ? RenderDiagnosticDetails() : Elements{};
        for (const auto& [label, details] : tab.fields) {
            rows.push_back(text(label) | bold | color(theme_.modal_accent));
            rows.push_back(paragraph(details));
        }
        const auto terminal = Terminal::Size();
        const int width = std::clamp(terminal.dimx - 10, 20, 72);
        const int height = std::clamp(terminal.dimy - 13, 4, 17);
        return vbox({
            text(std::string(tab.title) + " Info") | bold,
            separator() | color(theme_.modal_border),
            vbox(std::move(rows)) | focusPosition(0, info_scroll_) | vscroll_indicator | yframe |
                size(HEIGHT, LESS_THAN, height),
            separator() | color(theme_.modal_border),
            hbox({filler(), close_info_->Render()}),
        }) | size(WIDTH, EQUAL, width) | borderStyled(LIGHT, theme_.modal_border) |
            bgcolor(theme_.modal_background) | color(theme_.modal_foreground);
    }

    Component Field(const std::string& label, std::string& value, bool save = true) {
        InputOption option;
        option.multiline = false;
        if (save) option.on_change = [this] { SaveSettings(); };
        option.transform = [this](InputState state) {
            return theme_.InputTransform(std::move(state));
        };
        auto input = MakeSelectableInput(value, "", std::move(option));
        return Renderer(input, [this, label, input] {
            auto row = vbox({
                text(label) | bold | color(theme_.modal_accent),
                input->Render() | color(theme_.modal_input_fg) |
                    borderStyled(LIGHT, theme_.modal_border),
            });
            return input->Focused() ? row | focus : row;
        });
    }

    Component Action(const std::string& label, std::function<void()> callback,
                     ButtonRole role = ButtonRole::Utility, std::function<bool()> enabled = {}) {
        auto option = ButtonOption::Simple();
        option.label = label;
        option.on_click = [callback = std::move(callback), enabled] {
            if (!enabled || enabled()) callback();
        };
        option.transform = [this, enabled, spec = ButtonSpecFromLabel(label, role)](const EntryState& state) {
            auto style = spec;
            style.enabled = !enabled || enabled();
            return RenderModalFlatButton(theme_, style, state.focused || state.active);
        };
        return Button(std::move(option));
    }

    void SelectTab(int tab) {
        selected_tab_ = tab;
        notice_.clear();
        if (tab == static_cast<int>(ConnectSettingsTab::Identity)) RefreshFingerprint();
        if (ShowingDiagnostics()) {
            selected_address_ = 0;
            selected_ip_.clear();
            diagnostic_rows_.clear();
            workspace_.RefreshDiagnostics();
        }
        body_[tab]->TakeFocus();
    }

    void SaveSettings() {
        notice_ = workspace_.SaveSettings() ? std::string{} : "Could not save Connect settings";
    }

    void UsePeer() {
        if (selected_peer_ < 0 || static_cast<std::size_t>(selected_peer_) >= peers_.size()) return;
        auto& config = workspace_.Configuration();
        const auto& id = peers_[selected_peer_].first;
        if (config.peer_id != id) config.peer_fingerprint.clear();
        config.peer_id = id;
        config.peer_ip.clear();
        notice_ = workspace_.SaveSettings()
            ? "Enter the independently verified peer fingerprint. Start TextLT in the left column to apply it."
            : "Could not save Connect settings";
    }

    void CycleFocus(int delta) {
        Components controls = tabs_;
        const auto append_children = [&controls](const Component& parent) {
            for (std::size_t index = 0; index < parent->ChildCount(); ++index) {
                auto control = parent->ChildAt(index);
                if (control->Focusable()) controls.push_back(std::move(control));
            }
        };
        append_children(body_[selected_tab_]);
        append_children(footer_tab_->ActiveChild());
        controls.push_back(info_);
        controls.push_back(close_);
        const auto current = std::find_if(controls.begin(), controls.end(),
            [](const Component& control) { return control->Focused(); });
        const int size = static_cast<int>(controls.size());
        const int index = current == controls.end() ? 0 : static_cast<int>(current - controls.begin());
        controls[(index + size + delta) % size]->TakeFocus();
    }

    const connect_ui::DiagnosticAddress* SelectedAddress() const {
        if (selected_address_ < 0 || static_cast<std::size_t>(selected_address_) >= diagnostic_rows_.size()) {
            return nullptr;
        }
        return &diagnostic_rows_[selected_address_];
    }

    std::string ExplainAddress(const connect_ui::DiagnosticAddress& row,
                               const connect_ui::NetworkDiagnostics& network) const {
        const auto& state = workspace_.LocalSendRuntime();
        if (!row.can_bind) {
            if (row.windows_host) {
                return "Use this LAN address with TextLT running in Windows. TextLT in WSL cannot bind to it.";
            }
            return "This address is assigned to this system. TextLT cannot bind to it in its current state.";
        }
        const bool listening = state.listening && state.interface_ip == row.ip;
        if (network.wsl) {
            const std::string usage = row.windows_host ? "TextLT in Windows or WSL can listen on this LAN address."
                : listening ? "TextLT in WSL is listening here." : "TextLT in WSL can listen here.";
            if (listening && workspace_.LocalSendPhoneReachedListener()) {
                return usage + " A phone reached the LocalSend listener this session.";
            }
            if (network.wsl_mode == "nat") {
                return usage + " Phone access in NAT mode requires a Windows port forward.";
            }
            return usage + " Phone access is unconfirmed.";
        }
        return listening ? "Use this address for LocalSend on this device. TextLT is listening here."
                         : "TextLT can listen on this address for connections from other devices on this network.";
    }

    Element RenderDiagnostics() {
        const auto network = workspace_.Diagnostics();
        const auto& state = workspace_.LocalSendRuntime();
        const auto& configured = workspace_.Configuration().localsend_interface;
        const bool automatic = connect_ui::IsAutomaticLocalSendAddress(configured);
        const auto preferred = !state.interface_ip.empty() ? state.interface_ip :
            automatic ? connect_ui::SelectLanIPv4(network.local) : configured;
        diagnostic_rows_ = connect_ui::DiagnosticAddresses(network, preferred);
        diagnostic_labels_.clear();
        selected_address_ = 0;
        for (const auto& row : diagnostic_rows_) {
            if (row.ip == selected_ip_) selected_address_ = static_cast<int>(diagnostic_labels_.size());
            diagnostic_labels_.push_back(row.ip + "  " + row.label);
        }

        const auto* row = SelectedAddress();
        Elements body{
            text("IPv4 addresses") | bold | color(theme_.modal_accent),
            row ? diagnostic_menu_->Render() | vscroll_indicator | yframe | size(HEIGHT, LESS_THAN, 7) :
                  text("No usable IPv4 addresses found") | dim,
            separator() | color(theme_.modal_border),
        };
        if (row) body.push_back(paragraph(ExplainAddress(*row, network)));
        body.push_back(paragraph(automatic ? "LocalSend chooses its address automatically at Start."
                                           : "LocalSend override: " + configured + ". Clear it in LocalSend to use Auto.") | dim);
        if (!notice_.empty()) body.push_back(paragraph(notice_));
        body.push_back(filler());
        return vbox(std::move(body));
    }

    Elements RenderDiagnosticDetails() {
        using connect::LocalSendCheck;
        const auto check = [](LocalSendCheck result) {
            switch (result) {
            case LocalSendCheck::Succeeded: return "Succeeded";
            case LocalSendCheck::Failed: return "Failed";
            case LocalSendCheck::NotAttempted: return "Not attempted";
            }
            return "Unknown";
        };
        const auto port = [](const std::optional<std::uint16_t>& actual) {
            return actual ? std::to_string(*actual) : std::string("—");
        };
        const auto& config = workspace_.Configuration();
        const auto& state = workspace_.LocalSendRuntime();
        const bool automatic = connect_ui::IsAutomaticLocalSendAddress(config.localsend_interface);
        const auto network = workspace_.Diagnostics();
        const auto configured_https = config.localsend_https_port == "0" ? std::string("0 (Auto)") :
            config.localsend_https_port.empty() ? std::string(connect_ui::kDefaultLocalSendPort) : config.localsend_https_port;
        Elements summary{
            text("LocalSend") | bold | color(theme_.modal_accent),
            paragraph("LocalSend UDP discovery  Configured " +
                      (config.localsend_udp_port.empty() ? connect_ui::kDefaultLocalSendPort : config.localsend_udp_port) +
                      "  Actual " + port(state.udp_port) + "  Bind " + check(state.udp_bind)),
            paragraph("LocalSend TCP/HTTPS  Configured " + configured_https +
                      "  Actual " + port(state.https_port) + "  Bind " + check(state.https_bind)),
            paragraph(std::string("Multicast join ") + check(state.multicast_join) +
                      "  |  Listening " + (state.listening ? "Yes" : "No") +
                      "  |  Peer reachability " +
                      (workspace_.LocalSendPeerVerified() ? "Verified this session" : "Unverified")),
            paragraph("Interface mode " + (automatic ? std::string("Auto") : "Manual " + config.localsend_interface) +
                      (state.interface_ip.empty() ? "" : "  |  Selected at Start " + state.interface_ip)),
        };
        if (!state.error.empty()) summary.push_back(paragraph(state.error) | bold);

        if (network.wsl) summary.push_back(paragraph("WSL networking mode: " + network.wsl_mode));
        for (const auto* environment : {&network.local, &network.host}) {
            if (environment->name.empty()) continue;
            summary.push_back(separator() | color(theme_.modal_border));
            summary.push_back(text(environment->name) | bold | color(theme_.modal_accent));
            if (!environment->notice.empty()) summary.push_back(paragraph(environment->notice) | dim);
            for (const auto& address : environment->addresses) {
                const auto prefix = address.prefix < 0 ? "" : "/" + std::to_string(address.prefix);
                summary.push_back(paragraph(address.address + prefix + "  " + address.interface_name +
                    "  |  " + address.status + "  |  " + address.type + "  |  Origin " + address.origin +
                    (address.host_only ? "  |  Host only" : "")));
            }
            for (const auto& gateway : environment->gateways) {
                summary.push_back(paragraph("Default gateway " + gateway.address + "  " + gateway.interface_name));
            }
        }
        return summary;
    }

    void RefreshFingerprint() {
        fingerprint_ = connect::TextLTCertificateFingerprint(
            workspace_.Configuration().certificate).value_or("");
    }

    const Theme& theme_;
    connect_ui::ConnectWorkspace& workspace_;
    std::function<bool(const std::string&)> copy_;
    int selected_tab_ = 0;
    int info_open_ = 0;
    int info_scroll_ = 0;
    int selected_peer_ = 0;
    int selected_address_ = 0;
    std::string selected_ip_;
    std::vector<connect_ui::DiagnosticAddress> diagnostic_rows_;
    std::vector<std::string> diagnostic_labels_;
    Component diagnostic_menu_;
    std::vector<std::pair<std::string, std::string>> peers_;
    std::vector<std::string> peer_labels_;
    std::string notice_, fingerprint_, pairing_code_;
    Components tabs_, body_;
    Component tree_, tabs_row_, body_tab_, footer_tab_, peer_menu_, peer_box_, use_peer_;
    Component refresh_diagnostics_, copy_ip_, use_ip_;
    Component close_, refresh_fingerprint_, copy_fingerprint_, info_, close_info_;
};

void ConnectSettingsModal::Configure(const Theme& theme, connect_ui::ConnectWorkspace& workspace,
                                     std::function<bool(const std::string&)> copy,
                                     std::function<void()> close) {
    content_ = std::make_shared<ConnectSettingsContent>(theme, workspace, std::move(copy), close);
    modal_ = std::make_shared<ModalWindow>(content_, &theme, std::move(close));
}

void ConnectSettingsModal::Open(ConnectSettingsTab tab) {
    open_ = true;
    content_->Open(tab);
}

Component ConnectSettingsModal::View() const {
    return modal_;
}

} // namespace textlt
