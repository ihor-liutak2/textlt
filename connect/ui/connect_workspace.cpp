#include "connect_workspace.hpp"

#include "download_destination.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <sstream>
#include <utility>

#include "editor/clipboard_controller.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/screen/terminal.hpp"
#include "ftxui/screen/string.hpp"
#include "textlt/connect/textlt_provider.hpp"
#include "textlt/connect/localsend_provider.hpp"
#include "telegram/telegram_api.hpp"
#include "telegram/telegram_provider.hpp"
#include "theme.hpp"
#include "ui_button.hpp"
#include "ui_horizontal_scroll.hpp"

namespace textlt::connect_ui {
namespace {

using namespace ftxui;
using namespace connect;

std::optional<std::uint16_t> Port(const std::string& value) {
    if (value.empty()) return 0;
    std::uint16_t port = 0;
    const auto end = value.data() + value.size();
    const auto parsed = std::from_chars(value.data(), end, port);
    if (parsed.ec != std::errc{} || parsed.ptr != end) return std::nullopt;
    return port;
}

std::string TransferLabel(TransferState state) {
    switch (state) {
    case TransferState::Pending: return "Waiting for acceptance";
    case TransferState::InProgress: return "Sending";
    case TransferState::Completed: return "Delivered";
    case TransferState::Declined: return "Declined";
    case TransferState::Cancelled: return "Cancelled";
    case TransferState::Failed: return "Failed";
    }
    return {};
}

bool CanRemember(const PeerIdentity& peer) {
    if (peer.provider_id == telegram::kTelegramProviderId) return false;
    return peer.provider_id == kTextLTProviderId
        ? peer.peer_id.compare(0, kTextLTBrowserPeerPrefix.size(), kTextLTBrowserPeerPrefix) != 0
        : peer.peer_id.compare(0, 10, "anonymous-") != 0;
}

bool Pressed(Event event) {
    return event.is_mouse() && event.mouse().button == Mouse::Left &&
           event.mouse().motion == Mouse::Pressed;
}

void AppendMessageLines(Elements& rows, const std::string& message, int width) {
    std::istringstream input(ClipboardController::NormalizeText(message));
    std::string line;
    while (std::getline(input, line)) {
        std::string row;
        int columns = 0;
        std::replace(line.begin(), line.end(), '\t', ' ');
        for (const auto& glyph : Utf8ToGlyphs(line)) {
            const int cells = string_width(glyph);
            if (columns && columns + cells > width) {
                rows.push_back(text(std::move(row)));
                row.clear();
                columns = 0;
            }
            row += glyph;
            columns += cells;
        }
        rows.push_back(text(std::move(row)));
    }
}

int ScrollDelta(Event event) {
    if (event == Event::PageUp || (event.is_mouse() && event.mouse().button == Mouse::WheelUp)) return -1;
    if (event == Event::PageDown || (event.is_mouse() && event.mouse().button == Mouse::WheelDown)) return 1;
    return 0;
}

} // namespace

ConnectWorkspace::ConnectWorkspace(const Theme& theme, std::function<void()> redraw,
                                   WorkspaceTabs::Callbacks workspaces,
                                   std::function<void()> pick_file,
                                   std::function<std::filesystem::path()> downloads,
                                   std::filesystem::path localsend_identity,
                                   std::function<void()> open_textlt_config,
                                   std::function<void()> open_telegram_settings,
                                   std::function<bool(const std::string&)> write_clipboard)
    : theme_(theme), workspace_tabs_(std::move(workspaces)), redraw_(std::move(redraw)),
      pick_file_(std::move(pick_file)), downloads_(std::move(downloads)),
      write_clipboard_(std::move(write_clipboard)),
      open_telegram_settings_(std::move(open_telegram_settings)),
      message_menu_(theme, [this](const std::string& command) { ExecuteMessageCommand(command); }),
      localsend_identity_(std::move(localsend_identity)) {
    LoadSettings();
    LoadNativeConnections();
    navigator_ = CatchEvent(Renderer([](bool) { return emptyElement(); }),
                            [this](Event event) { return NavigatorEvent(event); });
    messenger_navigator_ = CatchEvent(Renderer([](bool) { return emptyElement(); }),
                                      [this](Event event) { return MessengerNavigatorEvent(event); });
    toggle_native_ = ProviderToggle(native_running_, [this] { ToggleNative(); });
    textlt_config_ = FlatAction("TextLT Config", ButtonRole::Utility, std::move(open_textlt_config));
    toggle_localsend_ = ProviderToggle(localsend_runtime_.listening, [this] { ToggleLocalSend(); });
    auto refresh_local = ButtonOption::Simple();
    refresh_local.on_click = [this] { RefreshLocalSendPeers(); };
    refresh_local.transform = [this](const EntryState& state) {
        const auto terminal = Terminal::Size();
        auto spec = ButtonSpecFromLabel(terminal.dimx < 54 || terminal.dimy <= 26 ? "Refresh" : "Refresh peers",
            ButtonRole::Utility, ButtonVariant::Minimal, ButtonSize::Compact);
        spec.enabled = localsend_runtime_.listening;
        return RenderModalFlatButton(theme_, spec, state.focused);
    };
    refresh_localsend_ = Button(std::move(refresh_local));
    auto connect_by_ip = ButtonOption::Simple();
    connect_by_ip.on_click = [this] { ConnectLocalSendByIp(); };
    connect_by_ip.transform = [this](const EntryState& state) {
        const auto spec = ButtonSpecFromLabel(Terminal::Size().dimx < 54 ? "By IP" : "Connect by IP",
            ButtonRole::Utility, ButtonVariant::Minimal, ButtonSize::Compact);
        return RenderModalFlatButton(theme_, spec, state.focused);
    };
    connect_by_ip_ = Button(std::move(connect_by_ip));
    auto telegram_toggle = ButtonOption::Simple();
    telegram_toggle.on_click = [this] { ToggleTelegram(); };
    telegram_toggle.transform = [this](const EntryState& state) {
        const bool running = telegram_provider_ && telegram_provider_->Running();
        const auto spec = ButtonSpecFromLabel(running ? "Stop" : "Start",
            running ? ButtonRole::Cancel : ButtonRole::Primary,
            ButtonVariant::Minimal, ButtonSize::Compact);
        return RenderModalFlatButton(theme_, spec, state.focused);
    };
    toggle_telegram_ = Button(std::move(telegram_toggle));
    telegram_settings_ = FlatAction(
        "Settings", ButtonRole::Utility, open_telegram_settings_);
    InputOption input;
    input.multiline = false;
    input.cursor_position = &cursor_;
    input.on_enter = [this] { Send(); };
    input.transform = [this](InputState state) {
        return theme_.InputTransform(std::move(state));
    };
    composer_ = ClipboardController::MakeInput(&draft_, "Type a message", std::move(input));
    send_ = FlatAction("Send", ButtonRole::Primary, [this] { Send(); });
    accept_ = FlatAction("Accept", ButtonRole::Primary, [this] { Decide(true); });
    decline_ = FlatAction("Decline", ButtonRole::Cancel, [this] { Decide(false); });
    send_file_ = FlatAction("Send File…", ButtonRole::Utility, [this] {
        if (CanSendFile()) pick_file_();
    });
    cancel_transfer_ = FlatAction("Cancel transfer", ButtonRole::Cancel, [this] { CancelTransfer(); });
    draft_file_ = FlatAction("Send as .txt", ButtonRole::Utility, [this] { SendDraftAsFile(); });
    retry_pin_ = FlatAction("Retry", ButtonRole::Primary, [this] { RetryPin(); });
    InputOption pin_option;
    pin_option.multiline = false;
    pin_option.password = true;
    pin_option.on_enter = [this] { RetryPin(); };
    pin_option.transform = [this](InputState state) { return theme_.InputTransform(std::move(state)); };
    pin_input_ = ClipboardController::MakeInput(&pin_, "PIN", std::move(pin_option));
    Add(draft_file_);
    Add(retry_pin_);
    Add(pin_input_);
    Add(send_file_);
    Add(cancel_transfer_);
    Add(navigator_);
    Add(messenger_navigator_);
    Add(toggle_native_);
    Add(textlt_config_);
    Add(toggle_localsend_);
    Add(refresh_localsend_);
    Add(connect_by_ip_);
    Add(toggle_telegram_);
    Add(telegram_settings_);
    Add(composer_);
    Add(send_);
    Add(accept_);
    Add(decline_);
    active_ = composer_;
    InitializeStructuredControls();
    service_ = std::make_unique<ConnectService>();
    auto telegram = std::make_unique<telegram::TelegramProvider>(
        telegram::MakeTelegramHttpApi(), std::filesystem::path{}, [this] { redraw_(); });
    telegram_provider_ = telegram.get();
    service_->RegisterProvider(std::move(telegram));
    providers_ = {kTextLTProviderId, kLocalSendProviderId, telegram::kTelegramProviderId};

    const auto telegram_settings = telegram_provider_->Settings();
    if (telegram_settings.start_on_launch && !telegram_settings.bot_token.empty()) {
        const auto result = telegram_provider_->Start(
            [this](connect::ProviderEvent event) { QueueProviderEvent(std::move(event)); });
        if (!result.accepted) status_ = result.reason;
    }
}

Component ConnectWorkspace::FlatAction(std::string label, ButtonRole role, std::function<void()> action) const {
    auto option = ButtonOption::Simple();
    option.label = label;
    option.on_click = std::move(action);
    option.transform = [this, spec = ButtonSpecFromLabel(std::move(label), role, ButtonVariant::Minimal,
                                                        ButtonSize::Compact)](const EntryState& state) {
        return RenderModalFlatButton(theme_, spec, state.focused);
    };
    return Button(std::move(option));
}

Component ConnectWorkspace::ProviderToggle(bool& running, std::function<void()> toggle) const {
    auto option = ButtonOption::Simple();
    option.on_click = std::move(toggle);
    option.transform = [this, &running](const EntryState& state) {
        const auto spec = ButtonSpecFromLabel(running ? "Stop" : "Start",
            running ? ButtonRole::Cancel : ButtonRole::Primary, ButtonVariant::Minimal, ButtonSize::Compact);
        return RenderModalFlatButton(theme_, spec, state.focused);
    };
    return Button(std::move(option));
}

ConnectWorkspace::~ConnectWorkspace() {
    diagnostics_cancelled_ = true;
    if (diagnostics_worker_.joinable()) diagnostics_worker_.join();
    Stop();
}

void ConnectWorkspace::UpdateBrowserTheme() {
    const auto& colors = theme_.browser;
    std::lock_guard lock(browser_theme_mutex_);
    browser_theme_ = {colors.background, colors.foreground, colors.input_background,
                      colors.input_foreground, colors.accent, colors.accent_text};
}

connect::BrowserTheme ConnectWorkspace::BrowserTheme() const {
    std::lock_guard lock(browser_theme_mutex_);
    return browser_theme_;
}

void ConnectWorkspace::StartNative() {
    const auto listen_port = Port(settings_.listen_port.empty() ? kDefaultNativePort : settings_.listen_port);
    const auto peer_port = settings_.peer_ip.empty() ? std::optional<std::uint16_t>{0} :
        Port(settings_.peer_port.empty() ? kDefaultNativePort : settings_.peer_port);
    if (!listen_port || !peer_port || (!settings_.peer_ip.empty() && !*peer_port)) {
        status_ = "Enter a port between 1 and 65535. Use 0 to disable the native TCP listener.";
        return;
    }
    const auto is_loopback = [](const std::string& address) {
        return address.compare(0, 4, "127.") == 0;
    };
    if ((*listen_port && is_loopback(settings_.listen_ip)) ||
        (*peer_port && is_loopback(settings_.peer_ip)) ||
        (settings_.discovery && is_loopback(settings_.discovery_ip.empty()
                                           ? settings_.listen_ip : settings_.discovery_ip))) {
        status_ = "Use a non-loopback IPv4 address for Connect";
        return;
    }

    const auto listen_ip = settings_.listen_ip.empty() ? local_ipv4_ : settings_.listen_ip;
    TextLTProviderOptions options;
    options.listen_ip = listen_ip;
    options.listen_port = settings_.peer_fingerprint.empty() ? 0 : *listen_port;
    options.peer_ip = settings_.peer_ip;
    options.peer_port = *peer_port;
    options.probe_manual_peer = true;
    options.structured_capabilities = content_.StructuredCapabilities();
    options.validate_structured = [this](const TransferMetadata& metadata, const StructuredPayload& payload) {
        const auto result = content_.ResolveHandler(metadata.type_id, *metadata.schema_version);
        return result.handler && !result.handler->Validate(*metadata.schema_version, payload);
    };
    options.timeout = std::chrono::seconds(60);
    if (!settings_.peer_fingerprint.empty()) {
        options.tls = TextLTTlsOptions{settings_.certificate, settings_.private_key,
                                       settings_.peer_fingerprint};
    } else if (options.peer_port) {
        status_ = "Choose a saved peer in TextLT Config → Connections before starting TextLT";
        return;
    }
    if (!settings_.peer_id.empty() && options.tls) options.trusted_discovery_peer_id = settings_.peer_id;
    if (settings_.discovery) {
        const auto& ip = settings_.discovery_ip.empty() ? listen_ip : settings_.discovery_ip;
        options.discovery = TextLTDiscoveryOptions{ip, settings_.local_id, settings_.display_name};
    }
    if (IsValidTextLTAddress(listen_ip)) {
        UpdateBrowserTheme();
        options.browser = TextLTBrowserOptions{listen_ip, settings_.certificate, settings_.private_key,
            [this] { return BrowserTheme(); },
            [this](std::uint16_t port) { native_browser_port_ = port; }};
    } else if (!options.listen_port && !options.peer_port && !options.discovery) {
        status_ = "Select a local IPv4 address in TextLT Config → Network";
        return;
    }
    auto provider = MakeTextLTProvider(std::move(options));
    const auto native = provider->Start([this](ProviderEvent event) { QueueProviderEvent(std::move(event)); });
    if (!native.accepted) {
        status_ = native.reason;
        return;
    }
    service_->RegisterProvider(std::move(provider));
    active_settings_ = settings_;
    active_settings_.listen_port = std::to_string(*listen_port);
    active_settings_.listen_ip = listen_ip;
    native_browser_ip_ = listen_ip;
    active_settings_.peer_port = *peer_port ? std::to_string(*peer_port) : "";
    native_running_ = true;
    status_ = settings_.peer_fingerprint.empty() && native_browser_port_
        ? "TextLT browser receiver running. Pair an app for direct TextLT transfers."
        : "TextLT running";
    if (*peer_port || (!settings_.peer_id.empty() && !settings_.peer_fingerprint.empty())) {
        const auto id = !settings_.peer_id.empty() ? settings_.peer_id : kTextLTManualPeerId;
        auto& chat = Get({kTextLTProviderId, id});
        chat.configured = true;
        if (id == kTextLTManualPeerId) chat.name = "Manual TextLT peer";
        if (!selected_) Select({kTextLTProviderId, id});
    }
}

void ConnectWorkspace::ToggleNative() {
    if (native_running_) {
        StopProvider(kTextLTProviderId);
        native_running_ = false;
        status_ = "TextLT stopped";
    } else {
        StartNative();
    }
}

ProviderResult ConnectWorkspace::StartLocalSend(bool test_ports) {
    localsend_runtime_ = {};
    localsend_peer_verified_ = false;
    localsend_phone_reached_listener_ = false;
    auto previous = Diagnostics();
    RefreshDiagnostics(test_ports);
    auto network = Diagnostics();
    if (network.local.name == "macOS" && network.local.gateways.empty()) {
        network.local.gateways = previous.local.gateways;
    }
    const auto& configured = settings_.localsend_interface;
    const auto address = IsAutomaticLocalSendAddress(configured) ? SelectLanIPv4(network.local) : configured;
    localsend_runtime_.interface_ip = address;

    const auto udp_port = Port(settings_.localsend_udp_port.empty() ? kDefaultLocalSendPort : settings_.localsend_udp_port);
    const auto https_port = Port(settings_.localsend_https_port.empty() ? kDefaultLocalSendPort : settings_.localsend_https_port);
    const bool eligible = std::any_of(network.local.addresses.begin(), network.local.addresses.end(),
        [&](const auto& entry) { return entry.ipv4 && entry.usable && entry.address == address; });
    if (!udp_port || !*udp_port || !https_port) {
        localsend_runtime_.error = "Invalid LocalSend UDP or HTTPS port";
    } else if (!eligible) {
        localsend_runtime_.error = address.empty()
            ? "LocalSend Auto could not find an eligible local IPv4 address"
            : "LocalSend interface IPv4 is unavailable on this system";
    }
    if (!localsend_runtime_.error.empty()) return {false, localsend_runtime_.error};

    UpdateBrowserTheme();
    LocalSendOptions options;
    options.browser_theme = [this] { return BrowserTheme(); };
    options.identity_file = localsend_identity_;
    options.alias = settings_.localsend_alias;
    options.multicast_address = settings_.localsend_group;
    options.listen_ip = address;
    options.interface_ip = address;
    options.discovery_port = *udp_port;
    options.receive_port = *https_port;
    for (const auto* environment : {&network.local, &network.host, &previous.host}) {
        for (const auto& entry : environment->addresses) {
            if (entry.ipv4) options.local_addresses.push_back(entry.address);
        }
    }
    options.report_runtime = [this](connect::LocalSendRuntime state) {
        localsend_runtime_ = std::move(state);
    };
    options.report_verified_peer = [this] {
        if (!localsend_peer_verified_.exchange(true)) redraw_();
    };
    options.report_incoming_mobile_peer = [this] {
        if (!localsend_phone_reached_listener_.exchange(true)) redraw_();
    };
    options.report_discovery_change = [this] { redraw_(); };
    if (network.wsl) {
        options.unicast_discovery_subnet = [this]() -> std::optional<LocalSendSubnet> {
            const auto address = SelectWslNatLanIPv4(Diagnostics());
            if (!address) return {};
            return LocalSendSubnet{address->address, address->prefix};
        };
    }
    auto provider = MakeLocalSendProvider(std::move(options));
    auto result = provider->Start([this](ProviderEvent event) { QueueProviderEvent(std::move(event)); });
    if (result.accepted) service_->RegisterProvider(std::move(provider));
    return result;
}

void ConnectWorkspace::ToggleLocalSend() {
    if (localsend_runtime_.listening) {
        StopProvider(kLocalSendProviderId);
        localsend_peer_verified_ = false;
        localsend_phone_reached_listener_ = false;
        if (active_ == refresh_localsend_) {
            FocusColumn(toggle_localsend_, ConnectColumn::LocalConnections);
        }
        status_ = "LocalSend stopped";
        return;
    }
    const auto result = StartLocalSend();
    status_ = result.accepted ? "LocalSend running" : result.reason;
}

void ConnectWorkspace::ToggleTelegram() {
    if (!telegram_provider_) return;
    if (telegram_provider_->Running()) {
        StopProvider(telegram::kTelegramProviderId);
        status_ = "Telegram stopped";
    } else {
        const auto result = telegram_provider_->Start(
            [this](connect::ProviderEvent event) { QueueProviderEvent(std::move(event)); });
        status_ = result.accepted ? "Telegram starting" : result.reason;
    }
    redraw_();
}

bool ConnectWorkspace::Running() const {
    return native_running_ || localsend_runtime_.listening ||
        (telegram_provider_ && telegram_provider_->Running());
}

telegram::TelegramProvider& ConnectWorkspace::Telegram() {
    return *telegram_provider_;
}

void ConnectWorkspace::RefreshLocalSendPeers() {
    if (!localsend_runtime_.listening) return;
    auto* provider = FindLocalSendProvider();
    const auto result = provider->RefreshPeers();
    status_ = result.accepted ? "LocalSend discovery requested" : result.reason;
}

ProviderResult ConnectWorkspace::TestLocalSendDiscovery() {
    if (auto* provider = FindLocalSendProvider()) {
        RefreshDiagnostics(true);
        return provider->RefreshPeers();
    }
    auto result = StartLocalSend(true);
    status_ = result.accepted ? "LocalSend running" : result.reason;
    return result;
}

connect::LocalSendDiscoveryStatus ConnectWorkspace::LocalSendDiscoveryStatus() const {
    if (const auto* provider = FindLocalSendProvider()) return provider->DiscoveryStatus();
    return {};
}

std::string ConnectWorkspace::ActiveNativeListenPort() const {
    if (!native_running_ || active_settings_.peer_fingerprint.empty()) return {};
    const auto port = Port(active_settings_.listen_port.empty() ? kDefaultNativePort : active_settings_.listen_port);
    return port && *port ? std::to_string(*port) : std::string{};
}

std::string ConnectWorkspace::ActiveNativeListenAddress() const {
    return native_running_ ? active_settings_.listen_ip : std::string{};
}

ProviderResult ConnectWorkspace::StartNativeListener() {
    const auto port = Port(settings_.listen_port.empty() ? kDefaultNativePort : settings_.listen_port);
    if (!port || !*port) return {false, "Set a TCP listen port in TextLT Config → Network first."};
    if (settings_.peer_fingerprint.empty()) {
        return {false, "Choose a saved peer in TextLT Config → Connections before starting the native listener."};
    }
    if (!native_running_) StartNative();
    if (!ActiveNativeListenPort().empty()) return {true, {}};
    return {false, native_running_ ? "Stop TextLT in the sidebar and start it with the new listen settings." : status_};
}

void ConnectWorkspace::StopProvider(std::string_view provider) {
    if (auto* transport = service_->FindProvider(provider)) transport->Stop();
    if (provider != telegram::kTelegramProviderId) {
        service_->UnregisterProvider(provider);
    }
    DrainEvents();
    if (selected_ && selected_->first == provider) pin_.clear();
    for (auto& [key, chat] : conversations_) {
        if (key.first != provider) continue;
        chat.online = false;
        chat.configured = false;
        chat.supports_files = false;
        chat.authenticated_capabilities.clear();
        chat.active_transfer.reset();
        chat.waiting_for_pin = false;
        chat.pin.clear();
        chat.offer.reset();
        chat.telegram_offers.clear();
    }
}

void ConnectWorkspace::Stop() {
    for (const auto& provider : providers_) StopProvider(provider);
    native_running_ = false;
    localsend_runtime_ = {};
    localsend_peer_verified_ = false;
    localsend_phone_reached_listener_ = false;
    status_ = "Network stopped";
}

ILocalSendProvider* ConnectWorkspace::FindLocalSendProvider() const {
    return static_cast<ILocalSendProvider*>(service_->FindProvider(kLocalSendProviderId));
}

void ConnectWorkspace::ConnectLocalSendByIp() {
    auto* provider = FindLocalSendProvider();
    if (!provider || !localsend_runtime_.listening) {
        status_ = "Start LocalSend before connecting by IP";
        return;
    }
    if (settings_.localsend_peer_endpoint.empty()) {
        status_ = "Enter the peer IP in Connect Settings → LocalSend";
        return;
    }
    const auto result = provider->ConnectByIp(settings_.localsend_peer_endpoint);
    status_ = result.accepted ? "Connecting to LocalSend peer…" : result.reason;
}

void ConnectWorkspace::QueueProviderEvent(ProviderEvent event) {
    {
        std::lock_guard lock(event_mutex_);
        events_.push_back(std::move(event));
    }
    redraw_();
}

void ConnectWorkspace::DrainEvents() {
    std::deque<ProviderEvent> events;
    {
        std::lock_guard lock(event_mutex_);
        events.swap(events_);
    }
    for (const auto& event : events) {
        std::visit([this](const auto& item) { Handle(item); }, event);
    }
}

ConnectWorkspace::Conversation& ConnectWorkspace::Get(const PeerIdentity& peer) {
    auto& chat = conversations_[{peer.provider_id, peer.peer_id}];
    if (chat.name.empty()) chat.name = peer.peer_id;
    return chat;
}

ConnectWorkspace::Conversation* ConnectWorkspace::Selected() {
    return selected_ ? &conversations_.at(*selected_) : nullptr;
}

void ConnectWorkspace::Select(const PeerKey& key) {
    if (selected_ == key) return;
    CloseMessageMenu();
    if (auto* chat = Selected()) {
        chat->draft = draft_;
        chat->pin = pin_;
    }
    const bool first = !selected_;
    selected_ = key;
    auto& selected_chat = Get({key.first, key.second});
    if (!first) draft_ = selected_chat.draft;
    cursor_ = static_cast<int>(draft_.size());
    pin_ = selected_chat.pin;
    if (key.first == telegram::kTelegramProviderId) EnsureSelectedMessengerPeerVisible();
    else EnsureSelectedPeerVisible();
}

void ConnectWorkspace::Send() {
    if (draft_.empty()) return;
    if (!Running() || !selected_) {
        status_ = Running() ? "Select a connection to send"
            : selected_ && selected_->first == telegram::kTelegramProviderId
                ? "Start Telegram before sending" : "Start networking before sending";
        return;
    }
    auto* provider = service_->FindProvider(selected_->first);
    if (!provider) {
        status_ = selected_->first == kLocalSendProviderId ? "Start LocalSend before sending"
                                                         : "Start TextLT networking before sending";
        return;
    }

    if (OffersTextFile()) {
        status_ = "Inline text is limited to 16384 bytes. Use Send as .txt.";
        return;
    }
    const auto id = "message-" + std::to_string(++next_message_);
    const auto result = provider->Send({
        {{selected_->first, PeerRoute()}, id}, {kTextTypeId, {}, {}, {}}, TextPayload{draft_}
    });
    if (!result.accepted) {
        status_ = result.reason;
        return;
    }
    auto& chat = *Selected();
    const bool telegram_message = selected_->first == telegram::kTelegramProviderId;
    chat.messages.push_back({id, std::move(draft_),
        telegram_message ? "Sending" : "Waiting for acceptance", true});
    if (selected_->first == kLocalSendProviderId) {
        chat.active_transfer = std::make_pair(TransferIdentity{{selected_->first, PeerRoute()}, id},
                                               TransferDirection::Outgoing);
    }
    chat.scroll = 0;
    draft_.clear();
    cursor_ = 0;
    status_ = telegram_message ? "Sending via Telegram" : "Waiting for acceptance";
}

bool ConnectWorkspace::CanSendFile() {
    const auto* chat = Selected();
    if (!Running() || !chat || !chat->online || !chat->supports_files) return false;
    if (selected_->first == kLocalSendProviderId) return true;
    if (selected_->first == telegram::kTelegramProviderId) {
        return telegram_provider_->Running() && !chat->active_transfer &&
            telegram_provider_->IsAuthorized(selected_->second);
    }
    return !active_settings_.peer_fingerprint.empty() &&
           (selected_->second == active_settings_.peer_id ||
            (selected_->second == kTextLTManualPeerId && !active_settings_.peer_port.empty()));
}

std::string ConnectWorkspace::PeerRoute() const {
    const auto& peer = selected_->second;
    return selected_->first == kTextLTProviderId && !active_settings_.peer_port.empty() && peer == active_settings_.peer_id
        ? kTextLTManualPeerId : peer;
}

bool ConnectWorkspace::SendFile(const std::filesystem::path& path, std::string& error) {
    if (!CanSendFile()) {
        error = "This peer is not configured for file transfers";
        return false;
    }
    std::error_code status;
    if (!std::filesystem::is_regular_file(path, status)) {
        error = "Select a regular file";
        return false;
    }
    const auto size = std::filesystem::file_size(path, status);
    if (status) {
        error = "Could not read the file size";
        return false;
    }
    auto input = std::make_unique<std::ifstream>(path, std::ios::binary);
    if (!*input) {
        error = "Could not open the file";
        return false;
    }
    return QueueFile(path.filename().u8string(), size, std::move(input), error);
}

bool ConnectWorkspace::OffersTextFile() {
    return CanSendFile() && selected_->first == kLocalSendProviderId &&
        draft_.size() > kLocalSendInlineTextLimit;
}

void ConnectWorkspace::SendDraftAsFile() {
    if (!OffersTextFile()) return;
    std::string error;
    if (!QueueFile("message.txt", draft_.size(), std::make_unique<std::istringstream>(draft_), error)) {
        status_ = std::move(error);
        return;
    }
    draft_.clear();
    cursor_ = 0;
}

bool ConnectWorkspace::QueueFile(std::string name, std::uint64_t size,
                                 std::unique_ptr<std::istream> input, std::string& error) {
    const auto id = "file-" + std::to_string(++next_message_);
    TransferIdentity identity{{selected_->first, PeerRoute()}, id};
    auto* provider = service_->FindProvider(selected_->first);
    const auto result = provider->Send({
        identity, {kFileTypeId, {}, name, size}, std::move(input)
    });
    if (!result.accepted) {
        error = result.reason;
        return false;
    }
    auto& chat = *Selected();
    const auto pending = selected_->first == telegram::kTelegramProviderId
        ? "Queued for upload" : "Waiting for acceptance";
    chat.messages.push_back({id, name + " (" + std::to_string(size) + " bytes)",
                             pending, true, true});
    chat.active_transfer = std::make_pair(std::move(identity), TransferDirection::Outgoing);
    chat.scroll = 0;
    status_ = pending;
    return true;
}

void ConnectWorkspace::CancelTransfer() {
    auto* chat = Selected();
    if (!chat || !chat->active_transfer) return;
    const auto& [identity, direction] = *chat->active_transfer;
    auto* provider = service_->FindProvider(identity.peer.provider_id);
    const auto result = provider->Cancel(identity, direction);
    status_ = result.accepted ? "Cancelling transfer" : result.reason;
}

void ConnectWorkspace::RetryPin() {
    auto* chat = Selected();
    if (!chat || !chat->waiting_for_pin || !chat->active_transfer) return;
    const auto& identity = chat->active_transfer->first;
    const auto result = service_->FindProvider(identity.peer.provider_id)->RetryWithPin(identity, pin_);
    if (result.accepted) {
        pin_.clear();
        chat->pin.clear();
        chat->waiting_for_pin = false;
        FocusColumn(composer_, ConnectColumn::Conversation);
        status_ = "Retrying LocalSend offer";
    } else {
        status_ = result.reason;
    }
}

void ConnectWorkspace::Decide(bool accept) {
    auto* chat = Selected();
    if (!Running() || !chat || !chat->offer) return;
    const PeerKey peer{chat->offer->identity.peer.provider_id, chat->offer->identity.peer.peer_id};
    const bool remember = CanRemember(chat->offer->identity.peer) && !auto_accept_peers_.count(peer);
    const auto result = ResolveOffer(*chat, accept, accept && remember);
    if (!accept) {
        status_ = result.accepted ? "Offer declined" : result.reason;
    } else if (result.accepted && remember) {
        status_ = auto_accept_peers_.count(peer)
            ? "Offer accepted. Future requests from this app will be accepted automatically."
            : "Offer accepted, but the automatic-accept preference could not be saved.";
    } else {
        status_ = result.accepted ? "Offer accepted" : result.reason;
    }
}

ProviderResult ConnectWorkspace::ResolveOffer(Conversation& chat, bool accept, bool remember_app) {
    if (!chat.offer) return {false, "This offer is no longer available."};
    const auto& id = chat.offer->identity;
    auto* provider = service_->FindProvider(id.peer.provider_id);
    if (!provider) return {false, "The connection is no longer available."};
    ProviderResult result;
    if (!accept) {
        result = provider->Decline(id, "Declined by recipient");
    } else if (chat.offer->metadata.type_id == kFileTypeId) {
        result = provider->AcceptFile(id, [downloads = downloads_, name = *chat.offer->metadata.display_name] {
            return MakeDownloadDestination(downloads(), name);
        });
    } else {
        result = provider->Accept(id, nullptr);
    }
    if (result.accepted && accept && remember_app) {
        const PeerKey peer{id.peer.provider_id, id.peer.peer_id};
        auto_accept_peers_.insert(peer);
        if (!SaveSettings()) auto_accept_peers_.erase(peer);
    }
    if (result.accepted) {
        chat.offer.reset();
        if (Selected() == &chat) FocusColumn(composer_, ConnectColumn::Conversation);
    }
    return result;
}

void ConnectWorkspace::Handle(const Peer& peer) {
    auto identity = peer.identity;
    const bool manual = identity.peer_id == kTextLTManualPeerId;
    if (manual && !active_settings_.peer_id.empty()) identity.peer_id = active_settings_.peer_id;
    auto& chat = Get(identity);
    if (!manual || identity.peer_id == kTextLTManualPeerId) chat.name = peer.display_name;
    if (manual || active_settings_.peer_port.empty() || identity.peer_id != active_settings_.peer_id) {
        chat.supports_files = std::any_of(peer.capabilities.begin(), peer.capabilities.end(),
            [](const Capability& capability) { return capability.type_id == kFileTypeId; });
    }
    chat.online = peer.online;
    if (!manual) chat.protocol = peer.protocol_version;
    if (!selected_) Select({identity.provider_id, identity.peer_id});
}

void ConnectWorkspace::Handle(const PeerCapabilities& peer) {
    auto identity = peer.identity;
    const bool manual = identity.peer_id == kTextLTManualPeerId;
    if (manual && !active_settings_.peer_id.empty()) identity.peer_id = active_settings_.peer_id;
    auto& chat = Get(identity);
    chat.authenticated_capabilities = peer.capabilities;
    if (manual) chat.online = true;
    chat.supports_files = std::any_of(peer.capabilities.begin(), peer.capabilities.end(),
        [](const Capability& capability) { return capability.type_id == kFileTypeId; });
}

void ConnectWorkspace::Handle(const IncomingOffer& offer) {
    auto& chat = Get(offer.identity.peer);
    const bool telegram_offer = offer.identity.peer.provider_id == telegram::kTelegramProviderId;
    if (telegram_offer && (chat.offer || chat.active_transfer)) {
        chat.telegram_offers.push_back(offer);
        return;
    }
    chat.offer = offer;
    const bool local_send = offer.identity.peer.provider_id == kLocalSendProviderId;
    const auto* current = Selected();
    const bool browser = !local_send && !telegram_offer && !CanRemember(offer.identity.peer);
    if (!current || ((local_send || browser) && !current->offer)) {
        Select({offer.identity.peer.provider_id, offer.identity.peer.peer_id});
    }
    const bool file = offer.metadata.type_id == kFileTypeId;
    if (file) {
        const auto size = offer.metadata.byte_length
            ? std::to_string(*offer.metadata.byte_length) + " bytes" : "size unknown";
        chat.messages.push_back({offer.identity.transfer_id,
            offer.metadata.display_name.value_or("download") + " (" + size + ")",
            "Waiting for acceptance", false, true});
        chat.active_transfer = std::make_pair(offer.identity, TransferDirection::Incoming);
        chat.scroll = 0;
    }
    if (!file && (local_send || browser)) {
        chat.active_transfer = std::make_pair(offer.identity, TransferDirection::Incoming);
    }
    status_ = file ? "Incoming file offer from " + chat.name
        : offer.metadata.schema_version ? "Incoming item: " + offer.metadata.display_name.value_or(offer.metadata.type_id)
                                        : "Incoming text offer from " + chat.name;
    const PeerKey peer{offer.identity.peer.provider_id, offer.identity.peer.peer_id};
    if (!telegram_offer && auto_accept_peers_.count(peer)) {
        const auto result = ResolveOffer(chat, true, false);
        status_ = result.accepted ? "Accepted automatically from " + chat.name : result.reason;
    }
}

void ConnectWorkspace::Handle(const ReceivedContent& content) {
    auto& chat = Get(content.identity.peer);
    if (const auto* text = std::get_if<TextPayload>(&content.payload)) {
        chat.messages.push_back({content.identity.transfer_id, text->utf8, {}, false});
    } else if (std::holds_alternative<StructuredPayload>(content.payload)) {
        const auto found = std::find_if(chat.messages.begin(), chat.messages.end(), [&](const auto& message) {
            return message.item && message.id == content.identity.transfer_id;
        });
        if (found == chat.messages.end()) {
            chat.messages.push_back({content.identity.transfer_id,
                content.metadata.display_name.value_or(content.metadata.type_id), "Received item", false, false,
                content, "Ready to import"});
        }
    }
    chat.scroll = 0;
}

void ConnectWorkspace::Handle(const TransferEvent& transfer) {
    const bool outgoing = transfer.direction == TransferDirection::Outgoing;
    auto peer = transfer.identity.peer;
    if (outgoing && peer.peer_id == kTextLTManualPeerId && !active_settings_.peer_id.empty()) {
        peer.peer_id = active_settings_.peer_id;
    }
    auto& chat = Get(peer);
    const bool telegram_file = peer.provider_id == telegram::kTelegramProviderId;
    if (telegram_file && !outgoing) {
        const auto queued = std::find_if(chat.telegram_offers.begin(), chat.telegram_offers.end(),
            [&](const IncomingOffer& offer) { return offer.identity.transfer_id == transfer.identity.transfer_id; });
        if (queued != chat.telegram_offers.end()) {
            chat.telegram_offers.erase(queued);
            return;
        }
    }
    if (transfer.state == TransferState::Pending && transfer.failure == TransferFailure::PinRequired) {
        chat.waiting_for_pin = true;
    }
    const auto summary = !outgoing && transfer.state == TransferState::Completed
        ? "Received" : telegram_file && !outgoing && transfer.state == TransferState::InProgress
            ? "Downloading" : TransferLabel(transfer.state);
    status_ = transfer.reason ? summary + ": " + *transfer.reason : summary;
    if (transfer.state == TransferState::InProgress && transfer.bytes_transferred && transfer.total_bytes) {
        status_ = std::to_string(*transfer.bytes_transferred) + " / " +
                  std::to_string(*transfer.total_bytes) + " bytes";
    } else if (telegram_file && !outgoing && transfer.state == TransferState::InProgress &&
               transfer.bytes_transferred) {
        status_ = "Downloaded " + std::to_string(*transfer.bytes_transferred) + " bytes";
    }
    for (auto message = chat.messages.rbegin(); message != chat.messages.rend(); ++message) {
        if ((outgoing || message->file) && message->outgoing == outgoing &&
            message->id == transfer.identity.transfer_id) {
            message->status = status_;
            break;
        }
    }
    if (!outgoing && chat.offer && chat.offer->identity.transfer_id == transfer.identity.transfer_id &&
        transfer.state != TransferState::InProgress) chat.offer.reset();
    if (chat.active_transfer && chat.active_transfer->first.transfer_id == transfer.identity.transfer_id &&
        chat.active_transfer->second == transfer.direction && transfer.state != TransferState::InProgress &&
        transfer.state != TransferState::Pending) {
        chat.active_transfer.reset();
        chat.waiting_for_pin = false;
        chat.pin.clear();
        if (selected_ == PeerKey{peer.provider_id, peer.peer_id}) {
            pin_.clear();
            if (active_ == cancel_transfer_ || active_ == pin_input_ || active_ == retry_pin_) active_ = composer_;
        }
    }
    if (auto* selected = Selected(); selected && !selected->offer &&
        (active_ == accept_ || active_ == decline_)) {
        active_ = composer_;
    }
    if (!chat.offer && !chat.active_transfer && !chat.telegram_offers.empty()) {
        auto next = std::move(chat.telegram_offers.front());
        chat.telegram_offers.pop_front();
        Handle(next);
    }
}

void ConnectWorkspace::Handle(const IncomingConnectionFailure& failure) {
    status_ = failure.reason;
}

void ConnectWorkspace::Handle(const LocalSendConnectResult& result) {
    status_ = result.message;
    if (result.peer) Select({result.peer->provider_id, result.peer->peer_id});
}

Component ConnectWorkspace::ActiveChild() {
    return active_;
}

void ConnectWorkspace::SetActiveChild(ComponentBase* child) {
    for (const auto& component : children()) {
        if (component.get() != child) continue;
        active_ = component;
        if (component == navigator_ || component == toggle_native_ || component == textlt_config_ ||
            component == toggle_localsend_ || component == refresh_localsend_ ||
            component == connect_by_ip_) {
            navigation_state_.Focus(ConnectColumn::LocalConnections);
        }
        else if (component == messenger_navigator_ || component == toggle_telegram_ ||
                 component == telegram_settings_) {
            navigation_state_.Focus(ConnectColumn::Messengers);
        }
        else navigation_state_.Focus(ConnectColumn::Conversation);
        return;
    }
}

void ConnectWorkspace::FocusColumn(Component component, ConnectColumn column) {
    if (!component) return;
    active_ = component;
    navigation_state_.Focus(column);
    component->TakeFocus();
}

void ConnectWorkspace::UpdateNavigatorScrollLimits() {
    if (!navigator_box_.IsEmpty()) {
        const int height = navigator_box_.y_max - navigator_box_.y_min + 1;
        navigator_visible_rows_ = std::max(1, height - 4);
    }
    navigator_scroll_max_ = std::max(0, navigator_content_rows_ - navigator_visible_rows_);
    navigation_state_.Scroll(ConnectColumn::LocalConnections, 0, navigator_scroll_max_);
}

void ConnectWorkspace::EnsureSelectedPeerVisible() {
    UpdateNavigatorScrollLimits();
    if (!selected_) return;
    const auto row = std::find_if(peer_rows_.begin(), peer_rows_.end(),
        [this](const PeerRow& entry) { return entry.key == *selected_; });
    if (row == peer_rows_.end()) return;

    const int offset = navigation_state_.ScrollOffset(ConnectColumn::LocalConnections);
    int delta = 0;
    if (row->content_row < offset) {
        delta = row->content_row - offset;
    } else if (row->content_row >= offset + navigator_visible_rows_) {
        delta = row->content_row - (offset + navigator_visible_rows_ - 1);
    }
    navigation_state_.Scroll(ConnectColumn::LocalConnections, delta, navigator_scroll_max_);
}

void ConnectWorkspace::UpdateMessengerScrollLimits() {
    if (!messenger_box_.IsEmpty()) {
        const int height = messenger_box_.y_max - messenger_box_.y_min + 1;
        messenger_visible_rows_ = std::max(1, height - 6);
    }
    messenger_scroll_max_ = std::max(0, messenger_content_rows_ - messenger_visible_rows_);
    navigation_state_.Scroll(ConnectColumn::Messengers, 0, messenger_scroll_max_);
}

void ConnectWorkspace::EnsureSelectedMessengerPeerVisible() {
    UpdateMessengerScrollLimits();
    if (!selected_) return;
    const auto row = std::find_if(messenger_peer_rows_.begin(), messenger_peer_rows_.end(),
        [this](const PeerRow& entry) { return entry.key == *selected_; });
    if (row == messenger_peer_rows_.end()) return;

    const int offset = navigation_state_.ScrollOffset(ConnectColumn::Messengers);
    int delta = 0;
    if (row->content_row < offset) {
        delta = row->content_row - offset;
    } else if (row->content_row >= offset + messenger_visible_rows_) {
        delta = row->content_row - (offset + messenger_visible_rows_ - 1);
    }
    navigation_state_.Scroll(ConnectColumn::Messengers, delta, messenger_scroll_max_);
}

bool ConnectWorkspace::Paste(Event event) {
    if (!ClipboardController::IsPasteEvent(event)) return false;
    if (pending_share_ || preview_) return true;
    if (auto* chat = Selected(); chat && chat->waiting_for_pin && pin_input_->Focused()) {
        return pin_input_->OnEvent(std::move(event));
    }
    FocusColumn(composer_, ConnectColumn::Conversation);
    return composer_->OnEvent(std::move(event));
}

bool ConnectWorkspace::HandleMessageMenuEvent(Event event) {
    if (message_menu_.IsOpen()) {
        const bool handled = message_menu_.OnEvent(event);
        if (!message_menu_.IsOpen()) message_menu_index_.reset();
        if (handled) return true;
    }
    if (!event.is_mouse() || pending_share_ || preview_) return false;
    const auto& mouse = event.mouse();
    if (mouse.button != Mouse::Right || mouse.motion != Mouse::Pressed ||
        !history_box_.Contain(mouse.x, mouse.y)) return false;

    const auto row = std::find_if(message_rows_.begin(), message_rows_.end(), [&](const auto& entry) {
        return entry.second.Contain(mouse.x, mouse.y);
    });
    if (row == message_rows_.end()) return false;
    const auto* chat = Selected();
    if (!chat || row->first >= chat->messages.size()) return false;
    const auto& message = chat->messages[row->first];
    const bool can_copy = write_clipboard_ && !message.text.empty();
    message_menu_index_ = row->first;
    message_menu_.Open(mouse.x, mouse.y, {
        {"copy", "Copy", can_copy},
        {"delete", "Delete"},
        {"cut", "Cut", can_copy},
    });
    return true;
}

void ConnectWorkspace::CloseMessageMenu() {
    message_menu_.Close();
    message_menu_index_.reset();
}

Element ConnectWorkspace::RenderMessageMenu() {
    return message_menu_.Render();
}

void ConnectWorkspace::ExecuteMessageCommand(const std::string& command) {
    const auto index = std::exchange(message_menu_index_, std::nullopt);
    auto* chat = Selected();
    if (!chat || !index || *index >= chat->messages.size()) return;
    const auto message = chat->messages.begin() + *index;
    if (command != "delete") {
        if (!write_clipboard_ || !write_clipboard_(message->text)) {
            status_ = ClipboardController::kWriteFailed;
            return;
        }
        if (command == "copy") {
            status_ = "Message copied";
            return;
        }
    }
    chat->messages.erase(message);
    status_ = command == "cut" ? "Message cut" : "Message deleted";
}

bool ConnectWorkspace::OnEvent(Event event) {
    if (event.is_mouse()) {
        const auto mouse = event.mouse();
        if (Pressed(event) && workspace_tabs_.ActivateAt(mouse.x, mouse.y)) return true;
        if (navigator_box_.Contain(mouse.x, mouse.y)) return NavigatorEvent(event);
        if (messenger_box_.Contain(mouse.x, mouse.y)) return MessengerNavigatorEvent(event);
    }
    if ((pending_share_ || preview_) && StructuredEvent(event)) return true;
    if (Paste(event)) return true;
    if (event.is_mouse()) {
        const auto mouse = event.mouse();
        if (history_box_.Contain(mouse.x, mouse.y)) {
            if (auto* chat = Selected(); chat && ScrollDelta(event)) {
                chat->scroll = std::clamp(chat->scroll - ScrollDelta(event) * 3, 0,
                                          std::max(0, history_rows_ - 1));
                return true;
            }
            if (Pressed(event)) {
                for (const auto& [index, box] : preview_rows_boxes_) {
                    if (box.Contain(mouse.x, mouse.y)) {
                        OpenPreview(index);
                        return true;
                    }
                }
                FocusColumn(composer_, ConnectColumn::Conversation);
                return true;
            }
        }
        if (auto* chat = Selected(); chat && chat->offer) {
            if (accept_->OnEvent(event)) {
                FocusColumn(accept_, ConnectColumn::Conversation);
                return true;
            }
            if (decline_->OnEvent(event)) {
                FocusColumn(decline_, ConnectColumn::Conversation);
                return true;
            }
        }
        if (Selected() && Selected()->waiting_for_pin) {
            if (pin_input_->OnEvent(event)) {
                FocusColumn(pin_input_, ConnectColumn::Conversation);
                return true;
            }
            if (retry_pin_->OnEvent(event)) {
                FocusColumn(retry_pin_, ConnectColumn::Conversation);
                return true;
            }
        }
        if (OffersTextFile() && draft_file_->OnEvent(event)) {
            FocusColumn(draft_file_, ConnectColumn::Conversation);
            return true;
        }
        if (CanSendFile() && send_file_->OnEvent(event)) {
            FocusColumn(send_file_, ConnectColumn::Conversation);
            return true;
        }
        if (Selected() && Selected()->active_transfer && cancel_transfer_->OnEvent(event)) {
            FocusColumn(cancel_transfer_, ConnectColumn::Conversation);
            return true;
        }
        if (composer_->OnEvent(event)) {
            FocusColumn(composer_, ConnectColumn::Conversation);
            return true;
        }
        if (send_->OnEvent(event)) {
            FocusColumn(send_, ConnectColumn::Conversation);
            return true;
        }
        if (Pressed(event) && conversation_box_.Contain(mouse.x, mouse.y)) {
            FocusColumn(composer_, ConnectColumn::Conversation);
            return true;
        }
        return false;
    }
    if (!Focused()) return false;
    if (event == Event::Tab || event == Event::TabReverse) {
        Components choices{navigator_, toggle_native_, textlt_config_, toggle_localsend_};
        if (localsend_runtime_.listening) choices.push_back(refresh_localsend_);
        choices.push_back(connect_by_ip_);
        choices.push_back(messenger_navigator_);
        choices.push_back(toggle_telegram_);
        choices.push_back(telegram_settings_);
        choices.push_back(composer_);
        choices.push_back(send_);
        if (CanSendFile()) choices.push_back(send_file_);
        if (OffersTextFile()) choices.push_back(draft_file_);
        if (Selected() && Selected()->waiting_for_pin) {
            choices.push_back(pin_input_);
            choices.push_back(retry_pin_);
        }
        if (Selected() && Selected()->active_transfer) choices.push_back(cancel_transfer_);
        if (auto* chat = Selected(); chat && chat->offer) {
            choices.push_back(accept_);
            choices.push_back(decline_);
        }
        const auto current = std::find(choices.begin(), choices.end(), active_);
        const int total = static_cast<int>(choices.size());
        const int index = current == choices.end() ? 0 : static_cast<int>(current - choices.begin());
        const Component next =
            choices[(index + total + (event == Event::Tab ? 1 : -1)) % total];
        if (next == navigator_ || next == toggle_native_ || next == textlt_config_ ||
            next == toggle_localsend_ || next == refresh_localsend_ || next == connect_by_ip_) {
            FocusColumn(next, ConnectColumn::LocalConnections);
        } else if (next == messenger_navigator_ || next == toggle_telegram_ ||
                   next == telegram_settings_) {
            FocusColumn(next, ConnectColumn::Messengers);
        } else {
            FocusColumn(next, ConnectColumn::Conversation);
        }
        return true;
    }
    if (ScrollDelta(event) && navigation_state_.FocusedColumn() == ConnectColumn::LocalConnections) {
        UpdateNavigatorScrollLimits();
        navigation_state_.Scroll(ConnectColumn::LocalConnections,
                                 ScrollDelta(event) * 5, navigator_scroll_max_);
        return true;
    }
    if (ScrollDelta(event) && navigation_state_.FocusedColumn() == ConnectColumn::Messengers) {
        navigation_state_.Scroll(ConnectColumn::Messengers, ScrollDelta(event) * 5,
                                 messenger_scroll_max_);
        return true;
    }
    if (ScrollDelta(event)) {
        if (auto* chat = Selected()) {
            chat->scroll = std::clamp(chat->scroll - ScrollDelta(event) * 5, 0,
                                      std::max(0, history_rows_ - 1));
        }
        return true;
    }
    return active_->OnEvent(event);
}

bool ConnectWorkspace::NavigatorEvent(Event event) {
    if (event.is_mouse()) {
        if (toggle_native_->OnEvent(event)) {
            FocusColumn(toggle_native_, ConnectColumn::LocalConnections);
            return true;
        }
        if (textlt_config_->OnEvent(event)) {
            FocusColumn(textlt_config_, ConnectColumn::LocalConnections);
            return true;
        }
        if (toggle_localsend_->OnEvent(event)) {
            FocusColumn(toggle_localsend_, ConnectColumn::LocalConnections);
            return true;
        }
        if (localsend_runtime_.listening && refresh_localsend_->OnEvent(event)) {
            FocusColumn(refresh_localsend_, ConnectColumn::LocalConnections);
            return true;
        }
        if (connect_by_ip_->OnEvent(event)) {
            FocusColumn(connect_by_ip_, ConnectColumn::LocalConnections);
            return true;
        }
        if (Pressed(event)) {
            const auto mouse = event.mouse();
            for (const auto& row : peer_rows_) {
                if (row.box.Contain(mouse.x, mouse.y)) {
                    CloseStructuredPicker();
                    Select(row.key);
                    FocusColumn(navigator_, ConnectColumn::LocalConnections);
                    return true;
                }
            }
            FocusColumn(navigator_, ConnectColumn::LocalConnections);
            return true;
        }
        const int scroll_delta = ScrollDelta(event);
        if (scroll_delta) {
            UpdateNavigatorScrollLimits();
            navigation_state_.Scroll(ConnectColumn::LocalConnections,
                                     scroll_delta * 3, navigator_scroll_max_);
            FocusColumn(navigator_, ConnectColumn::LocalConnections);
            return true;
        }
        return false;
    }
    const int scroll_delta = ScrollDelta(event);
    if (scroll_delta) {
        UpdateNavigatorScrollLimits();
        navigation_state_.Scroll(ConnectColumn::LocalConnections,
                                 scroll_delta * 5, navigator_scroll_max_);
        return true;
    }
    if (event == Event::ArrowUp || event == Event::ArrowDown) {
        UpdateNavigatorScrollLimits();
        const int delta = event == Event::ArrowUp ? -1 : 1;
        if (peer_rows_.empty()) return true;
        const auto row = std::find_if(peer_rows_.begin(), peer_rows_.end(),
                                      [this](const PeerRow& entry) { return selected_ == entry.key; });
        const int index = row == peer_rows_.end() ? 0 : static_cast<int>(row - peer_rows_.begin());
        CloseStructuredPicker();
        Select(peer_rows_[std::clamp(index + delta, 0, static_cast<int>(peer_rows_.size()) - 1)].key);
        FocusColumn(navigator_, ConnectColumn::LocalConnections);
        return true;
    }
    if (event == Event::Return && selected_) {
        FocusColumn(composer_, ConnectColumn::Conversation);
        return true;
    }
    return false;
}

bool ConnectWorkspace::MessengerNavigatorEvent(Event event) {
    if (event.is_mouse()) {
        if (toggle_telegram_->OnEvent(event)) {
            FocusColumn(toggle_telegram_, ConnectColumn::Messengers);
            return true;
        }
        if (telegram_settings_->OnEvent(event)) {
            FocusColumn(telegram_settings_, ConnectColumn::Messengers);
            return true;
        }
        if (Pressed(event)) {
            const auto mouse = event.mouse();
            for (const auto& row : messenger_peer_rows_) {
                if (!row.box.Contain(mouse.x, mouse.y)) continue;
                Select(row.key);
                FocusColumn(messenger_navigator_, ConnectColumn::Messengers);
                return true;
            }
            FocusColumn(messenger_navigator_, ConnectColumn::Messengers);
            return true;
        }
        const int scroll_delta = ScrollDelta(event);
        if (scroll_delta) {
            UpdateMessengerScrollLimits();
            navigation_state_.Scroll(ConnectColumn::Messengers, scroll_delta * 3,
                                     messenger_scroll_max_);
            FocusColumn(messenger_navigator_, ConnectColumn::Messengers);
            return true;
        }
        return false;
    }

    if (ScrollDelta(event)) {
        UpdateMessengerScrollLimits();
        navigation_state_.Scroll(ConnectColumn::Messengers, ScrollDelta(event) * 5,
                                 messenger_scroll_max_);
        return true;
    }
    if (event == Event::ArrowUp || event == Event::ArrowDown) {
        if (messenger_peer_rows_.empty()) return true;
        const auto row = std::find_if(messenger_peer_rows_.begin(), messenger_peer_rows_.end(),
            [this](const PeerRow& entry) { return selected_ == entry.key; });
        const int index = row == messenger_peer_rows_.end()
            ? 0 : static_cast<int>(row - messenger_peer_rows_.begin());
        const int delta = event == Event::ArrowUp ? -1 : 1;
        const auto& key = messenger_peer_rows_[std::clamp(
            index + delta, 0, static_cast<int>(messenger_peer_rows_.size()) - 1)].key;
        Select(key);
        FocusColumn(messenger_navigator_, ConnectColumn::Messengers);
        return true;
    }
    if (event == Event::Return && selected_) {
        FocusColumn(composer_, ConnectColumn::Conversation);
        return true;
    }
    if (event == Event::PageUp || event == Event::PageDown ||
        event == Event::ArrowUp || event == Event::ArrowDown) {
        const int delta = event == Event::PageUp || event == Event::ArrowUp ? -3 : 3;
        navigation_state_.Scroll(ConnectColumn::Messengers, delta, messenger_scroll_max_);
        return true;
    }
    return false;
}

Element ConnectWorkspace::RenderNavigator(int width) {
    peer_rows_.clear();
    peer_rows_.reserve(conversations_.size());
    navigator_content_rows_ = 0;
    const auto button = [](const Component& component) {
        auto rendered = component->Render();
        return component->Focused() ? rendered | focus : rendered;
    };
    const bool controls_focused = toggle_native_->Focused() || textlt_config_->Focused() || toggle_localsend_->Focused() ||
        refresh_localsend_->Focused() || connect_by_ip_->Focused();
    const bool compact = Terminal::Size().dimy <= 26;
    Elements rows;
    int wrapped_content_rows = 0;
    const auto append_online_apps = [this, &rows, &wrapped_content_rows, width, controls_focused, compact](const std::string& provider_id,
                                                                           bool running) {
        rows.push_back(text(" "));
        rows.push_back(text("Open the link in browser"));
        const bool native = provider_id == kTextLTProviderId;
        const auto port = native ? native_browser_port_ : localsend_runtime_.https_port.value_or(0);
        if (running && port) {
            const auto& ip = native ? native_browser_ip_ : localsend_runtime_.interface_ip;
            const std::string link = "https://" + ip + ":" + std::to_string(port);
            const auto line_width = static_cast<std::size_t>(std::max(1, width - 2));
            for (std::size_t offset = 0; offset < link.size(); offset += line_width) {
                rows.push_back(text(link.substr(offset, line_width)) | color(theme_.header) | hyperlink(link));
            }
        } else {
            const std::string status = running ? "HTTPS link unavailable" : "Start to show HTTPS link";
            const int line_width = std::max(1, width - 2);
            wrapped_content_rows += std::max(
                0, (string_width(status) + line_width - 1) / line_width - 1);
            rows.push_back(paragraph(status) | dim);
        }
        rows.push_back(text(" "));
        rows.push_back(text("Online apps") | bold);
        std::vector<std::pair<PeerKey, std::string>> online;
        if (provider_id == kLocalSendProviderId) {
            if (auto* provider = FindLocalSendProvider()) {
                for (const auto& peer : provider->OnlinePeers()) {
                    const PeerKey key{peer.identity.provider_id, peer.identity.peer_id};
                    auto& chat = Get({key.first, key.second});
                    chat.name = peer.display_name;
                    chat.online = peer.online;
                    chat.protocol = peer.protocol_version;
                    chat.supports_files = std::any_of(peer.capabilities.begin(), peer.capabilities.end(),
                        [](const Capability& capability) { return capability.type_id == kFileTypeId; });
                    online.emplace_back(key, peer.display_name);
                }
            }
        } else {
            for (const auto& [key, chat] : conversations_) {
                if (key.first == provider_id && chat.online && chat.protocol) online.emplace_back(key, chat.name);
            }
        }
        if (online.empty()) {
            if (!compact) rows.push_back(text("No apps online") | dim);
            return;
        }
        for (const auto& [key, name] : online) {
            peer_rows_.push_back(
                {key, Box{}, static_cast<int>(rows.size()) + wrapped_content_rows});
            const int row_width = std::max(1, width - 1);
            const int status_width = std::min(7, row_width);
            const int name_width = std::max(1, row_width - status_width);
            Element row = hbox({
                text(VisibleLabel(" " + name, 0, name_width)) | size(WIDTH, EQUAL, name_width),
                text(" Online") | size(WIDTH, EQUAL, status_width),
            });
            if (selected_ == key) {
                row = row | inverted;
                if (!controls_focused) row = row | focus;
            }
            rows.push_back(row | reflect(peer_rows_.back().box));
        }
    };
    for (const auto& provider : providers_) {
        if (provider != kTextLTProviderId && provider != kLocalSendProviderId) continue;
        const bool native = provider == kTextLTProviderId;
        if (!native && !compact) rows.push_back(separator() | color(theme_.gutter));
        auto heading = text(native ? "TEXTLT" : "LOCALSEND") | bold | color(theme_.modal_accent);
        auto toggle = button(native ? toggle_native_ : toggle_localsend_);
        if (width >= 20) rows.push_back(hbox({std::move(heading), filler(), std::move(toggle)}));
        else {
            rows.push_back(std::move(heading));
            rows.push_back(hbox({filler(), std::move(toggle)}));
        }
        if (native) {
            rows.push_back(hbox({button(textlt_config_), filler()}));
            append_online_apps(kTextLTProviderId, native_running_);
        } else {
            auto refresh = button(refresh_localsend_);
            auto connect_by_ip = button(connect_by_ip_);
            if (compact && width >= 31) {
                rows.push_back(hbox({std::move(refresh), std::move(connect_by_ip), filler()}));
            } else {
                rows.push_back(hbox({std::move(refresh), filler()}));
                rows.push_back(hbox({std::move(connect_by_ip), filler()}));
            }
            append_online_apps(kLocalSendProviderId, localsend_runtime_.listening);
        }
        for (const auto& [key, chat] : conversations_) {
            if (key.first != provider) continue;
            if ((provider == kTextLTProviderId || provider == kLocalSendProviderId) &&
                chat.online && chat.protocol) continue;
            peer_rows_.push_back(
                {key, Box{}, static_cast<int>(rows.size()) + wrapped_content_rows});
            std::string detail = chat.offer ? "Offer" : chat.online
                ? (chat.configured ? "Online" : "Discovered") : chat.configured ? "Configured" : "Offline";
            const int row_width = std::max(1, width - 1);
            const int status_width = std::min(static_cast<int>(detail.size()) + 1, row_width / 2);
            const int name_width = row_width - status_width;
            Element row = hbox({
                text(VisibleLabel(" " + chat.name, 0, name_width)) | size(WIDTH, EQUAL, name_width),
                text(VisibleLabel(" " + detail, 0, status_width)) | size(WIDTH, EQUAL, status_width),
            });
            if (selected_ == key) {
                row = row | inverted;
                if (!controls_focused) row = row | focus;
            }
            rows.push_back(row | reflect(peer_rows_.back().box));
        }
    }
    if (peer_rows_.empty()) rows.push_back(text("No connections") | dim);
    navigator_content_rows_ = static_cast<int>(rows.size()) + wrapped_content_rows;
    navigator_scroll_max_ = std::max(0, navigator_content_rows_ - navigator_visible_rows_);
    navigation_state_.Scroll(ConnectColumn::LocalConnections, 0, navigator_scroll_max_);
    return vbox({
        text(" Local Connections") | bold | color(theme_.modal_accent) |
            (navigation_state_.FocusedColumn() == ConnectColumn::LocalConnections ? inverted : nothing),
        separator() | color(theme_.gutter),
        vbox(std::move(rows)) | focusPosition(0, navigation_state_.ScrollOffset(
            ConnectColumn::LocalConnections)) | vscroll_indicator | yframe | flex,
    }) | size(WIDTH, EQUAL, width) | borderStyled(LIGHT, theme_.gutter) | reflect(navigator_box_);
}

Element ConnectWorkspace::RenderMessengerNavigator(int width) {
    Elements rows;
    messenger_peer_rows_.clear();
    const auto settings = telegram_provider_->Settings();
    const auto chats = telegram_provider_->AuthorizedChats();
    const bool controls_focused = toggle_telegram_->Focused() || telegram_settings_->Focused();
    if (chats.empty()) {
        const bool has_discovered = !settings.known_chats.empty();
        rows.push_back(text(has_discovered
            ? "No Telegram chats are authorized"
            : "Message the bot to discover chats") | dim);
    } else {
        messenger_peer_rows_.reserve(chats.size());
        for (const auto& chat_info : chats) {
            const PeerKey key{telegram::kTelegramProviderId, chat_info.id};
            auto& chat = conversations_[key];
            chat.name = chat_info.title.empty() ? chat_info.id : chat_info.title;
            messenger_peer_rows_.push_back({key, Box{}, static_cast<int>(rows.size())});
            const int content_width = std::max(1, width - 3);
            const auto label = chat.offer || !chat.telegram_offers.empty()
                ? "[File] " + chat.name : chat.name;
            Element row = text(" " + VisibleLabel(label, 0, content_width)) |
                size(WIDTH, EQUAL, content_width);
            if (selected_ == key) {
                row = row | inverted;
                if (!controls_focused) row = row | focus;
            }
            rows.push_back(row | reflect(messenger_peer_rows_.back().box));
        }
    }
    messenger_content_rows_ = static_cast<int>(rows.size());
    messenger_scroll_max_ = std::max(0, messenger_content_rows_ - messenger_visible_rows_);
    navigation_state_.Scroll(ConnectColumn::Messengers, 0, messenger_scroll_max_);
    return vbox({
        text(" Messengers") | bold | color(theme_.modal_accent) |
            (navigation_state_.FocusedColumn() == ConnectColumn::Messengers ? inverted : nothing),
        separator() | color(theme_.gutter),
        text(" Telegram") | bold,
        hbox({
            toggle_telegram_->Render(),
            filler(),
            telegram_settings_->Render(),
        }),
        vbox(std::move(rows)) | focusPosition(0, navigation_state_.ScrollOffset(
            ConnectColumn::Messengers)) | vscroll_indicator | yframe | flex,
    }) | size(WIDTH, EQUAL, width) | borderStyled(LIGHT, theme_.gutter) |
        reflect(messenger_box_);
}

Element ConnectWorkspace::RenderChat(int width) {
    auto* chat = Selected();
    history_box_ = {};
    message_rows_.clear();
    if (pending_share_ || preview_) {
        CloseMessageMenu();
        return RenderStructuredPicker(width);
    }

    Elements history;
    int line_count = 0;
    preview_rows_boxes_.clear();
    if (chat) {
        preview_rows_boxes_.reserve(chat->messages.size());
        message_rows_.reserve(chat->messages.size());
        for (std::size_t index = 0; index < chat->messages.size(); ++index) {
            const auto& message = chat->messages[index];
            const auto title = message.outgoing ? "You" : chat->name;
            Elements lines{text(title + (message.status.empty() ? "" : "  " + message.status)) | bold};
            AppendMessageLines(lines, message.text, width);
            if (message.item) {
                preview_rows_boxes_.emplace_back(index, Box{});
                lines.push_back(text(" Preview / Import…  " + message.import_status) | underlined |
                                reflect(preview_rows_boxes_.back().second));
            }
            line_count += static_cast<int>(lines.size());
            auto entry = vbox(std::move(lines));
            if (!message.file && !message.item && !message.sent_snapshot) {
                message_rows_.emplace_back(index, Box{});
                entry |= reflect(message_rows_.back().second);
            }
            history.push_back(std::move(entry));
        }
    }
    if (history.empty()) history.push_back(text(chat ? "No messages" : "Select a connection") | dim);
    history_rows_ = std::max(1, line_count);
    if (chat) chat->scroll = std::clamp(chat->scroll, 0, history_rows_ - 1);
    const int anchor = chat ? std::max(0, history_rows_ - 1 - chat->scroll) : 0;

    Elements body{
        text(chat ? chat->name : "Conversation") | bold,
        separator() | color(theme_.gutter),
        vbox(std::move(history)) | focusPosition(0, anchor) | vscroll_indicator | yframe | flex |
            reflect(history_box_),
    };
    if (chat && chat->offer) {
        body.push_back(hbox({
            text(chat->offer->metadata.schema_version
                ? "Incoming item: " + chat->offer->metadata.display_name.value_or(chat->offer->metadata.type_id)
                : chat->offer->metadata.type_id == kFileTypeId ? "Incoming file offer" : "Incoming text offer") | flex, accept_->Render(), text(" "), decline_->Render()
        }));
        if (CanRemember(chat->offer->identity.peer)) {
            body.push_back(text("Accepting once remembers this app and accepts its future requests automatically.") | dim);
        }
    }
    body.push_back(separator() | color(theme_.gutter));
    body.push_back(hbox({composer_->Render() | flex, text(" "), send_->Render()}));
    if (chat && chat->waiting_for_pin) {
        body.push_back(hbox({text("LocalSend PIN "), pin_input_->Render() | flex, retry_pin_->Render()}));
    }
    if (CanSendFile() || (chat && chat->active_transfer)) {
        body.push_back(hbox({CanSendFile() ? send_file_->Render() : emptyElement(),
            OffersTextFile() ? draft_file_->Render() : emptyElement(), filler(),
            chat && chat->active_transfer ? cancel_transfer_->Render() : emptyElement()}));
    }
    body.push_back(text(status_) | dim);
    return vbox(std::move(body)) | flex | borderStyled(LIGHT, theme_.gutter);
}

Element ConnectWorkspace::OnRender() {
    const auto terminal = Terminal::Size();
    const auto layout = ComputeConnectWorkspaceLayout(terminal.dimx, terminal.dimy);
    const int fixed_height = layout.mode == ConnectLayoutMode::ThreeColumns
        ? terminal.dimy - 5
        : layout.mode == ConnectLayoutMode::CompactNavigationRow
            ? layout.navigation_height - 4
            : layout.local_navigation_height - 4;
    navigator_visible_rows_ = std::max(1, fixed_height);
    const int messenger_height = layout.mode == ConnectLayoutMode::ThreeColumns
        ? terminal.dimy - 5
        : layout.mode == ConnectLayoutMode::CompactNavigationRow
            ? layout.navigation_height - 5
            : layout.messenger_navigation_height - 6;
    messenger_visible_rows_ = std::max(1, messenger_height);

    Element columns;
    if (layout.mode == ConnectLayoutMode::ThreeColumns) {
        columns = hbox({
            RenderNavigator(layout.local_width),
            RenderMessengerNavigator(layout.messenger_width),
            RenderChat(layout.conversation_width) | reflect(conversation_box_),
        }) | flex;
    } else if (layout.mode == ConnectLayoutMode::CompactNavigationRow) {
        auto navigation = hbox({
            RenderNavigator(layout.local_width),
            RenderMessengerNavigator(layout.messenger_width),
        }) | size(HEIGHT, LESS_THAN, layout.navigation_height) | yflex;
        columns = vbox({
            std::move(navigation),
            RenderChat(layout.conversation_width) | flex | reflect(conversation_box_),
        }) | flex;
    } else {
        columns = vbox({
            RenderNavigator(layout.local_width) |
                size(HEIGHT, LESS_THAN, layout.local_navigation_height) | yflex,
            RenderMessengerNavigator(layout.messenger_width) |
                size(HEIGHT, LESS_THAN, layout.messenger_navigation_height) | yflex,
            RenderChat(layout.conversation_width) | flex | reflect(conversation_box_),
        }) | flex;
    }
    return vbox({
        workspace_tabs_.Render(theme_, WorkspaceTab::Connect),
        std::move(columns) | flex,
    }) | flex | reflect(workspace_box_);
}

} // namespace textlt::connect_ui
