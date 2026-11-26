#include "telegram_provider.hpp"

#include <algorithm>
#include <iterator>
#include <set>
#include <utility>

#include "download_destination.hpp"
#include "nlohmann/json.hpp"
#include "textlt/connect/types.hpp"

namespace textlt::telegram {
using namespace connect;

namespace {

using Json = nlohmann::json;

constexpr int kLongPollSeconds = 10;
constexpr int kMaxKnownChats = 500;

struct ApiResult {
    bool success = false;
    int error_code = 0;
    int retry_after = 0;
    Json result;
    std::string error;
};

ApiResult ParseResponse(const TelegramApiResponse& response) {
    ApiResult parsed;
    parsed.error_code = static_cast<int>(response.http_status);
    if (!response.error.empty()) {
        parsed.error = response.error;
        return parsed;
    }

    const Json root = Json::parse(response.body, nullptr, false);
    if (!root.is_object()) {
        parsed.error = response.http_status == 0
            ? "No response from Telegram." : "Telegram returned an invalid response.";
        return parsed;
    }

    const auto ok = root.find("ok");
    if (ok != root.end() && ok->is_boolean() && *ok) {
        parsed.success = response.http_status >= 200 && response.http_status < 300;
        parsed.result = root.value("result", Json{});
        if (!parsed.success) parsed.error = "Telegram returned an unexpected HTTP status.";
        return parsed;
    }

    const auto code = root.find("error_code");
    if (code != root.end() && code->is_number_integer()) {
        try {
            parsed.error_code = code->get<int>();
        } catch (const Json::exception&) {
            parsed.error_code = static_cast<int>(response.http_status);
        }
    }
    const auto description = root.find("description");
    if (description != root.end() && description->is_string()) {
        parsed.error = description->get<std::string>();
    } else {
        parsed.error = "Telegram API request failed.";
    }
    const auto parameters = root.find("parameters");
    if (parameters != root.end() && parameters->is_object()) {
        const auto retry = parameters->find("retry_after");
        if (retry != parameters->end() && retry->is_number_integer()) {
            try {
                parsed.retry_after = std::clamp(retry->get<int>(), 0, 300);
            } catch (const Json::exception&) {
                parsed.retry_after = 300;
            }
        }
    }
    return parsed;
}

std::string JsonId(const Json& value) {
    if (value.is_string()) return value.get<std::string>();
    if (value.is_number_integer()) return value.dump();
    return {};
}

std::string StringField(const Json& value, const char* key) {
    const auto field = value.find(key);
    return field != value.end() && field->is_string()
        ? field->get<std::string>() : std::string{};
}

std::optional<std::uint64_t> FileSize(const Json& file) {
    const auto size = file.find("file_size");
    if (size == file.end()) return std::nullopt;
    if (size->is_number_unsigned()) return size->get<std::uint64_t>();
    if (size->is_number_integer() && size->get<std::int64_t>() >= 0) {
        return size->get<std::uint64_t>();
    }
    return std::nullopt;
}

struct IncomingAttachment {
    std::string file_id;
    std::string filename;
    std::optional<std::uint64_t> size;
};

std::optional<IncomingAttachment> MessageAttachment(const Json& message) {
    const auto photos = message.find("photo");
    if (photos != message.end() && photos->is_array()) {
        std::optional<IncomingAttachment> largest;
        double largest_area = -1;
        for (const auto& photo : *photos) {
            auto file_id = StringField(photo, "file_id");
            if (file_id.empty()) continue;
            const auto width = photo.find("width");
            const auto height = photo.find("height");
            const double area = width != photo.end() && width->is_number() &&
                height != photo.end() && height->is_number()
                ? width->get<double>() * height->get<double>() : 0;
            if (!largest || area > largest_area) {
                largest = IncomingAttachment{std::move(file_id), "photo.jpg", FileSize(photo)};
                largest_area = area;
            }
        }
        if (largest) return largest;
    }

    constexpr std::pair<const char*, const char*> types[] = {
        {"document", "download"}, {"audio", "audio"}, {"video", "video"},
        {"voice", "voice.ogg"}, {"video_note", "video-note.mp4"}, {"animation", "animation"},
    };
    for (const auto& [field, fallback] : types) {
        const auto file = message.find(field);
        if (file == message.end() || !file->is_object()) continue;
        auto file_id = StringField(*file, "file_id");
        if (file_id.empty()) continue;
        auto name = StringField(*file, "file_name");
        if (name.empty()) name = fallback;
        return IncomingAttachment{std::move(file_id), std::move(name), FileSize(*file)};
    }
    return std::nullopt;
}

bool SameTransfer(const TransferIdentity& a, const TransferIdentity& b) {
    return a.peer.provider_id == b.peer.provider_id && a.peer.peer_id == b.peer.peer_id &&
        a.transfer_id == b.transfer_id;
}

std::string ChatTitle(const Json& chat) {
    const std::string title = StringField(chat, "title");
    if (!title.empty()) return title;

    std::string name = StringField(chat, "first_name");
    const std::string last = StringField(chat, "last_name");
    if (!last.empty()) {
        if (!name.empty()) name += " ";
        name += last;
    }
    if (!name.empty()) return name;

    const std::string username = StringField(chat, "username");
    return username.empty() ? "Telegram chat" : "@" + username;
}

bool ContainsId(const std::vector<std::string>& ids, const std::string& id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

int BackoffSeconds(int attempt, std::chrono::milliseconds base) {
    const auto base_seconds = std::max<std::int64_t>(
        1, std::chrono::duration_cast<std::chrono::seconds>(base).count());
    const int exponent = std::clamp(attempt, 0, 5);
    return static_cast<int>(std::min<std::int64_t>(30, base_seconds << exponent));
}

} // namespace

TelegramProvider::TelegramProvider(
    std::shared_ptr<TelegramApi> api,
    std::filesystem::path settings_path,
    StateChanged state_changed,
    std::chrono::milliseconds retry_base)
    : api_(std::move(api)),
      store_(std::move(settings_path)),
      state_changed_(std::move(state_changed)),
      retry_base_(std::max(std::chrono::milliseconds(1), retry_base)),
      settings_(store_.Load()) {
    if (!settings_.bot_token.empty()) status_ = "Stopped";
}

TelegramProvider::~TelegramProvider() {
    Stop();
}

std::vector<Capability> TelegramProvider::Capabilities() const {
    return {{kTextTypeId, {}}, {kFileTypeId, {}}};
}

TransferDirection TelegramProvider::FileJob::Direction() const {
    return std::holds_alternative<Upload>(data) ? TransferDirection::Outgoing
                                                : TransferDirection::Incoming;
}

TelegramSettings TelegramProvider::Settings() const {
    std::lock_guard lock(settings_mutex_);
    return settings_;
}

std::vector<TelegramChat> TelegramProvider::KnownChats() const {
    std::lock_guard lock(settings_mutex_);
    return settings_.known_chats;
}

std::vector<TelegramChat> TelegramProvider::AuthorizedChats() const {
    std::lock_guard lock(settings_mutex_);
    std::vector<TelegramChat> result;
    for (const auto& chat : settings_.known_chats) {
        if (ContainsId(settings_.authorized_chat_ids, chat.id)) result.push_back(chat);
    }
    return result;
}

bool TelegramProvider::IsAuthorized(const std::string& chat_id) const {
    std::lock_guard lock(settings_mutex_);
    return ContainsId(settings_.authorized_chat_ids, chat_id);
}

std::string TelegramProvider::Status() const {
    std::lock_guard lock(settings_mutex_);
    return status_;
}

bool TelegramProvider::SaveSettings(TelegramSettings settings, std::string& error) {
    error.clear();
    TelegramSettings saved;
    {
        std::lock_guard lock(settings_mutex_);
        // Polling owns update offsets and discovers chats while the settings modal
        // is open. Preserve those fields instead of overwriting a newer snapshot.
        const bool token_changed = settings.bot_token != settings_.bot_token;
        if (token_changed) {
            settings.last_update_id = -1;
            settings.known_chats.clear();
            settings.authorized_chat_ids.clear();
        } else {
            settings.last_update_id = settings_.last_update_id;
            settings.known_chats = settings_.known_chats;
        }
        std::set<std::string> unique;
        std::vector<std::string> authorized;
        authorized.reserve(settings.authorized_chat_ids.size());
        for (const auto& id : settings.authorized_chat_ids) {
            if (!id.empty() && unique.insert(id).second) authorized.push_back(id);
        }
        settings.authorized_chat_ids = std::move(authorized);
        if (!store_.Save(settings, error)) return false;
        settings_ = std::move(settings);
        saved = settings_;
    }

    if (running_) {
        poll_cancel_ = true;
        wait_condition_.notify_all();
        for (const auto& chat : saved.known_chats) {
            if (ContainsId(saved.authorized_chat_ids, chat.id)) EmitPeer(chat, true);
        }
    }
    if (state_changed_) state_changed_();
    return true;
}

TelegramConnectionTest TelegramProvider::TestConnection(const std::string& token) const {
    if (token.empty()) return {false, {}, "Enter a Telegram bot token."};
    if (!api_) return {false, {}, "Telegram HTTPS client is unavailable."};
    const auto response = api_->GetMe(token);
    const auto parsed = ParseResponse(response);
    if (!parsed.success || !parsed.result.is_object()) {
        const std::string error = parsed.error.empty()
            ? "Telegram returned invalid bot information." : parsed.error;
        return {false, {}, Redact(error, token)};
    }

    TelegramBotIdentity bot;
    bot.first_name = StringField(parsed.result, "first_name");
    bot.username = StringField(parsed.result, "username");
    return {true, std::move(bot), {}};
}

ProviderResult TelegramProvider::Start(ProviderEventSink sink) {
    if (!sink) return {false, "Telegram event sink is not available."};
    if (!api_) return {false, "Telegram HTTPS client is unavailable."};
    const auto settings = Settings();
    if (settings.bot_token.empty()) {
        SetStatus("Configure a Telegram bot token first.");
        return {false, "Configure a Telegram bot token first."};
    }

    {
        std::lock_guard lifecycle_lock(lifecycle_mutex_);
        if (running_) return {false, "Telegram is already running."};
        if (worker_.joinable()) worker_.join();
        {
            std::lock_guard lock(settings_mutex_);
            sink_ = std::move(sink);
            status_ = "Starting Telegram polling";
        }
        stop_ = false;
        poll_cancel_ = false;
        running_ = true;
        worker_ = std::thread([this] { PollLoop(); });
    }
    if (state_changed_) state_changed_();
    return {true, {}};
}

void TelegramProvider::Stop() noexcept {
    std::thread worker;
    {
        std::lock_guard lifecycle_lock(lifecycle_mutex_);
        stop_ = true;
        poll_cancel_ = true;
        {
            std::lock_guard lock(outgoing_mutex_);
            file_cancel_ = true;
        }
        wait_condition_.notify_all();
        if (worker_.joinable()) worker = std::move(worker_);
    }
    try {
        if (worker.joinable()) worker.join();
    } catch (...) {
    }
    running_ = false;
    {
        std::lock_guard lock(settings_mutex_);
        sink_ = {};
        status_ = "Stopped";
    }
    if (state_changed_) state_changed_();
}

ProviderResult TelegramProvider::Send(OutgoingTransfer transfer) {
    if (transfer.identity.peer.provider_id != kTelegramProviderId) {
        return {false, "Telegram cannot send to this provider."};
    }
    const auto settings = Settings();
    if (!ContainsId(settings.authorized_chat_ids, transfer.identity.peer.peer_id)) {
        return {false, "This Telegram chat is not authorized."};
    }
    if (transfer.metadata.schema_version) {
        return {false, "Telegram supports ordinary text and files."};
    }
    {
        std::lock_guard lock(outgoing_mutex_);
        if (!running_ || stop_) return {false, "Start Telegram before sending."};
        if (transfer.metadata.type_id == kFileTypeId) {
            auto* input = std::get_if<std::unique_ptr<std::istream>>(&transfer.payload);
            if (!input || !*input || !transfer.metadata.display_name || !transfer.metadata.byte_length) {
                return {false, "Telegram requires a filename, size and readable file."};
            }
            const auto size = *transfer.metadata.byte_length;
            if (size > kTelegramUploadLimit) return {false, "Telegram files over 50 MB cannot be sent."};
            files_.push_back({std::move(transfer.identity), settings.bot_token, Upload{
                connect_ui::SafeDownloadBasename(std::move(*transfer.metadata.display_name)),
                size, std::move(*input)}});
        } else if (transfer.metadata.type_id == kTextTypeId) {
            auto* payload = std::get_if<TextPayload>(&transfer.payload);
            if (!payload || payload->utf8.empty()) return {false, "Telegram messages cannot be empty."};
            const auto chat_id = transfer.identity.peer.peer_id;
            outgoing_.push_back({std::move(transfer.identity), chat_id, std::move(payload->utf8)});
        } else {
            return {false, "Telegram supports ordinary text and files."};
        }
    }
    poll_cancel_ = true;
    wait_condition_.notify_all();
    return {true, {}};
}

ProviderResult TelegramProvider::Accept(
    const TransferIdentity&,
    std::unique_ptr<std::ostream>) {
    return {false, "Choose a download destination for the Telegram file."};
}

ProviderResult TelegramProvider::AcceptFile(
    const TransferIdentity& offer, FileDestinationFactory destination) {
    if (!destination) return {false, "Choose a download destination for the Telegram file."};
    {
        std::lock_guard lock(outgoing_mutex_);
        if (!running_ || stop_) return {false, "Start Telegram before downloading."};
        const auto pending = std::find_if(documents_.begin(), documents_.end(),
            [&](const PendingDocument& document) { return SameTransfer(document.offer.identity, offer); });
        if (pending == documents_.end()) return {false, "This Telegram file offer is no longer available."};
        files_.push_back({std::move(pending->offer.identity), std::move(pending->token), Download{
            std::move(pending->file_id), pending->offer.metadata.byte_length, std::move(destination)}});
        documents_.erase(pending);
    }
    poll_cancel_ = true;
    wait_condition_.notify_all();
    return {true, {}};
}

ProviderResult TelegramProvider::Decline(const TransferIdentity& offer, std::string reason) {
    {
        std::lock_guard lock(outgoing_mutex_);
        const auto pending = std::find_if(documents_.begin(), documents_.end(),
            [&](const PendingDocument& document) { return SameTransfer(document.offer.identity, offer); });
        if (pending == documents_.end()) return {false, "This Telegram file offer is no longer available."};
        documents_.erase(pending);
    }
    Emit(TransferEvent{offer, TransferDirection::Incoming, TransferState::Declined,
                       std::nullopt, std::nullopt, std::move(reason)});
    return {true, {}};
}

ProviderResult TelegramProvider::Cancel(
    const TransferIdentity& transfer,
    TransferDirection direction) {
    {
        std::lock_guard lock(outgoing_mutex_);
        if (active_file_ && active_file_->second == direction &&
            SameTransfer(active_file_->first, transfer)) {
            file_cancel_ = true;
            return {true, {}};
        }
        const auto file = std::find_if(files_.begin(), files_.end(), [&](const FileJob& job) {
            return job.Direction() == direction && SameTransfer(job.identity, transfer);
        });
        if (file != files_.end()) {
            files_.erase(file);
        } else if (direction == TransferDirection::Incoming) {
            const auto pending = std::find_if(documents_.begin(), documents_.end(),
                [&](const PendingDocument& document) { return SameTransfer(document.offer.identity, transfer); });
            if (pending == documents_.end()) return {false, "This Telegram file offer is no longer available."};
            documents_.erase(pending);
        } else {
            const auto pending = std::find_if(outgoing_.begin(), outgoing_.end(),
                [&](const PendingMessage& message) { return SameTransfer(message.identity, transfer); });
            if (pending == outgoing_.end()) return {false, "This Telegram transfer is no longer queued."};
            outgoing_.erase(pending);
        }
    }
    Emit(TransferEvent{transfer, direction, TransferState::Cancelled});
    return {true, {}};
}

void TelegramProvider::PollLoop() {
    for (const auto& chat : AuthorizedChats()) EmitPeer(chat, true);
    int backoff_attempt = 0;
    while (!stop_) {
        const TelegramSettings settings = Settings();
        if (settings.bot_token.empty()) {
            SetStatus("Configure a Telegram bot token first.");
            break;
        }

        const auto response = api_->GetUpdates(
            settings.bot_token, settings.last_update_id + 1, kLongPollSeconds, &poll_cancel_);
        if (stop_) break;
        const bool interrupted = poll_cancel_.exchange(false);
        if (!response.body.empty()) {
            const auto parsed = ParseResponse(response);
            if (parsed.success) {
                backoff_attempt = 0;
                SetStatus(settings.bot_username.empty()
                    ? "Telegram polling" : "Polling as @" + settings.bot_username);
                if (!ProcessUpdates(response.body, settings.bot_token)) {
                    SetStatus("Telegram returned an invalid getUpdates response.");
                }
            } else if (settings.bot_token != Settings().bot_token) {
                continue;
            } else if (parsed.error_code == 429) {
                const auto delay = parsed.retry_after > 0
                    ? std::chrono::seconds(std::clamp(parsed.retry_after, 1, 300))
                    : std::chrono::seconds(BackoffSeconds(backoff_attempt++, retry_base_));
                if (!settings.auto_reconnect) {
                    SetStatus("Telegram rate limited; reconnect is disabled.");
                    break;
                }
                SetStatus("Telegram rate limited; retrying in " +
                    std::to_string(delay.count()) + " seconds.");
                if (WaitForRetry(delay)) break;
            } else if (parsed.error_code == 0 || parsed.error_code >= 500) {
                const std::string error = Redact(
                    parsed.error.empty() ? "Temporary Telegram server error." : parsed.error,
                    settings.bot_token);
                if (!settings.auto_reconnect) {
                    SetStatus("Telegram polling error: " + error +
                              " (automatic reconnect is disabled).");
                    break;
                }
                const auto delay = std::chrono::seconds(BackoffSeconds(backoff_attempt++, retry_base_));
                SetStatus("Telegram polling error: " + error + "; retrying in " +
                          std::to_string(delay.count()) + " seconds.");
                if (WaitForRetry(delay)) break;
            } else {
                SetStatus("Telegram polling failed: " + Redact(parsed.error, settings.bot_token));
                Emit(IncomingConnectionFailure{
                    kTelegramProviderId, TransferFailure::ConnectionFailure,
                    Redact(parsed.error, settings.bot_token)});
                break;
            }
        } else if (!interrupted) {
            if (settings.bot_token != Settings().bot_token) continue;
            const std::string error = response.error.empty()
                ? "Telegram polling connection failed." : Redact(response.error, settings.bot_token);
            if (!settings.auto_reconnect) {
                SetStatus("Telegram connection failed: " + error +
                          " (automatic reconnect is disabled).");
                Emit(IncomingConnectionFailure{
                    kTelegramProviderId, TransferFailure::ConnectionFailure, error});
                break;
            }
            const auto delay = std::chrono::seconds(BackoffSeconds(backoff_attempt++, retry_base_));
            SetStatus("Telegram connection failed: " + error + "; retrying in " +
                std::to_string(delay.count()) + " seconds.");
            Emit(IncomingConnectionFailure{
                kTelegramProviderId, TransferFailure::ConnectionFailure, error});
            if (WaitForRetry(delay)) break;
        }

        ProcessOutgoing();
        ProcessFiles();
        if (interrupted) continue;
    }

    std::deque<PendingMessage> pending;
    std::deque<PendingDocument> documents;
    std::deque<FileJob> files;
    {
        std::lock_guard lock(outgoing_mutex_);
        running_ = false;
        pending.swap(outgoing_);
        documents.swap(documents_);
        files.swap(files_);
    }
    for (const auto& job : files) {
        Emit(TransferEvent{job.identity, job.Direction(),
            stop_ ? TransferState::Cancelled : TransferState::Failed,
            std::nullopt, std::nullopt,
            stop_ ? std::nullopt : std::make_optional(Status())});
    }
    for (const auto& document : documents) {
        Emit(TransferEvent{document.offer.identity, TransferDirection::Incoming,
                           TransferState::Cancelled});
    }
    for (const auto& message : pending) {
        if (stop_) {
            Emit(TransferEvent{
                message.identity, TransferDirection::Outgoing, TransferState::Cancelled});
        } else {
            const std::string reason = Status().empty()
                ? "Telegram stopped before sending this message." : Status();
            Emit(TransferEvent{
                message.identity, TransferDirection::Outgoing, TransferState::Failed,
                std::nullopt, std::nullopt, reason, TransferFailure::ConnectionFailure});
        }
    }
    if (stop_) {
        SetStatus("Stopped");
    } else {
        const auto status = Status();
        if (status == "Starting Telegram polling" || status == "Telegram polling" ||
            status.rfind("Polling as ", 0) == 0) {
            SetStatus("Telegram polling stopped.");
        }
    }
    if (state_changed_) state_changed_();
}

bool TelegramProvider::ProcessUpdates(const std::string& body, const std::string& token) {
    const auto parsed = Json::parse(body, nullptr, false);
    if (!parsed.is_object()) return false;
    const auto result = parsed.find("result");
    if (result == parsed.end() || !result->is_array()) return false;
    for (const auto& update : *result) {
        if (stop_) return false;
        ProcessUpdate(update.dump(), token);
    }
    return true;
}

void TelegramProvider::ProcessUpdate(const std::string& update_json, const std::string& token) {
    const Json update = Json::parse(update_json, nullptr, false);
    if (!update.is_object()) return;
    const auto id = update.find("update_id");
    if (id == update.end() || !id->is_number_integer()) return;
    std::int64_t update_id = -1;
    try {
        update_id = id->get<std::int64_t>();
    } catch (const Json::exception&) {
        return;
    }

    TelegramChat discovered;
    bool has_chat = false;
    bool from_bot = false;
    std::string message_text;
    const auto message = update.find("message");
    if (message != update.end() && message->is_object()) {
        const auto chat = message->find("chat");
        if (chat != message->end() && chat->is_object()) {
            discovered.id = JsonId(chat->value("id", Json{}));
            discovered.title = ChatTitle(*chat);
            discovered.username = StringField(*chat, "username");
            discovered.type = StringField(*chat, "type");
            has_chat = !discovered.id.empty();
        }
        const auto from = message->find("from");
        if (from != message->end() && from->is_object()) {
            const auto is_bot = from->find("is_bot");
            from_bot = is_bot != from->end() && is_bot->is_boolean() && is_bot->get<bool>();
        }
        const auto text = message->find("text");
        if (text != message->end() && text->is_string()) {
            message_text = text->get<std::string>();
        }
    }

    bool changed = false;
    {
        std::lock_guard lock(settings_mutex_);
        if (token != settings_.bot_token || update_id <= settings_.last_update_id) return;
        if (has_chat) {
            auto existing = std::find_if(settings_.known_chats.begin(), settings_.known_chats.end(),
                [&](const TelegramChat& chat) { return chat.id == discovered.id; });
            if (existing == settings_.known_chats.end()) {
                settings_.known_chats.insert(settings_.known_chats.begin(), discovered);
                changed = true;
            } else if (existing->title != discovered.title ||
                       existing->username != discovered.username ||
                       existing->type != discovered.type) {
                *existing = discovered;
                changed = true;
            }
            while (settings_.known_chats.size() > kMaxKnownChats) {
                const auto removable = std::find_if(
                    settings_.known_chats.rbegin(), settings_.known_chats.rend(),
                    [&](const TelegramChat& known) {
                        return known.id != discovered.id &&
                            !ContainsId(settings_.authorized_chat_ids, known.id);
                    });
                if (removable == settings_.known_chats.rend()) break;
                settings_.known_chats.erase(std::next(removable).base());
                changed = true;
            }
        }
    }

    if (has_chat && IsFileAuthorized({{kTelegramProviderId, discovered.id}, {}}, token)) {
        EmitPeer(discovered, true);
        const TransferIdentity identity{
            {kTelegramProviderId, discovered.id},
            "telegram-update-" + std::to_string(update_id),
        };
        if (!from_bot && !message_text.empty()) {
            Emit(ReceivedContent{
                identity,
                {kTextTypeId, std::nullopt, std::nullopt,
                    static_cast<std::uint64_t>(message_text.size())},
                TextPayload{std::move(message_text)},
            });
        }
        if (!from_bot) {
            if (auto attachment = MessageAttachment(*message)) {
                const auto name = connect_ui::SafeDownloadBasename(Redact(std::move(attachment->filename), token));
                const IncomingOffer offer{identity, {kFileTypeId, std::nullopt, name, attachment->size}};
                {
                    std::lock_guard lock(outgoing_mutex_);
                    documents_.push_back({offer, std::move(attachment->file_id), token});
                }
                Emit(offer);
            }
        }
    }
    {
        std::lock_guard lock(settings_mutex_);
        if (token == settings_.bot_token && update_id > settings_.last_update_id) {
            settings_.last_update_id = update_id;
            std::string error;
            if (!store_.Save(settings_, error)) {
                status_ = "Could not save Telegram update state.";
                changed = true;
            }
        }
    }
    if (changed && state_changed_) state_changed_();
}

void TelegramProvider::ProcessOutgoing() {
    std::deque<PendingMessage> pending;
    {
        std::lock_guard lock(outgoing_mutex_);
        pending.swap(outgoing_);
    }

    for (auto& message : pending) {
        if (stop_) {
            Emit(TransferEvent{
                message.identity, TransferDirection::Outgoing, TransferState::Cancelled});
            continue;
        }
        for (int attempt = 0; !stop_; ++attempt) {
            const auto settings = Settings();
            if (!ContainsId(settings.authorized_chat_ids, message.chat_id)) {
                Emit(TransferEvent{
                    message.identity, TransferDirection::Outgoing, TransferState::Failed,
                    std::nullopt, std::nullopt,
                    std::string("This Telegram chat is no longer authorized."),
                    TransferFailure::UnsupportedContentType});
                break;
            }
            const auto response = api_->SendMessage(
                settings.bot_token, message.chat_id, message.text, &stop_);
            const auto parsed = ParseResponse(response);
            if (parsed.success) {
                Emit(TransferEvent{
                    message.identity, TransferDirection::Outgoing, TransferState::Completed});
                break;
            }
            if (stop_) {
                Emit(TransferEvent{
                    message.identity, TransferDirection::Outgoing, TransferState::Cancelled});
                break;
            }
            if (parsed.error_code == 429 && settings.auto_reconnect) {
                const int seconds = parsed.retry_after > 0
                    ? std::clamp(parsed.retry_after, 1, 300)
                    : BackoffSeconds(attempt, retry_base_);
                SetStatus("Telegram send rate limited; retrying in " +
                    std::to_string(seconds) + " seconds.");
                if (WaitForRetry(std::chrono::seconds(seconds))) {
                    Emit(TransferEvent{
                        message.identity, TransferDirection::Outgoing, TransferState::Cancelled});
                    break;
                }
                continue;
            }
            const std::string error = Redact(
                parsed.error.empty() ? "Telegram could not send the message." : parsed.error,
                settings.bot_token);
            const auto failure = parsed.error_code == 429
                ? TransferFailure::RateLimited : TransferFailure::ConnectionFailure;
            SetStatus("Telegram send failed: " + error);
            Emit(TransferEvent{
                message.identity, TransferDirection::Outgoing, TransferState::Failed,
                std::nullopt, std::nullopt, error, failure});
            break;
        }
    }
}

bool TelegramProvider::IsFileAuthorized(const TransferIdentity& identity, const std::string& token) const {
    std::lock_guard lock(settings_mutex_);
    return token == settings_.bot_token && ContainsId(settings_.authorized_chat_ids, identity.peer.peer_id);
}

void TelegramProvider::ProcessFiles() {
    while (!stop_) {
        std::optional<FileJob> job;
        {
            std::lock_guard lock(outgoing_mutex_);
            if (stop_ || files_.empty()) return;
            job.emplace(std::move(files_.front()));
            files_.pop_front();
            active_file_.emplace(job->identity, job->Direction());
            file_cancel_ = false;
        }

        const auto direction = job->Direction();
        Emit(TransferEvent{job->identity, direction, TransferState::InProgress});
        ProviderResult result;
        try {
            if (!IsFileAuthorized(job->identity, job->token)) {
                result.reason = "This Telegram chat is no longer authorized for the transfer.";
            } else if (auto* upload = std::get_if<Upload>(&job->data)) {
                result = UploadDocument(*job, *upload);
            } else {
                result = DownloadDocument(*job, std::get<Download>(job->data));
            }
        } catch (const std::exception& error) {
            result.reason = error.what();
        }

        bool cancelled;
        {
            std::lock_guard lock(outgoing_mutex_);
            cancelled = file_cancel_ || stop_;
            active_file_.reset();
        }
        const auto state = result.accepted ? TransferState::Completed
            : cancelled ? TransferState::Cancelled : TransferState::Failed;
        const auto reason = result.reason.empty() || state == TransferState::Cancelled
            ? std::nullopt : std::make_optional(Redact(std::move(result.reason), job->token));
        Emit(TransferEvent{job->identity, direction, state, std::nullopt, std::nullopt, reason,
            state == TransferState::Failed ? std::make_optional(TransferFailure::FileFailure) : std::nullopt});
    }
}

ProviderResult TelegramProvider::UploadDocument(const FileJob& job, Upload& upload) {
    std::string bytes(static_cast<std::size_t>(upload.size), '\0');
    auto& source = *upload.source;
    if (!source.read(bytes.data(), static_cast<std::streamsize>(bytes.size())) ||
        source.peek() != std::char_traits<char>::eof() || source.bad() || !source.eof()) {
        return {false, "The selected file changed size or could not be read."};
    }
    if (file_cancel_) return {false, {}};
    if (!IsFileAuthorized(job.identity, job.token)) {
        return {false, "This Telegram chat is no longer authorized for the transfer."};
    }
    const auto sent = ParseResponse(api_->SendDocument(
        job.token, job.identity.peer.peer_id, upload.filename, bytes, &file_cancel_));
    return {sent.success, sent.error};
}

ProviderResult TelegramProvider::DownloadDocument(const FileJob& job, const Download& download) {
    if (download.size && *download.size > kTelegramDownloadLimit) {
        return {false, "Telegram files over 20 MB cannot be downloaded."};
    }
    if (file_cancel_) return {false, {}};
    const auto file = ParseResponse(api_->GetFile(job.token, download.file_id, &file_cancel_));
    if (!file.success) return {false, file.error};
    const auto path = StringField(file.result, "file_path");
    if (path.empty()) return {false, "Telegram did not return a download path."};
    const auto size = FileSize(file.result);
    const auto expected = size ? size : download.size;
    if (expected && *expected > kTelegramDownloadLimit) {
        return {false, "Telegram files over 20 MB cannot be downloaded."};
    }
    if (file_cancel_) return {false, {}};
    if (!IsFileAuthorized(job.identity, job.token)) {
        return {false, "This Telegram chat is no longer authorized for the transfer."};
    }
    auto destination = download.destination();
    if (!destination) return {false, "Could not open the Downloads staging file."};

    auto next_report = std::chrono::steady_clock::now();
    const auto downloaded = api_->DownloadFile(job.token, path, &file_cancel_, [&](std::uint64_t count) {
        const auto now = std::chrono::steady_clock::now();
        if (now < next_report) return;
        next_report = now + std::chrono::milliseconds(100);
        Emit(TransferEvent{job.identity, TransferDirection::Incoming, TransferState::InProgress,
            expected ? std::min(count, *expected) : count, expected});
    });
    if (file_cancel_) return {false, {}};
    if (!downloaded.error.empty()) return {false, downloaded.error};
    if (downloaded.http_status < 200 || downloaded.http_status >= 300) {
        return {false, "Telegram could not download the file."};
    }
    if (downloaded.body.size() > kTelegramDownloadLimit ||
        (expected && downloaded.body.size() != *expected)) {
        return {false, "Telegram returned an unexpected file size."};
    }
    if (!destination->Stream().write(downloaded.body.data(),
                                      static_cast<std::streamsize>(downloaded.body.size()))) {
        return {false, "Could not write to the Downloads staging file."};
    }
    if (file_cancel_) return {false, {}};
    if (!IsFileAuthorized(job.identity, job.token)) {
        return {false, "This Telegram chat is no longer authorized for the transfer."};
    }
    return destination->Commit();
}

void TelegramProvider::Emit(ProviderEvent event) {
    connect::ProviderEventSink sink;
    {
        std::lock_guard lock(settings_mutex_);
        sink = sink_;
    }
    if (sink) sink(std::move(event));
}

void TelegramProvider::EmitPeer(const TelegramChat& chat, bool online) {
    Emit(connect::Peer{
        {kTelegramProviderId, chat.id},
        chat.title.empty() ? chat.id : chat.title,
        online,
        Capabilities(),
        std::nullopt,
    });
}

void TelegramProvider::SetStatus(std::string status) {
    bool changed = false;
    {
        std::lock_guard lock(settings_mutex_);
        if (status_ != status) {
            status_ = std::move(status);
            changed = true;
        }
    }
    if (changed && state_changed_) state_changed_();
}

bool TelegramProvider::WaitForRetry(std::chrono::seconds duration) {
    std::unique_lock lock(wait_mutex_);
    wait_condition_.wait_for(lock, duration, [this] {
        return stop_.load() || poll_cancel_.load();
    });
    if (stop_) return true;
    poll_cancel_ = false;
    return false;
}

std::string TelegramProvider::Redact(std::string value, const std::string& token) const {
    if (token.empty()) return value;
    std::size_t at = 0;
    while ((at = value.find(token, at)) != std::string::npos) {
        value.replace(at, token.size(), "[redacted]");
        at += sizeof("[redacted]") - 1;
    }
    return value;
}

} // namespace textlt::telegram
