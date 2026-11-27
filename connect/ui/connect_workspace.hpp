#pragma once

#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <thread>

#include "context_menu_component.hpp"
#include "connect_workspace_layout.hpp"
#include "ftxui/component/component_base.hpp"
#include "ftxui/dom/elements.hpp"
#include "network_diagnostics.hpp"
#include "textlt/connect/localsend_provider.hpp"
#include "textlt/connect/service.hpp"

namespace textlt {

struct Theme;
enum class ButtonRole;
namespace telegram {
class TelegramProvider;
}
namespace connect { class IrcProvider; }

namespace connect_ui {

inline constexpr char kDefaultNativePort[] = "53327";
inline constexpr char kDefaultLocalSendPort[] = "53317";

struct Settings {
    std::string peer_ip;
    std::string peer_port = kDefaultNativePort;
    std::string peer_id;
    std::string peer_fingerprint;
    std::string listen_ip;
    std::string listen_port = kDefaultNativePort;
    bool discovery = false;
    std::string discovery_ip;
    std::string local_id;
    std::string display_name;
    std::string certificate;
    std::string private_key;
    std::string localsend_peer_endpoint;
    std::string localsend_alias = "TextLT";
    std::string localsend_interface;
    std::string localsend_group = "224.0.0.167";
    std::string localsend_udp_port = kDefaultLocalSendPort;
    std::string localsend_https_port = kDefaultLocalSendPort;
};

struct NativeConnectionProfile {
    std::string name;
    std::string peer_id;
    std::string address;
    std::uint16_t port = 0;
    std::string fingerprint;
};

class ConnectWorkspace : public ftxui::ComponentBase {
public:
    ConnectWorkspace(const Theme& theme, std::function<void()> redraw,
                     std::function<void()> pick_file, std::function<std::filesystem::path()> downloads,
                     std::filesystem::path localsend_identity, std::function<void()> open_textlt_config,
                     std::function<void()> open_telegram_settings,
                     std::function<void()> open_irc_settings,
                     std::function<bool(const std::string&)> write_clipboard);
    ~ConnectWorkspace() override;

    ftxui::Element OnRender() override;
    bool OnEvent(ftxui::Event event) override;
    ftxui::Component ActiveChild() override;
    void SetActiveChild(ftxui::ComponentBase* child) override;
    bool Focusable() const override { return true; }

    Settings& Configuration() { return settings_; }
    bool SaveSettings();
    NativeConnectionProfile NativeIdentity() const;
    connect::ProviderResult SaveNativeDeviceName(std::string_view name);
    connect::ProviderResult SaveNativePeerId(std::string_view peer_id);
    connect::ProviderResult SaveNativeIdentityPaths(std::string certificate, std::string private_key);
    connect::ProviderResult RegenerateNativeIdentity(std::string_view name);
    connect::ProviderResult ExportNativeIdentity(std::string_view address, std::string_view port,
                                                  std::string& json) const;
    const std::vector<NativeConnectionProfile>& NativeConnections() const { return native_connections_; }
    connect::ProviderResult ImportNativeConnection(std::string_view json, std::size_t& index);
    connect::ProviderResult ActivateNativeConnection(std::size_t index);
    void RefreshLocalIPv4();
    void RefreshDiagnostics(bool test_localsend_ports = false);
    connect::ProviderResult TestLocalSendDiscovery();
    NetworkDiagnostics Diagnostics() const;
    const connect::LocalSendRuntime& LocalSendRuntime() const { return localsend_runtime_; }
    connect::LocalSendDiscoveryStatus LocalSendDiscoveryStatus() const;
    bool LocalSendPeerVerified() const { return localsend_peer_verified_; }
    bool LocalSendPhoneReachedListener() const { return localsend_phone_reached_listener_; }
    std::string ActiveNativeListenPort() const;
    std::string ActiveNativeListenAddress() const;
    connect::ProviderResult StartNativeListener();
    bool UseLocalSendAddress(const std::string& address);
    const std::string& LocalIPv4() const { return local_ipv4_; }
    const std::string& Status() const { return status_; }
    bool Running() const;
    telegram::TelegramProvider& Telegram();
    connect::IrcProvider& Irc();
    void Stop();
    void DrainEvents();
    void UpdateBrowserTheme();
    bool Paste(ftxui::Event event);
    bool HandleMessageMenuEvent(ftxui::Event event);
    bool HandleSendEnter(ftxui::Event event);
    void CloseMessageMenu();
    ftxui::Element RenderMessageMenu();
    void RegisterContentHandler(std::unique_ptr<connect::ContentHandler> handler);
    void Share(connect::TransferMetadata metadata, connect::StructuredPayload payload);
    bool SendFile(const std::filesystem::path& path, std::string& error);
    bool CanSendCurrentFile() { return CanSendFile(); }

private:
    using PeerKey = std::pair<std::string, std::string>;

