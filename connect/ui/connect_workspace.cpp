#include "connect_workspace.hpp"

#include "download_destination.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <sstream>

#include "editor/clipboard_controller.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/screen/terminal.hpp"
#include "ftxui/screen/string.hpp"
#include "textlt/connect/textlt_provider.hpp"
#include "textlt/connect/localsend_provider.hpp"
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
                                   std::filesystem::path localsend_identity)
    : theme_(theme), workspace_tabs_(std::move(workspaces)), redraw_(std::move(redraw)),
      pick_file_(std::move(pick_file)), downloads_(std::move(downloads)),
      localsend_identity_(std::move(localsend_identity)) {
    LoadSettings();
    navigator_ = CatchEvent(Renderer([](bool) { return emptyElement(); }),
                            [this](Event event) { return NavigatorEvent(event); });
    toggle_native_ = ProviderToggle(native_running_, [this] { ToggleNative(); });
    toggle_localsend_ = ProviderToggle(localsend_runtime_.listening, [this] { ToggleLocalSend(); });
    auto refresh_local = ButtonOption::Simple();
    refresh_local.on_click = [this] { RefreshLocalSendPeers(); };
    refresh_local.transform = [this](const EntryState& state) {
        auto spec = ButtonSpecFromLabel(Terminal::Size().dimx < 54 ? "Refresh" : "Refresh peers",
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
    Add(toggle_native_);
    Add(toggle_localsend_);
    Add(refresh_localsend_);
    Add(connect_by_ip_);
    Add(composer_);
    Add(send_);
    Add(accept_);
    Add(decline_);
    active_ = composer_;
    InitializeStructuredControls();
    service_ = std::make_unique<ConnectService>();
    providers_ = {kTextLTProviderId, kLocalSendProviderId};
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

std::vector<std::pair<std::string, std::string>> ConnectWorkspace::DiscoveredPeers() const {
    std::vector<std::pair<std::string, std::string>> peers;
    for (const auto& [key, chat] : conversations_) {
        if (key.first == kTextLTProviderId && chat.online && chat.protocol) peers.emplace_back(key.second, chat.name);
    }
    return peers;
}

void ConnectWorkspace::StartNative() {
    const auto listen_port = Port(settings_.listen_port.empty() ? kDefaultNativePort : settings_.listen_port);
    const auto peer_port = settings_.peer_ip.empty() ? std::optional<std::uint16_t>{0} :
        Port(settings_.peer_port.empty() ? kDefaultNativePort : settings_.peer_port);
    if (!listen_port || !peer_port || (!settings_.peer_ip.empty() && !*peer_port)) {
        status_ = "Enter a port between 1 and 65535. Use 0 to disable the TextLT listener.";
        return;
    }
    if (!*listen_port && !*peer_port && !settings_.discovery) {
        status_ = "Configure a TextLT listener, peer, or LAN discovery in Connect Settings";
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

    TextLTProviderOptions options;
    options.listen_ip = settings_.listen_ip;
    options.listen_port = *listen_port;
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
    } else if (options.listen_port || options.peer_port) {
        status_ = "Pair a peer in Connect Settings → Connections before starting TextLT";
        return;
    }
    if (!settings_.peer_id.empty() && options.tls) options.trusted_discovery_peer_id = settings_.peer_id;
    if (settings_.discovery) {
        const auto& ip = settings_.discovery_ip.empty() ? settings_.listen_ip : settings_.discovery_ip;
        options.discovery = TextLTDiscoveryOptions{ip, settings_.local_id, settings_.display_name};
    }
    auto provider = MakeTextLTProvider(std::move(options));
    const auto native = provider->Start([this](ProviderEvent event) { QueueProviderEvent(std::move(event)); });
    if (!native.accepted) {
        status_ = native.reason;
        return;
    }
    service_->RegisterProvider(std::move(provider));
    active_settings_ = settings_;
    active_settings_.peer_port = *peer_port ? std::to_string(*peer_port) : "";
    native_running_ = true;
    status_ = "TextLT running";
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

    LocalSendOptions options;
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
        if (active_ == refresh_localsend_) toggle_localsend_->TakeFocus();
        status_ = "LocalSend stopped";
        return;
    }
    const auto result = StartLocalSend();
    status_ = result.accepted ? "LocalSend running" : result.reason;
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
    if (!native_running_) return {};
    const auto port = Port(active_settings_.listen_port.empty() ? kDefaultNativePort : active_settings_.listen_port);
    return port && *port ? std::to_string(*port) : std::string{};
}

std::string ConnectWorkspace::ActiveNativeListenAddress() const {
    return native_running_ ? active_settings_.listen_ip : std::string{};
}

ProviderResult ConnectWorkspace::StartNativeListener() {
    const auto port = Port(settings_.listen_port.empty() ? kDefaultNativePort : settings_.listen_port);
    if (!port || !*port) return {false, "Set a TCP listen port in Connect Settings → Network first."};
    if (!native_running_) StartNative();
    if (!ActiveNativeListenPort().empty()) return {true, {}};
    return {false, native_running_ ? "Stop TextLT in the sidebar and start it with the new listen settings." : status_};
}

void ConnectWorkspace::StopProvider(std::string_view provider) {
    if (auto* transport = service_->FindProvider(provider)) transport->Stop();
    service_->UnregisterProvider(provider);
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
    if (auto* chat = Selected()) {
        chat->draft = draft_;
        chat->pin = pin_;
    }
    const bool first = !selected_;
    selected_ = key;
    if (!first) draft_ = Selected()->draft;
    cursor_ = static_cast<int>(draft_.size());
    pin_ = Selected()->pin;
}

void ConnectWorkspace::Send() {
    if (draft_.empty()) return;
    if (!Running() || !selected_) {
        status_ = Running() ? "Select a connection to send" : "Start networking before sending";
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
    chat.messages.push_back({id, std::move(draft_), "Waiting for acceptance", true});
    if (selected_->first == kLocalSendProviderId) {
        chat.active_transfer = std::make_pair(TransferIdentity{{selected_->first, PeerRoute()}, id},
                                               TransferDirection::Outgoing);
    }
    chat.scroll = 0;
    draft_.clear();
    cursor_ = 0;
    status_ = "Waiting for acceptance";
}

bool ConnectWorkspace::CanSendFile() {
    const auto* chat = Selected();
    if (!Running() || !chat || !chat->online || !chat->supports_files) return false;
    if (selected_->first == kLocalSendProviderId) return true;
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
    chat.messages.push_back({id, name + " (" + std::to_string(size) + " bytes)",
                             "Waiting for acceptance", true, true});
    chat.active_transfer = std::make_pair(std::move(identity), TransferDirection::Outgoing);
    chat.scroll = 0;
    status_ = "Waiting for acceptance";
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
        composer_->TakeFocus();
        status_ = "Retrying LocalSend offer";
    } else {
        status_ = result.reason;
    }
}

void ConnectWorkspace::Decide(bool accept) {
    auto* chat = Selected();
    if (!Running() || !chat || !chat->offer) return;
    const auto& id = chat->offer->identity;
    auto* provider = service_->FindProvider(id.peer.provider_id);
    if (!provider) return;
    ProviderResult result;
    if (!accept) {
        result = provider->Decline(id, "Declined by recipient");
    } else if (chat->offer->metadata.type_id == kFileTypeId) {
        result = provider->AcceptFile(id, [downloads = downloads_, name = *chat->offer->metadata.display_name] {
            return MakeDownloadDestination(downloads(), name);
        });
    } else {
        result = provider->Accept(id, nullptr);
    }
    status_ = result.accepted ? (accept ? "Offer accepted" : "Offer declined") : result.reason;
    if (result.accepted) {
        chat->offer.reset();
        composer_->TakeFocus();
    }
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
    chat.offer = offer;
    const bool local_send = offer.identity.peer.provider_id == kLocalSendProviderId;
    const auto* current = Selected();
    // A LocalSend sender without a client certificate has its own conversation.
    // Show the offer there even when the sender's discovery entry is selected.
    if (!current || (local_send && !current->offer)) {
        Select({offer.identity.peer.provider_id, offer.identity.peer.peer_id});
    }
    const bool file = offer.metadata.type_id == kFileTypeId;
    if (file) {
        chat.messages.push_back({offer.identity.transfer_id,
            *offer.metadata.display_name + " (" + std::to_string(*offer.metadata.byte_length) + " bytes)",
            "Waiting for acceptance", false, true});
        chat.active_transfer = std::make_pair(offer.identity, TransferDirection::Incoming);
        chat.scroll = 0;
    }
    if (!file && local_send) {
        chat.active_transfer = std::make_pair(offer.identity, TransferDirection::Incoming);
    }
    status_ = file ? "Incoming file offer from " + chat.name
        : offer.metadata.schema_version ? "Incoming item: " + offer.metadata.display_name.value_or(offer.metadata.type_id)
                                        : "Incoming text offer from " + chat.name;
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
    if (transfer.state == TransferState::Pending && transfer.failure == TransferFailure::PinRequired) {
        chat.waiting_for_pin = true;
    }
    const auto summary = !outgoing && transfer.state == TransferState::Completed
        ? "Received" : TransferLabel(transfer.state);
    status_ = transfer.reason ? summary + ": " + *transfer.reason : summary;
    if (transfer.state == TransferState::InProgress && transfer.bytes_transferred && transfer.total_bytes) {
        status_ = std::to_string(*transfer.bytes_transferred) + " / " +
                  std::to_string(*transfer.total_bytes) + " bytes";
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
        if (component.get() == child) active_ = component;
    }
}

bool ConnectWorkspace::Paste(Event event) {
    if (!ClipboardController::IsPasteEvent(event)) return false;
    if (pending_share_ || preview_) return true;
    if (auto* chat = Selected(); chat && chat->waiting_for_pin && pin_input_->Focused()) {
        return pin_input_->OnEvent(std::move(event));
    }
    composer_->TakeFocus();
    return composer_->OnEvent(std::move(event));
}

bool ConnectWorkspace::OnEvent(Event event) {
    if (event.is_mouse()) {
        const auto mouse = event.mouse();
        if (Pressed(event) && workspace_tabs_.ActivateAt(mouse.x, mouse.y)) return true;
        if (navigator_box_.Contain(mouse.x, mouse.y)) return NavigatorEvent(event);
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
                composer_->TakeFocus();
                return true;
            }
        }
        if (auto* chat = Selected(); chat && chat->offer &&
            (accept_->OnEvent(event) || decline_->OnEvent(event))) return true;
        if ((Selected() && Selected()->waiting_for_pin &&
             (pin_input_->OnEvent(event) || retry_pin_->OnEvent(event))) ||
            (OffersTextFile() && draft_file_->OnEvent(event)) ||
            (CanSendFile() && send_file_->OnEvent(event)) ||
            (Selected() && Selected()->active_transfer && cancel_transfer_->OnEvent(event)) ||
            composer_->OnEvent(event) || send_->OnEvent(event)) return true;
        if (Pressed(event) && workspace_box_.Contain(mouse.x, mouse.y)) {
            composer_->TakeFocus();
            return true;
        }
        return false;
    }
    if (!Focused()) return false;
    if (event == Event::Tab || event == Event::TabReverse) {
        Components choices{navigator_, toggle_native_, toggle_localsend_};
        if (localsend_runtime_.listening) choices.push_back(refresh_localsend_);
        choices.push_back(connect_by_ip_);
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
        choices[(index + total + (event == Event::Tab ? 1 : -1)) % total]->TakeFocus();
        return true;
    }
    if (ScrollDelta(event) && active_ != navigator_) {
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
        if (toggle_native_->OnEvent(event) || toggle_localsend_->OnEvent(event) ||
            (localsend_runtime_.listening && refresh_localsend_->OnEvent(event)) ||
            connect_by_ip_->OnEvent(event)) return true;
        if (Pressed(event)) {
            const auto mouse = event.mouse();
            for (const auto& [key, box] : peer_rows_) {
                if (box.Contain(mouse.x, mouse.y)) {
                    CloseStructuredPicker();
                    Select(key);
                    navigator_->TakeFocus();
                    break;
                }
            }
            return true;
        }
        if (!ScrollDelta(event)) return false;
    }
    const int delta = ScrollDelta(event) ? ScrollDelta(event) * 3 :
                      event == Event::ArrowUp ? -1 : event == Event::ArrowDown ? 1 : 0;
    if (delta && !peer_rows_.empty()) {
        const auto row = std::find_if(peer_rows_.begin(), peer_rows_.end(),
                                      [this](const auto& entry) { return selected_ == entry.first; });
        const int index = row == peer_rows_.end() ? 0 : static_cast<int>(row - peer_rows_.begin());
        CloseStructuredPicker();
        Select(peer_rows_[std::clamp(index + delta, 0, static_cast<int>(peer_rows_.size()) - 1)].first);
        navigator_->TakeFocus();
        return true;
    }
    if (event == Event::Return && selected_) {
        composer_->TakeFocus();
        return true;
    }
    return false;
}

