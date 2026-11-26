#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "telegram_api.hpp"
#include "telegram_settings_store.hpp"
#include "textlt/connect/provider.hpp"

namespace textlt::telegram {

inline constexpr char kTelegramProviderId[] = "telegram";

struct TelegramBotIdentity {
    std::string first_name;
    std::string username;
};

struct TelegramConnectionTest {
    bool success = false;
    TelegramBotIdentity bot;
    std::string error;
};

class TelegramProvider final : public connect::IConnectProvider {
public:
    using StateChanged = std::function<void()>;

    explicit TelegramProvider(
        std::shared_ptr<TelegramApi> api,
        std::filesystem::path settings_path = {},
        StateChanged state_changed = {},
        std::chrono::milliseconds retry_base = std::chrono::seconds(1));
    ~TelegramProvider() override;

    std::string Id() const override { return kTelegramProviderId; }
    std::vector<connect::Capability> Capabilities() const override;
    connect::ProviderResult Start(connect::ProviderEventSink sink) override;
    void Stop() noexcept override;
    connect::ProviderResult Send(connect::OutgoingTransfer transfer) override;
    connect::ProviderResult Accept(
        const connect::TransferIdentity& offer,
        std::unique_ptr<std::ostream> file_destination) override;
    connect::ProviderResult AcceptFile(
        const connect::TransferIdentity& offer, connect::FileDestinationFactory destination) override;
    connect::ProviderResult Decline(
        const connect::TransferIdentity& offer,
        std::string reason) override;
    connect::ProviderResult Cancel(
        const connect::TransferIdentity& transfer,
        connect::TransferDirection direction) override;

    TelegramSettings Settings() const;
    bool SaveSettings(TelegramSettings settings, std::string& error);
    TelegramConnectionTest TestConnection(const std::string& token) const;
    std::vector<TelegramChat> KnownChats() const;
    std::vector<TelegramChat> AuthorizedChats() const;
    bool IsAuthorized(const std::string& chat_id) const;
    bool Running() const { return running_.load(); }
    std::string Status() const;

private:
    struct PendingMessage {
        connect::TransferIdentity identity;
        std::string chat_id;
        std::string text;
    };

    struct PendingDocument {
        connect::IncomingOffer offer;
        std::string file_id;
        std::string token;
    };

    struct Upload {
        std::string filename;
        std::uint64_t size;
        std::unique_ptr<std::istream> source;
    };

    struct Download {
        std::string file_id;
        std::optional<std::uint64_t> size;
        connect::FileDestinationFactory destination;
    };

    struct FileJob {
        connect::TransferIdentity identity;
        std::string token;
        std::variant<Upload, Download> data;
        connect::TransferDirection Direction() const;
    };

    void PollLoop();
    bool ProcessUpdates(const std::string& body, const std::string& token);
    void ProcessUpdate(const std::string& update_json, const std::string& token);
    void ProcessOutgoing();
    void ProcessFiles();
    connect::ProviderResult UploadDocument(const FileJob& job, Upload& upload);
    connect::ProviderResult DownloadDocument(const FileJob& job, const Download& download);
    bool IsFileAuthorized(const connect::TransferIdentity& identity, const std::string& token) const;
    void Emit(connect::ProviderEvent event);
    void EmitPeer(const TelegramChat& chat, bool online);
    void SetStatus(std::string status);
    bool WaitForRetry(std::chrono::seconds duration);
    std::string Redact(std::string value, const std::string& token) const;

    std::shared_ptr<TelegramApi> api_;
    TelegramSettingsStore store_;
    StateChanged state_changed_;
    std::chrono::milliseconds retry_base_;

    mutable std::mutex settings_mutex_;
    TelegramSettings settings_;
    std::string status_ = "Not configured";
    connect::ProviderEventSink sink_;

    std::mutex lifecycle_mutex_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> poll_cancel_{false};
    std::atomic<bool> running_{false};
    std::thread worker_;

    std::mutex outgoing_mutex_;
    std::deque<PendingMessage> outgoing_;
    std::deque<PendingDocument> documents_;
    std::deque<FileJob> files_;
    std::optional<std::pair<connect::TransferIdentity, connect::TransferDirection>> active_file_;
    std::atomic<bool> file_cancel_{false};
    std::mutex wait_mutex_;
    std::condition_variable wait_condition_;
};

} // namespace textlt::telegram