    struct Message {
        std::string id;
        std::string text;
        std::string status;
        bool outgoing;
        bool file = false;
        std::optional<connect::ReceivedContent> item = std::nullopt;
        std::string import_status;
        bool imported = false;
        bool sent_snapshot = false;
    };

    struct Conversation {
        std::string name;
        std::string draft;
        std::vector<Message> messages;
        std::optional<connect::IncomingOffer> offer;
        std::deque<connect::IncomingOffer> telegram_offers;
        std::optional<std::uint32_t> protocol;
        bool online = false;
        bool configured = false;
        bool supports_files = false;
        std::vector<connect::Capability> authenticated_capabilities;
        std::optional<std::pair<connect::TransferIdentity, connect::TransferDirection>> active_transfer;
        bool waiting_for_pin = false;
        std::string pin;
        int scroll = 0;
    };

    struct IrcLeaveControl {
        PeerKey key;
        ftxui::Component button;
        ftxui::Box box;
    };

    struct PeerRow {
        PeerKey key;
        ftxui::Box box;
        int content_row = 0;
    };

    Conversation& Get(const connect::PeerIdentity& peer);
    Conversation* Selected();
    void Select(const PeerKey& key);
    connect::BrowserTheme BrowserTheme() const;
    void LoadSettings();
    void LoadNativeConnections();
    ftxui::Component FlatAction(std::string label, ButtonRole role, std::function<void()> action,
                                std::function<bool()> enabled = {}) const;
    void StartNative();
    void ToggleNative();
    connect::ProviderResult StartLocalSend(bool test_ports = false);
    void ToggleLocalSend();
    void ToggleTelegram();
    void ToggleIrc();
    void UseIrcTarget(bool channel);
    std::string SelectedIrcServer() const;
    std::string IrcControlServer() const;
    std::string SelectedIrcTarget() const;
    std::string ServerResponses() const;
    void CopyServerResponses();
    bool IsIrcServerSelected() const;
    bool CanJoinIrcChannel() const;
    bool CanSendIrcMessage() const;
    void RefreshLocalSendPeers();
    void ConnectLocalSendByIp();
    void StopProvider(std::string_view provider);
    ftxui::Component ProviderToggle(bool& running, std::function<void()> toggle) const;
    connect::ILocalSendProvider* FindLocalSendProvider() const;
    void Send();
    bool CanSendFile();
    std::string PeerRoute() const;
    void CancelTransfer();
    bool OffersTextFile();
    void SendDraftAsFile();
    void RetryPin();
    bool QueueFile(std::string name, std::uint64_t size, std::unique_ptr<std::istream> input, std::string& error);
    void QueueProviderEvent(connect::ProviderEvent event);
    void Decide(bool accept);
    connect::ProviderResult ResolveOffer(Conversation& chat, bool accept, bool remember_app);
    void Handle(const connect::Peer& peer);
    void Handle(const connect::PeerCapabilities& peer);
    void InitializeStructuredControls();
    bool StructuredEvent(ftxui::Event event);
    ftxui::Element RenderStructuredPicker(int width);
    void OpenPreview(std::size_t index);
    void ConfirmStructuredChoice();
    void CloseStructuredPicker();
    Message* PreviewMessage();
    bool Supports(const Conversation& chat, const connect::TransferMetadata& metadata) const;
    void Handle(const connect::IncomingOffer& offer);
    void Handle(const connect::ReceivedContent& content);
    void Handle(const connect::TransferEvent& transfer);
    void Handle(const connect::IncomingConnectionFailure& failure);
    void Handle(const connect::LocalSendConnectResult& result);
    void ExecuteMessageCommand(const std::string& command);
    bool NavigatorEvent(ftxui::Event event);
    bool MessengerNavigatorEvent(ftxui::Event event);
    void FocusColumn(ftxui::Component component, ConnectColumn column);
    void UpdateNavigatorScrollLimits();
    void UpdateMessengerScrollLimits();
    void EnsureSelectedPeerVisible();
    void EnsureSelectedMessengerPeerVisible();
    ftxui::Element RenderNavigator(int width);
    ftxui::Element RenderMessengerNavigator(int width);
    ftxui::Element RenderChat(int width);