Element ConnectWorkspace::RenderNavigator(int width, bool show_workspace_tabs) {
    peer_rows_.clear();
    peer_rows_.reserve(conversations_.size());
    const auto button = [](const Component& component) {
        auto rendered = component->Render();
        return component->Focused() ? rendered | focus : rendered;
    };
    const bool controls_focused = toggle_native_->Focused() || toggle_localsend_->Focused() ||
        refresh_localsend_->Focused() || connect_by_ip_->Focused();
    Elements rows;
    for (const auto& provider : providers_) {
        rows.push_back(text(provider == kTextLTProviderId ? "TextLT" : provider == kLocalSendProviderId ? "LocalSend" : provider) | bold);
        if (provider == kTextLTProviderId) {
            rows.push_back(hbox({button(toggle_native_), filler()}));
        } else if (provider == kLocalSendProviderId) {
            auto toggle = button(toggle_localsend_);
            auto refresh = button(refresh_localsend_);
            if (width >= 29) {
                rows.push_back(hbox({std::move(toggle), text(" "), std::move(refresh), filler()}));
            } else {
                rows.push_back(hbox({std::move(toggle), filler()}));
                rows.push_back(hbox({std::move(refresh), filler()}));
            }
            rows.push_back(hbox({button(connect_by_ip_), filler()}));
        }
        for (const auto& [key, chat] : conversations_) {
            if (key.first != provider) continue;
            peer_rows_.emplace_back(key, Box{});
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
            rows.push_back(row | reflect(peer_rows_.back().second));
        }
    }
    if (peer_rows_.empty()) rows.push_back(text("No connections") | dim);
    return vbox({
        show_workspace_tabs ? workspace_tabs_.Render(theme_, WorkspaceTab::Connect) : emptyElement(),
        show_workspace_tabs ? separator() | color(theme_.gutter) : emptyElement(),
        text(" Connections") | bold | (navigator_->Focused() ? inverted : nothing),
        separator() | color(theme_.gutter),
        vbox(std::move(rows)) | vscroll_indicator | yframe | flex,
    }) | size(WIDTH, EQUAL, width) | borderStyled(LIGHT, theme_.gutter) | reflect(navigator_box_);
}

