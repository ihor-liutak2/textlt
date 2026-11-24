#pragma once

#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <thread>

#include "ftxui/component/component_base.hpp"
#include "ftxui/dom/elements.hpp"
#include "network_diagnostics.hpp"
#include "textlt/connect/localsend_provider.hpp"
#include "textlt/connect/service.hpp"
#include "workspace_tabs.hpp"

namespace textlt {

struct Theme;
enum class ButtonRole;

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

class ConnectWorkspace : public ftxui::ComponentBase {
public:
    ConnectWorkspace(const Theme& theme, std::function<void()> redraw, WorkspaceTabs::Callbacks workspaces,
                     std::function<void()> pick_file, std::function<std::filesystem::path()> downloads,
                     std::filesystem::path localsend_identity);
    ~ConnectWorkspace() override;

    ftxui::Element OnRender() override;
    bool OnEvent(ftxui::Event event) override;
    ftxui::Component ActiveChild() override;
    void SetActiveChild(ftxui::ComponentBase* child) override;
    bool Focusable() const override { return true; }

    Settings& Configuration() { return settings_; }
    bool SaveSettings();
    connect::ProviderResult PairNativePeer(std::string_view code);
    void RefreshLocalIPv4();
    void RefreshDiagnostics();
    NetworkDiagnostics Diagnostics() const;
    const connect::LocalSendRuntime& LocalSendRuntime() const { return localsend_runtime_; }
    bool LocalSendPeerVerified() const { return localsend_peer_verified_; }
    bool LocalSendPhoneReachedListener() const { return localsend_phone_reached_listener_; }
    bool UseLocalSendAddress(const std::string& address);
    const std::string& LocalIPv4() const { return local_ipv4_; }
    const std::string& Status() const { return status_; }
    bool Running() const { return native_running_ || localsend_runtime_.listening; }
    std::vector<std::pair<std::string, std::string>> DiscoveredPeers() const;
    void Stop();
    void DrainEvents();
    bool Paste(ftxui::Event event);
    void RegisterContentHandler(std::unique_ptr<connect::ContentHandler> handler);
    void Share(connect::TransferMetadata metadata, connect::StructuredPayload payload);
    bool SendFile(const std::filesystem::path& path, std::string& error);

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
    };

    struct Conversation {
        std::string name;
        std::string draft;
        std::vector<Message> messages;
        std::optional<connect::IncomingOffer> offer;
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

    Conversation& Get(const connect::PeerIdentity& peer);
    Conversation* Selected();
    void Select(const PeerKey& key);
    void LoadSettings();
    ftxui::Component FlatAction(std::string label, ButtonRole role, std::function<void()> action) const;
    void StartNative();
    void ToggleNative();
    connect::ProviderResult StartLocalSend();
    void ToggleLocalSend();
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
    bool NavigatorEvent(ftxui::Event event);
    ftxui::Element RenderNavigator(int width, bool show_workspace_tabs);
    ftxui::Element RenderChat(int width);

    const Theme& theme_;
    WorkspaceTabs workspace_tabs_;
    std::function<void()> redraw_;
    std::function<void()> pick_file_;
    std::function<std::filesystem::path()> downloads_;
    const std::filesystem::path localsend_identity_;
    std::string local_ipv4_;
    Settings settings_;
    Settings active_settings_;
    mutable std::mutex diagnostics_mutex_;
    NetworkDiagnostics diagnostics_;
    std::atomic<bool> diagnostics_cancelled_{false};
    std::thread diagnostics_worker_;
    connect::LocalSendRuntime localsend_runtime_;
    std::atomic<bool> localsend_peer_verified_{false};
    std::atomic<bool> localsend_phone_reached_listener_{false};
    std::unique_ptr<connect::ConnectService> service_;
    connect::ConnectService content_;
    std::vector<std::string> providers_;
    bool native_running_ = false;
    std::string status_ = "Network stopped";
    std::mutex event_mutex_;
    std::deque<connect::ProviderEvent> events_;
    std::map<PeerKey, Conversation> conversations_;
    std::optional<PeerKey> selected_;
    std::string draft_;
    int cursor_ = 0;
    std::string pin_;
    std::uint64_t next_message_ = 0;
    std::vector<std::pair<PeerKey, ftxui::Box>> peer_rows_;
    ftxui::Box workspace_box_;
    ftxui::Box navigator_box_;
    ftxui::Box history_box_;
    int history_rows_ = 0;
    ftxui::Component navigator_, composer_, send_, accept_, decline_, send_file_, cancel_transfer_, draft_file_, pin_input_, retry_pin_;
    ftxui::Component toggle_native_, toggle_localsend_, refresh_localsend_, connect_by_ip_;
    ftxui::Component active_;

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
