#include "modals/modal_connect_settings.hpp"

#include <algorithm>

#include "connect_workspace.hpp"
#include "editor/clipboard_controller.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/screen/terminal.hpp"
#include "modals/modal_window.hpp"
#include "ui_button.hpp"
#include "ui_selectable_input.hpp"

namespace textlt {

using namespace ftxui;

class ConnectSettingsContent : public IModalContent {
    enum TroubleshootStep { ChoosePort, Prepare, CheckWindows, Repair, CheckAgain, TestPeer, WindowsBlocked, PeerBlocked, Done };

    struct ForwardEndpoint {
        std::string windows_ip;
        std::string wsl_ip;
        std::string port;
    };

public:
    ConnectSettingsContent(const Theme& theme, connect_ui::ConnectWorkspace& workspace,
                           std::function<bool(const std::string&)> copy,
                           std::function<void()> close)
        : theme_(theme), workspace_(workspace), copy_(std::move(copy)) {
        auto& config = workspace_.Configuration();
        for (const auto* label : {"LocalSend", "Diagnostics"}) {
            const int tab = static_cast<int>(tabs_.size());
            auto option = ButtonOption::Simple();
            option.label = label;
            option.on_click = [this, tab] { SelectTab(tab); };
            option.transform = [this, label, tab](const EntryState& state) {
                return RenderModalTabButton(theme_, label, selected_tab_ == tab, state.focused);
            };
            tabs_.push_back(Button(std::move(option)));
        }

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
            workspace_.RefreshDiagnostics(true);
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
        test_discovery_ = Action("Test discovery", [this] {
            const bool was_listening = workspace_.LocalSendRuntime().listening;
            const auto result = workspace_.TestLocalSendDiscovery();
            if (!result.accepted) {
                notice_ = result.reason;
            } else {
                notice_ = was_listening ? "LocalSend discovery requested."
                                        : "LocalSend started. You can stop it from the Connect sidebar.";
            }
            OpenInfo();
        });
        copy_diagnostics_ = Action("Copy diagnostics", [this] {
            std::string report;
            for (const auto& line : DiagnosticReport()) report += line + "\n";
            notice_ = copy_(report) ? "Diagnostics copied" : ClipboardController::kWriteFailed;
        }, ButtonRole::Utility, [this] {
            const auto network = workspace_.Diagnostics();
            return !network.checking_localsend_ports && !network.localsend_port_findings.empty();
        });
        troubleshoot_ = Action("Troubleshoot", [this] {
            wizard_endpoint_.reset();
            info_open_ = 2;
            SetTroubleshootStep(ChoosePort);
        });
        wizard_actions_ = Container::Tab({
            Container::Vertical({
                WizardPortChoice(false),
                WizardPortChoice(true),
            }),
            Container::Vertical({
                Action("Start listener", [this] {
                    const auto result = wizard_native_ ? workspace_.StartNativeListener()
                                                       : workspace_.TestLocalSendDiscovery();
                    wizard_notice_ = result.accepted ? "The listener is running." : result.reason;
                }, ButtonRole::Utility, [this] { return WizardListenPort().empty(); }),
                Action("Continue", [this] {
                    const auto network = workspace_.Diagnostics();
                    wizard_endpoint_ = ForwardEndpointFor(network);
                    SetTroubleshootStep(wizard_endpoint_ ? CheckWindows : TestPeer);
                }, ButtonRole::Primary, [this] { return CanContinueTroubleshooting(); }),
            }),
            Container::Vertical({
                Action("TCP test passed", [this] { SetTroubleshootStep(TestPeer); }),
                Action("TCP test failed", [this] { SetTroubleshootStep(Repair); }),
            }),
            Action("I ran the command", [this] { SetTroubleshootStep(CheckAgain); }, ButtonRole::Primary),
            Container::Vertical({
                Action("TCP test passed", [this] { SetTroubleshootStep(TestPeer); }),
                Action("TCP test failed", [this] { SetTroubleshootStep(WindowsBlocked); }),
            }),
            Container::Vertical({
                Action("Message received", [this] { SetTroubleshootStep(Done); }, ButtonRole::Primary),
                Action("Still waiting", [this] { SetTroubleshootStep(PeerBlocked); }),
            }),
            Action("Try the TCP test again", [this] { SetTroubleshootStep(CheckAgain); }),
            Action("Retry from the other device", [this] { SetTroubleshootStep(TestPeer); }),
            Renderer([] { return emptyElement(); }),
        }, &wizard_step_);
        copy_wizard_command_ = Action("Copy command", [this] { CopyTroubleshootCommand(); },
            ButtonRole::Utility, [this] { return !TroubleshootCommand().empty(); });
        close_wizard_ = Action("Close", [this] { CloseTroubleshoot(); }, ButtonRole::Cancel);
        close_ = Action("Close", close, ButtonRole::Cancel);
        info_ = Action("Info", [this] { OpenInfo(); });
        close_info_ = Action("Close", [this] { CloseInfo(); }, ButtonRole::Cancel);
        tabs_row_ = Container::Horizontal(tabs_);
        body_tab_ = Container::Tab(body_, &selected_tab_);
        footer_tab_ = Container::Tab({
            Renderer([] { return emptyElement(); }),
            Container::Horizontal({refresh_diagnostics_, copy_ip_, use_ip_, test_discovery_, copy_diagnostics_, troubleshoot_}),
        }, &selected_tab_);
        auto main = Container::Vertical({
            tabs_row_, body_tab_, Container::Horizontal({footer_tab_, info_, close_}),
        });
        auto wizard = Container::Vertical({wizard_actions_, Container::Horizontal({copy_wizard_command_, close_wizard_})});
        tree_ = CatchEvent(Container::Tab({main, close_info_, wizard}, &info_open_),
                          [this, close = std::move(close)](Event event) {
            if (info_open_ == 2) {
                if (event == Event::Escape) {
                    CloseTroubleshoot();
                    return true;
                }
                if (event == Event::Tab || event == Event::TabReverse) {
                    const auto step = wizard_actions_->ActiveChild();
                    Components controls;
                    for (std::size_t index = 0; index < step->ChildCount(); ++index) {
                        if (auto control = step->ChildAt(index); control->Focusable()) controls.push_back(control);
                    }
                    if (controls.empty() && step->Focusable()) controls.push_back(step);
                    if (!TroubleshootCommand().empty()) controls.push_back(copy_wizard_command_);
                    controls.push_back(close_wizard_);
                    const auto current = std::find_if(controls.begin(), controls.end(),
                        [](const auto& control) { return control->Focused(); });
                    const int count = static_cast<int>(controls.size());
                    const int index = current == controls.end() ? -1 : static_cast<int>(current - controls.begin());
                    const int delta = event == Event::Tab ? 1 : -1;
                    controls[(index + delta + count) % count]->TakeFocus();
                    return true;
                }
                return false;
            }
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
        if (info_open_ == 2) return 1;
        const int width = Terminal::Size().dimx;
        if (ShowingDiagnostics()) return width < 36 ? 6 : width < 40 ? 5 : width < 60 ? 4 : width < 100 ? 2 : 1;
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
        if (info_open_ == 2) return dbox({settings, RenderTroubleshoot() | clear_under | center});
        return info_open_ ? dbox({settings, RenderInfo() | clear_under | center}) : settings;
    }

    Element RenderCustomFooter() override {
        if (ShowingDiagnostics()) {
            const int width = Terminal::Size().dimx;
            if (info_open_) return emptyElement();
            Elements rows;
            auto close = hbox({info_->Render(), text(" "), close_->Render()});
            if (width < 40) {
                rows.push_back(hbox({refresh_diagnostics_->Render(), copy_ip_->Render()}));
                if (width < 36) {
                    rows.push_back(test_discovery_->Render());
                    rows.push_back(troubleshoot_->Render());
                } else {
                    rows.push_back(hbox({test_discovery_->Render(), troubleshoot_->Render()}));
                }
                rows.push_back(copy_diagnostics_->Render());
                rows.push_back(use_ip_->Render());
                rows.push_back(std::move(close));
            } else if (width < 60) {
                rows.push_back(hbox({refresh_diagnostics_->Render(), copy_ip_->Render()}));
                rows.push_back(hbox({test_discovery_->Render(), copy_diagnostics_->Render()}));
                rows.push_back(troubleshoot_->Render());
                rows.push_back(hbox({use_ip_->Render(), filler(), std::move(close)}));
            } else if (width < 100) {
                rows.push_back(hbox({refresh_diagnostics_->Render(), copy_ip_->Render(),
                                     test_discovery_->Render(), troubleshoot_->Render()}));
                rows.push_back(hbox({use_ip_->Render(), copy_diagnostics_->Render(), filler(), std::move(close)}));
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

    void OpenInfo() {
        info_open_ = 1;
        info_scroll_ = 0;
        close_info_->TakeFocus();
    }

    void CloseInfo() {
        info_open_ = 0;
        info_->TakeFocus();
    }

    std::string ConfiguredWizardPort(bool native) const {
        const auto& config = workspace_.Configuration();
        const auto& configured = native ? config.listen_port : config.localsend_https_port;
        if (!configured.empty()) return configured;
        return native ? connect_ui::kDefaultNativePort : connect_ui::kDefaultLocalSendPort;
    }

    std::string ListenerPort(bool native) const {
        if (native) return workspace_.ActiveNativeListenPort();
        const auto port = workspace_.LocalSendRuntime().https_port;
        return port ? std::to_string(*port) : std::string{};
    }

    std::string WizardListenPort() const { return ListenerPort(wizard_native_); }

    Component WizardPortChoice(bool native) {
        ButtonOption option = ButtonOption::Simple();
        option.on_click = [this, native] {
            wizard_native_ = native;
            SetTroubleshootStep(Prepare);
        };
        option.transform = [this, native](const EntryState& state) {
            auto port = ListenerPort(native);
            if (port.empty()) port = ConfiguredWizardPort(native);
            if (port == "0") port = native ? "off" : "auto port";
            const auto label = std::string(native ? "TextLT " : "LocalSend ") + port;
            return RenderModalFlatButton(theme_, ButtonSpecFromLabel(label, ButtonRole::Utility), state.focused);
        };
        return Button(std::move(option));
    }

    std::optional<ForwardEndpoint> ForwardEndpointFor(const connect_ui::NetworkDiagnostics& network) const {
        const auto host = connect_ui::SelectWslNatLanIPv4(network);
        const auto port = WizardListenPort();
        if (!host || port.empty()) return {};
        auto guest = wizard_native_ ? workspace_.ActiveNativeListenAddress() : workspace_.LocalSendRuntime().interface_ip;
        if (guest == "0.0.0.0") guest = connect_ui::SelectLanIPv4(network.local);
        const auto ipv4 = [](const std::string& address) {
            return !address.empty() && address.find_first_not_of("0123456789.") == std::string::npos;
        };
        const auto local = std::find_if(network.local.addresses.begin(), network.local.addresses.end(),
            [&](const auto& address) { return address.ipv4 && address.usable && address.address == guest; });
        if (!ipv4(host->address) || !ipv4(guest) || local == network.local.addresses.end()) return {};
        return ForwardEndpoint{host->address, std::move(guest), port};
    }

    bool CanContinueTroubleshooting() const {
        if (WizardListenPort().empty()) return false;
        const auto network = workspace_.Diagnostics();
        if (!network.wsl) return true;
        if (network.wsl_mode == "nat") return ForwardEndpointFor(network).has_value();
        return network.wsl_mode == "mirrored" || network.wsl_mode == "bridged";
    }

    std::string TroubleshootCommand() const {
        if (!wizard_endpoint_) return {};
        const auto& [windows_ip, wsl_ip, port] = *wizard_endpoint_;
        if (wizard_step_ == CheckWindows || wizard_step_ == CheckAgain) {
            return "Test-NetConnection " + windows_ip + " -Port " + port;
        }
        if (wizard_step_ == Repair) {
            const auto listen = " listenaddress=" + windows_ip + " listenport=" + port;
            const auto target = " connectaddress=" + wsl_ip + " connectport=" + port;
            return "if (-not (Test-NetConnection " + wsl_ip + " -Port " + port + " -InformationLevel Quiet)) "
                "{ throw 'Windows cannot reach the listener in WSL. Leave TextLT running and check its listen address.' }; "
                "$rows = netsh.exe interface portproxy show v4tov4; "
                "if ($LASTEXITCODE -ne 0) { throw 'Could not read the Windows forwarding rules.' }; "
                "$prefix = '^\\s*' + [regex]::Escape('" + windows_ip + "') + '\\s+" + port + "\\s+'; "
                "$expected = $prefix + [regex]::Escape('" + wsl_ip + "') + '\\s+" + port + "\\s*$'; "
                "$existing = @($rows | Where-Object { $_ -match $prefix }); "
                "if ($existing.Count) { if ($existing.Count -ne 1 -or $existing[0] -notmatch $expected) "
                "{ throw 'This Windows port already forwards somewhere else. Review the existing rule before changing it.' }; "
                "netsh.exe interface portproxy delete v4tov4" + listen + "; "
                "if ($LASTEXITCODE -ne 0) { throw 'Could not remove the matching rule. Check that PowerShell is running as Administrator.' } }; "
                "netsh.exe interface portproxy add v4tov4" + listen + target + "; "
                "if ($LASTEXITCODE -ne 0) { throw 'Could not add the rule. Check that PowerShell is running as Administrator.' }";
        }
        if (wizard_step_ == WindowsBlocked) {
            return "Get-Service iphlpsvc | Format-List Status,StartType; netsh.exe interface portproxy show all; "
                "Test-NetConnection " + wsl_ip + " -Port " + port;
        }
        if (wizard_step_ == PeerBlocked) {
            return "Get-NetFirewallPortFilter -PolicyStore ActiveStore | Where-Object { "
                "$_.Protocol -eq 'TCP' -and $_.LocalPort -contains '" + port + "' } | Get-NetFirewallRule | "
                "Where-Object { $_.Enabled -eq 'True' -and $_.Direction -eq 'Inbound' } | "
                "Get-NetFirewallAddressFilter | Format-List LocalAddress,RemoteAddress";
        }
        return {};
    }

    void CopyTroubleshootCommand() {
        const auto command = TroubleshootCommand();
        if (!command.empty()) {
            wizard_notice_ = copy_(command) ? "PowerShell command copied." : ClipboardController::kWriteFailed;
        }
    }

    void SetTroubleshootStep(TroubleshootStep step) {
        wizard_step_ = step;
        wizard_notice_.clear();
        CopyTroubleshootCommand();
        if (step == Done) close_wizard_->TakeFocus();
        else wizard_actions_->TakeFocus();
    }

    void CloseTroubleshoot() {
        info_open_ = 0;
        troubleshoot_->TakeFocus();
    }

    Element RenderTroubleshoot() {
        const auto port = WizardListenPort();
        const auto endpoint = wizard_endpoint_ ? wizard_endpoint_->windows_ip + ":" + wizard_endpoint_->port
                                               : std::string{};
        std::string title, instructions;
        switch (wizard_step_) {
        case ChoosePort:
            title = "Choose a port to troubleshoot";
            instructions = "Choose LocalSend for phone transfers or TextLT for a paired TextLT device. Each button shows the TCP port for that service.";
            break;
        case Prepare: {
            title = "Check the listener";
            const auto network = workspace_.Diagnostics();
            if (port.empty()) {
                instructions = wizard_native_
                    ? "Start the TextLT listener. Direct connections require a paired TextLT device."
                    : "Start LocalSend in TextLT and leave it running during the checks.";
            } else {
                instructions = "TextLT is listening on TCP port " + port + ".";
                if (network.wsl && !CanContinueTroubleshooting()) {
                    instructions += network.checking_localsend_ports
                        ? " Waiting for Windows network details."
                        : " The Windows forwarding address could not be determined. Close this guide and refresh Diagnostics.";
                }
            }
            break;
        }
        case CheckWindows:
        case CheckAgain:
            title = wizard_step_ == CheckWindows ? "Test the Windows port" : "Test the Windows port again";
            instructions = "Open PowerShell in Windows. Paste the copied command and press Enter. "
                "It checks " + endpoint + ". Choose the result shown next to TcpTestSucceeded.";
            break;
        case Repair:
            title = "Set up Windows forwarding";
            instructions = "Open PowerShell as Administrator in Windows. Paste the single copied command and press Enter. "
                "It checks that Windows can reach TextLT in WSL, then creates or recreates the matching forward from " +
                endpoint + " to " + wizard_endpoint_->wsl_ip + ":" + wizard_endpoint_->port + ".";
            break;
        case TestPeer:
            title = "Send a test message";
            instructions = wizard_native_
                ? "From your paired TextLT device, send a short message to this computer."
                : "On the phone, open LocalSend, refresh the device list, and send a short message to TextLT.";
            if (!endpoint.empty()) instructions += " If you connect by IP, use " + endpoint + ".";
            break;
        case WindowsBlocked:
            title = "Windows still cannot accept connections";
            instructions = "Run the copied command in Windows PowerShell. Check that IP Helper is running "
                "and the test to WSL succeeds. Review any error from the forwarding command before testing again.";
            break;
        case PeerBlocked:
            title = "The message did not arrive";
            instructions = "Check that both devices are on the same Wi-Fi. ";
            if (wizard_native_) instructions += "Use the paired TextLT identity and check the peer address. ";
            if (wizard_endpoint_) {
                instructions += "In Administrator PowerShell, run the copied command and check that an incoming firewall rule allows the sending device's Wi-Fi address.";
            } else {
                instructions += "Review the incoming firewall rules for TCP port " + port + ".";
            }
            break;
        case Done:
            title = "Message received";
            instructions = "You confirmed that a message reached TextLT on port " + port + ".";
            break;
        }
        const auto terminal = Terminal::Size();
        Elements buttons;
        if (!TroubleshootCommand().empty()) buttons.push_back(copy_wizard_command_->Render());
        buttons.push_back(close_wizard_->Render());
        return vbox({
            paragraph(std::move(title)) | bold | color(theme_.modal_accent),
            separator() | color(theme_.modal_border),
            paragraph(std::move(instructions)),
            wizard_notice_.empty() ? emptyElement() : paragraph(wizard_notice_) | dim,
            wizard_actions_->Render(),
            hbox(std::move(buttons)),
        }) | size(WIDTH, EQUAL, std::clamp(terminal.dimx - 6, 20, 68)) |
             borderStyled(LIGHT, theme_.modal_border) | bgcolor(theme_.modal_background) |
             color(theme_.modal_foreground);
    }

    Element RenderInfo() {
        struct Help {
            const char* title;
            std::vector<std::pair<const char*, const char*>> fields;
        };
        static const Help help[] = {
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
        Elements rows;
        if (ShowingDiagnostics()) {
            for (const auto& line : DiagnosticReport()) {
                rows.push_back(line.empty() ? separator() | color(theme_.modal_border) : paragraph(line));
            }
        }
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
        if (ShowingDiagnostics()) {
            selected_address_ = 0;
            selected_ip_.clear();
            diagnostic_rows_.clear();
            workspace_.RefreshDiagnostics(true);
        }
        body_[tab]->TakeFocus();
    }

    void SaveSettings() {
        notice_ = workspace_.SaveSettings() ? std::string{} : "Could not save Connect settings";
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
        if (row.docker_host) {
            return "Docker resolves this address for reaching the host from the container. Use the host's LAN IP for incoming connections through a published port.";
        }
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
        Elements body{text("Can TextLT use these ports?") | bold | color(theme_.modal_accent)};
        if (network.checking_localsend_ports) {
            body.push_back(paragraph("Checking ports 53317 and 53327 and firewall settings…") | dim);
        } else {
            for (const auto& line : PortStatusLines(network)) body.push_back(paragraph(line));
        }
        if (!network.checking_localsend_ports && !network.localsend_port_findings.empty()) {
            const auto& findings = network.localsend_port_findings;
            const std::string_view priorities[]{
                "Windows shows no TCP listener at",
                "no standard TCP forwarding rule",
                "Windows firewall on ",
            };
            std::vector<std::string_view> highlights;
            for (const auto priority : priorities) {
                const auto match = std::find_if(findings.begin(), findings.end(), [priority](const auto& line) {
                    return line.find(priority) != std::string::npos;
                });
                if (match != findings.end()) highlights.push_back(*match);
            }
            for (const auto line : highlights) body.push_back(paragraph(std::string(line)));
            body.push_back(paragraph("Free means the port could be opened on this device. Open Info for firewall and forwarding details.") | dim);
        }
        if (!state.error.empty() && state.error != notice_) {
            body.push_back(paragraph(state.error));
        }
        if (!notice_.empty()) body.push_back(paragraph(notice_));
        body.push_back(separator() | color(theme_.modal_border));
        body.push_back(text("IPv4 addresses") | bold | color(theme_.modal_accent));
        body.push_back(row ? diagnostic_menu_->Render() | vscroll_indicator | yframe | size(HEIGHT, LESS_THAN, 7)
                           : text("No usable IPv4 addresses found") | dim);
        if (row) body.push_back(paragraph(ExplainAddress(*row, network)));
        if (network.container) {
            body.push_back(paragraph("Published TCP ports and LAN discovery are checked separately.") | dim);
        }
        body.push_back(paragraph(automatic ? "LocalSend chooses its address automatically at Start."
                                           : "LocalSend override: " + configured + ". Clear it in LocalSend to use Auto.") | dim);
        body.push_back(filler());
        return vbox(std::move(body));
    }

    std::vector<std::string> PortStatusLines(const connect_ui::NetworkDiagnostics& network) const {
        using connect_ui::PortAvailability;
        const auto status = [](PortAvailability availability) {
            switch (availability) {
            case PortAvailability::Available: return "Free";
            case PortAvailability::InUse: return "In use";
            case PortAvailability::Reserved: return "Reserved";
            case PortAvailability::Unknown: return "Could not check";
            }
            return "Could not check";
        };
        std::vector<std::string> lines;
        for (const auto& row : network.localsend_ports) {
            lines.push_back(row.system + " · " + std::to_string(row.port) +
                            " · TCP " + status(row.tcp) + " · UDP " + status(row.udp));
        }
        return lines;
    }

    std::vector<std::string> DiagnosticReport() {
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
        std::vector<std::string> summary{
            "TextLT discovery and port report",
            "UDP lets devices find each other. TCP lets them connect and transfer files.",
        };
        if (network.checking_localsend_ports) {
            summary.push_back(network.localsend_port_findings.empty()
                ? "Checking ports 53317 and 53327, forwarding, and firewall rules…"
                : "Refreshing the port, forwarding, and firewall checks. Previous results are shown below.");
        } else if (network.localsend_port_findings.empty()) {
            summary.push_back("Port and firewall results are unavailable. Choose Refresh to try again.");
        }
        const auto ports = PortStatusLines(network);
        summary.insert(summary.end(), ports.begin(), ports.end());
        if (!ports.empty()) summary.push_back("");
        summary.insert(summary.end(), network.localsend_port_findings.begin(),
                       network.localsend_port_findings.end());
        if (!state.error.empty()) summary.push_back("LocalSend could not start. " + state.error);
        summary.push_back("");
        const auto native_port = workspace_.ActiveNativeListenPort();
        const auto configured_native = config.listen_port.empty() ? connect_ui::kDefaultNativePort : config.listen_port;
        summary.push_back("TextLT direct connections. Configured TCP port " + configured_native +
                          (native_port.empty() ? ". No active listener." : ". Listening on port " + native_port + "."));
        summary.push_back("LocalSend UDP discovery. Configured port " +
                      (config.localsend_udp_port.empty() ? connect_ui::kDefaultLocalSendPort : config.localsend_udp_port) +
                      ", active port " + port(state.udp_port) + ". Opening the port " + check(state.udp_bind) + ".");
        summary.push_back("LocalSend TCP transfers. Configured port " + configured_https +
                      ", active port " + port(state.https_port) + ". Opening the port " + check(state.https_bind) + ".");
        summary.push_back(std::string("Joining the discovery group ") + config.localsend_group +
                          ". " + check(state.multicast_join) + ".");
        summary.push_back("Network address selection " +
            (automatic ? std::string("Automatic") : "Manual " + config.localsend_interface) +
            (state.interface_ip.empty() ? "" : ". LocalSend chose " + state.interface_ip) + ".");
        if (state.listening) {
            const auto discovery = workspace_.LocalSendDiscoveryStatus();
            if (discovery.multicast_send == LocalSendCheck::Failed) {
                summary.push_back("Could not send the discovery announcement. " + discovery.multicast_error);
            } else if (discovery.multicast_send == LocalSendCheck::Succeeded) {
                summary.push_back("Discovery announcement sent. Messages from nearby devices this session " +
                                  std::to_string(discovery.multicast_packets) + ".");
            } else {
                summary.push_back("Waiting to send the discovery announcement.");
            }
            if (discovery.unicast_running) {
                summary.push_back("Looking for LocalSend devices on the Windows network " + discovery.unicast_subnet + "…");
            } else if (!discovery.unicast_subnet.empty()) {
                summary.push_back("Windows network " + discovery.unicast_subnet + ". Checked " +
                    std::to_string(discovery.unicast_attempts) + " addresses and found " +
                    std::to_string(discovery.unicast_registered) + " LocalSend devices over HTTPS.");
            } else if (network.wsl_mode == "nat") {
                summary.push_back(connect_ui::SelectWslNatLanIPv4(network)
                    ? "The Windows network discovery check is pending."
                    : "TextLT needs a usable Windows network address to run the HTTPS discovery check.");
            }
            summary.push_back(workspace_.LocalSendPeerVerified() ? "TextLT reached a LocalSend device this session."
                                                               : "Reachability from another device is still unconfirmed.");
            if (workspace_.LocalSendPhoneReachedListener()) summary.push_back("A phone reached TextLT this session.");
        } else {
            summary.push_back("LocalSend is stopped.");
        }
        if (network.wsl) summary.push_back("WSL networking mode " + network.wsl_mode + ".");
        if (network.wsl_mode == "nat") summary.push_back("In NAT mode, Windows must forward the TCP port for a phone to reach TextLT in WSL.");
        for (const auto* environment : {&network.local, &network.host}) {
            if (environment->name.empty()) continue;
            summary.push_back("");
            summary.push_back(environment->name);
            if (!environment->notice.empty()) summary.push_back(environment->notice);
            for (const auto& address : environment->addresses) {
                const auto prefix = address.prefix < 0 ? "" : "/" + std::to_string(address.prefix);
                summary.push_back(address.address + prefix + "  " + address.interface_name +
                    "  |  " + address.status + "  |  " + address.type + "  |  Origin " + address.origin +
                    (address.host_only ? "  |  Host only" : ""));
            }
            for (const auto& gateway : environment->gateways) {
                summary.push_back("Default gateway " + gateway.address + "  " + gateway.interface_name);
            }
        }
        return summary;
    }

    const Theme& theme_;
    connect_ui::ConnectWorkspace& workspace_;
    std::function<bool(const std::string&)> copy_;
    int selected_tab_ = 0;
    int info_open_ = 0;
    int info_scroll_ = 0;
    int wizard_step_ = ChoosePort;
    bool wizard_native_ = false;
    std::optional<ForwardEndpoint> wizard_endpoint_;
    std::string wizard_notice_;
    int selected_address_ = 0;
    std::string selected_ip_;
    std::vector<connect_ui::DiagnosticAddress> diagnostic_rows_;
    std::vector<std::string> diagnostic_labels_;
    Component diagnostic_menu_;
    std::string notice_;
    Components tabs_, body_;
    Component tree_, tabs_row_, body_tab_, footer_tab_;
    Component refresh_diagnostics_, copy_ip_, use_ip_, test_discovery_, copy_diagnostics_;
    Component troubleshoot_, wizard_actions_, copy_wizard_command_, close_wizard_;
    Component close_, info_, close_info_;
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