Element ConnectWorkspace::RenderChat(int width) {
    auto* chat = Selected();
    history_box_ = {};
    if (pending_share_ || preview_) return RenderStructuredPicker(width);

    Elements history;
    preview_rows_boxes_.clear();
    if (chat) {
        preview_rows_boxes_.reserve(chat->messages.size());
        for (std::size_t index = 0; index < chat->messages.size(); ++index) {
            const auto& message = chat->messages[index];
            const auto title = message.outgoing ? "You" : chat->name;
            history.push_back(text(title + (message.status.empty() ? "" : "  " + message.status)) | bold);
            AppendMessageLines(history, message.text, width);
            if (message.item) {
                preview_rows_boxes_.emplace_back(index, Box{});
                history.push_back(text(" Preview / Import…  " + message.import_status) | underlined |
                                  reflect(preview_rows_boxes_.back().second));
            }
        }
    }
    if (history.empty()) history.push_back(text(chat ? "No messages" : "Select a connection") | dim);
    history_rows_ = static_cast<int>(history.size());
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
    const int columns = Terminal::Size().dimx;
    const int navigator_width = std::clamp(columns / 3, 8, 32);
    const bool tabs_fit = navigator_width >= WorkspaceTabs::kConnectRowWidth;
    auto layout = hbox({RenderNavigator(navigator_width, tabs_fit),
                        RenderChat(std::max(1, columns - navigator_width - 5))}) | flex;
    if (!tabs_fit) {
        layout = vbox({workspace_tabs_.Render(theme_, WorkspaceTab::Connect), std::move(layout)}) | flex;
    }
    return layout | reflect(workspace_box_);
}

} // namespace textlt::connect_ui