    const Theme& theme_;
    std::function<void()> redraw_;
    std::function<void()> pick_file_;
    std::function<std::filesystem::path()> downloads_;
    std::function<bool(const std::string&)> write_clipboard_;
    std::function<void()> open_telegram_settings_;
    ContextMenuComponent message_menu_;
    std::optional<std::size_t> message_menu_index_;
    const std::filesystem::path localsend_identity_;
    std::string local_ipv4_;
    Settings settings_;
    Settings active_settings_;
    std::vector<NativeConnectionProfile> native_connections_;
    mutable std::mutex diagnostics_mutex_;
    NetworkDiagnostics diagnostics_;
    std::atomic<bool> diagnostics_cancelled_{false};
    std::thread diagnostics_worker_;
    connect::LocalSendRuntime localsend_runtime_;
    std::atomic<bool> localsend_peer_verified_{false};
    std::atomic<bool> localsend_phone_reached_listener_{false};
    std::unique_ptr<connect::ConnectService> service_;
    connect::ConnectService content_;
    telegram::TelegramProvider* telegram_provider_ = nullptr;
    connect::IrcProvider* irc_provider_ = nullptr;
    std::vector<std::string> providers_;
    bool native_running_ = false;
    std::string native_browser_ip_;
    std::uint16_t native_browser_port_ = 0;
    mutable std::mutex browser_theme_mutex_;
    connect::BrowserTheme browser_theme_;
    std::string status_ = "Network stopped";
    std::mutex event_mutex_;
    std::deque<connect::ProviderEvent> events_;
    std::map<PeerKey, Conversation> conversations_;
    std::set<PeerKey> auto_accept_peers_;
    std::optional<PeerKey> selected_;
    std::string draft_;
    int cursor_ = 0;
    std::string pin_;
    std::uint64_t next_message_ = 0;
    std::vector<PeerRow> peer_rows_;
    std::vector<PeerRow> messenger_peer_rows_;
    ftxui::Box workspace_box_;
    ftxui::Box navigator_box_;
    ftxui::Box messenger_box_;
    ftxui::Box conversation_box_;
    ftxui::Box history_box_;
    std::vector<std::pair<std::size_t, ftxui::Box>> message_rows_;
    int history_rows_ = 0;
    int navigator_content_rows_ = 0;
    int navigator_visible_rows_ = 1;
    int navigator_scroll_max_ = 0;
    int messenger_content_rows_ = 0;
    int messenger_visible_rows_ = 1;
    int messenger_scroll_max_ = 0;
    int irc_controls_row_ = 0;
    int irc_toggle_row_ = 0;
    ftxui::Component navigator_, messenger_navigator_, composer_, send_, accept_, decline_, send_file_, cancel_transfer_, draft_file_, pin_input_, retry_pin_;
    ftxui::Component toggle_native_, textlt_config_, toggle_localsend_, refresh_localsend_, connect_by_ip_;
    ftxui::Component toggle_telegram_, telegram_settings_;
    ftxui::Component toggle_irc_, irc_settings_, irc_target_input_, irc_join_, irc_private_;
    ftxui::Component copy_server_responses_;
    std::vector<IrcLeaveControl> irc_leave_controls_;
    std::string irc_target_;
    ftxui::Component active_;
    ConnectNavigationState navigation_state_;

    std::optional<std::pair<connect::TransferMetadata, connect::StructuredPayload>> pending_share_;
    std::optional<std::pair<PeerKey, std::size_t>> preview_;
    std::vector<PeerKey> share_peers_;
    std::vector<connect::ImportDestination> destinations_;
    std::vector<std::string> choice_labels_;
    int choice_index_ = 0;
    int preview_scroll_ = 0;
    int preview_rows_ = 0;
    std::string preview_text_;
    std::vector<std::pair<std::size_t, ftxui::Box>> preview_rows_boxes_;
    ftxui::Box preview_content_box_;
    ftxui::Component structured_picker_, structured_menu_, structured_send_, structured_import_, structured_close_;

};

} // namespace connect_ui
} // namespace textlt
