#include "telegram/telegram_provider.hpp"
#include "telegram/telegram_settings_store.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h>
#endif

#include "nlohmann/json.hpp"
#include "editor_config.hpp"
#include "download_destination.hpp"

namespace {

using namespace std::chrono_literals;
using namespace textlt;
using namespace textlt::connect;
using namespace textlt::telegram;
using Json = nlohmann::json;
const std::string kFixtureBytes("file\0data\r\n", 11);

bool Check(bool condition, const char* message) {
    if (!condition) std::cerr << message << '\n';
    return condition;
}

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        path_ = std::filesystem::temp_directory_path() /
            ("textlt-telegram-test-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(path_);
    }
    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    const std::filesystem::path& Path() const { return path_; }

private:
    std::filesystem::path path_;
};

class MockTelegramApi final : public TelegramApi {
public:
    TelegramApiResponse GetMe(
        const std::string& token, const std::atomic<bool>*) override {
        std::lock_guard lock(mutex_);
        tokens_.push_back(token);
        if (!get_me_responses_.empty()) {
            auto response = std::move(get_me_responses_.front());
            get_me_responses_.pop_front();
            return response;
        }
        return {200,
            R"({"ok":true,"result":{"id":9001,"is_bot":true,"first_name":"Fixture Bot","username":"fixture_bot"}})",
            {}};
    }

    TelegramApiResponse GetUpdates(
        const std::string& token, std::int64_t offset, int,
        const std::atomic<bool>* cancel_requested) override {
        std::unique_lock lock(mutex_);
        tokens_.push_back(token);
        offsets_.push_back(offset);
        ++update_calls_;
        ++active_updates_;
        max_active_updates_ = std::max(max_active_updates_, active_updates_);
        condition_.notify_all();
        condition_.wait_for(lock, 60ms, [&] {
            return !updates_.empty() ||
                (cancel_requested && cancel_requested->load());
        });
        if (cancel_requested && cancel_requested->load()) {
            --active_updates_;
            condition_.notify_all();
            return {0, {}, "cancelled"};
        }
        if (updates_.empty()) {
            --active_updates_;
            condition_.notify_all();
            return {200, R"({"ok":true,"result":[]})", {}};
        }
        auto response = std::move(updates_.front());
        updates_.pop_front();
        ++updates_delivered_;
        --active_updates_;
        condition_.notify_all();
        return response;
    }

    TelegramApiResponse SendMessage(
        const std::string& token, const std::string& chat_id,
        const std::string& text, const std::atomic<bool>*) override {
        std::lock_guard lock(mutex_);
        tokens_.push_back(token);
        sent_chat_ids_.push_back(chat_id);
        sent_texts_.push_back(text);
        condition_.notify_all();
        if (!send_responses_.empty()) {
            auto response = std::move(send_responses_.front());
            send_responses_.pop_front();
            return response;
        }
        return {200, R"({"ok":true,"result":{"message_id":1}})", {}};
    }

    TelegramApiResponse SendDocument(
        const std::string&, const std::string& chat_id, const std::string& filename,
        const std::string& bytes, const std::atomic<bool>*) override {
        if (chat_id != "77" || filename != "archive.zip" || bytes != kFixtureBytes) {
            return {0, {}, "Unexpected upload contents."};
        }
        return {200, R"({"ok":true,"result":{"message_id":2}})", {}};
    }

    TelegramApiResponse GetFile(
        const std::string& token, const std::string& file_id, const std::atomic<bool>*) override {
        ++get_file_calls_;
        if (file_id == "failure") return {0, {}, "Could not download with " + token};
        if (file_id == "thumbnail") return {0, {}, "Incorrect Telegram photo variant."};
        return {200, R"({"ok":true,"result":{"file_path":"documents/file_1.zip","file_size":11}})", {}};
    }

    TelegramApiResponse DownloadFile(
        const std::string&, const std::string& path, const std::atomic<bool>*,
        FileProgress progress) override {
        if (path != "documents/file_1.zip") return {0, {}, "Unexpected download path."};
        progress(5);
        return {200, kFixtureBytes, {}};
    }

    int GetFileCalls() const { return get_file_calls_; }

    void QueueUpdateResponse(TelegramApiResponse response) {
        std::lock_guard lock(mutex_);
        updates_.push_back(std::move(response));
        condition_.notify_all();
    }

    void QueueSendResponse(TelegramApiResponse response) {
        std::lock_guard lock(mutex_);
        send_responses_.push_back(std::move(response));
    }

    void QueueGetMeResponse(TelegramApiResponse response) {
        std::lock_guard lock(mutex_);
        get_me_responses_.push_back(std::move(response));
    }

    bool WaitForUpdatesDelivered(int count, std::chrono::milliseconds timeout = 3s) {
        std::unique_lock lock(mutex_);
        return condition_.wait_for(lock, timeout, [&] { return updates_delivered_ >= count; });
    }

    bool WaitForUpdateCalls(int count, std::chrono::milliseconds timeout = 3s) {
        std::unique_lock lock(mutex_);
        return condition_.wait_for(lock, timeout, [&] { return update_calls_ >= count; });
    }

    bool WaitForSendCalls(int count, std::chrono::milliseconds timeout = 3s) {
        std::unique_lock lock(mutex_);
        return condition_.wait_for(lock, timeout, [&] {
            return static_cast<int>(sent_texts_.size()) >= count;
        });
    }

    int UpdateCalls() const {
        std::lock_guard lock(mutex_);
        return update_calls_;
    }

    int MaxConcurrentUpdates() const {
        std::lock_guard lock(mutex_);
        return max_active_updates_;
    }

    int UpdatesDelivered() const {
        std::lock_guard lock(mutex_);
        return updates_delivered_;
    }

    std::vector<std::int64_t> Offsets() const {
        std::lock_guard lock(mutex_);
        return offsets_;
    }

    std::vector<std::string> SentChatIds() const {
        std::lock_guard lock(mutex_);
        return sent_chat_ids_;
    }

    std::vector<std::string> SentTexts() const {
        std::lock_guard lock(mutex_);
        return sent_texts_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<TelegramApiResponse> updates_;
    std::deque<TelegramApiResponse> send_responses_;
    std::deque<TelegramApiResponse> get_me_responses_;
    std::vector<std::string> tokens_;
    std::vector<std::int64_t> offsets_;
    std::vector<std::string> sent_chat_ids_;
    std::vector<std::string> sent_texts_;
    int update_calls_ = 0;
    int updates_delivered_ = 0;
    int active_updates_ = 0;
    int max_active_updates_ = 0;
    std::atomic<int> get_file_calls_{0};
};

class EventCollector {
public:
    void Push(ProviderEvent event) {
        {
            std::lock_guard lock(mutex_);
            events_.push_back(std::move(event));
        }
        condition_.notify_all();
    }

    bool WaitFor(const std::function<bool(const std::vector<ProviderEvent>&)>& predicate,
                 std::chrono::milliseconds timeout = 3s) {
        std::unique_lock lock(mutex_);
        return condition_.wait_for(lock, timeout, [&] { return predicate(events_); });
    }

    std::vector<ProviderEvent> Snapshot() const {
        std::lock_guard lock(mutex_);
        return events_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::vector<ProviderEvent> events_;
};

std::string Update(
    std::int64_t update_id, const std::string& chat_id, const std::string& text) {
    return Json({
        {"ok", true},
        {"result", Json::array({
            {
                {"update_id", update_id},
                {"message", {
                    {"message_id", update_id},
                    {"chat", {{"id", std::stoll(chat_id)}, {"type", "private"},
                              {"first_name", "Ada"}, {"username", "ada"}},
                    },
                    {"from", {{"is_bot", false}}},
                    {"text", text},
                }},
            },
        })},
    }).dump();
}

bool HasReceivedText(const std::vector<ProviderEvent>& events, const std::string& text) {
    return std::count_if(events.begin(), events.end(), [&](const ProviderEvent& event) {
        const auto* received = std::get_if<ReceivedContent>(&event);
        if (!received) return false;
        const auto* body = std::get_if<TextPayload>(&received->payload);
        return body && body->utf8 == text;
    }) > 0;
}

int CountReceivedText(const std::vector<ProviderEvent>& events, const std::string& text) {
    return static_cast<int>(std::count_if(events.begin(), events.end(), [&](const ProviderEvent& event) {
        const auto* received = std::get_if<ReceivedContent>(&event);
        if (!received) return false;
        const auto* body = std::get_if<TextPayload>(&received->payload);
        return body && body->utf8 == text;
    }));
}

bool HasCompletedTransfer(const std::vector<ProviderEvent>& events, const std::string& id) {
    return std::any_of(events.begin(), events.end(), [&](const ProviderEvent& event) {
        const auto* transfer = std::get_if<TransferEvent>(&event);
        return transfer && transfer->identity.transfer_id == id &&
            transfer->state == TransferState::Completed;
    });
}

bool WaitUntil(const std::function<bool()>& predicate, std::chrono::milliseconds timeout = 3s) {
    const auto end = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < end) {
        if (predicate()) return true;
        std::this_thread::sleep_for(10ms);
    }
    return predicate();
}

bool TestSettingsPersistence(const std::filesystem::path& root) {
    const char* old_home_value = std::getenv("HOME");
    const std::optional<std::string> old_home =
        old_home_value ? std::optional<std::string>(old_home_value) : std::nullopt;
    const char* old_xdg_value = std::getenv("XDG_CONFIG_HOME");
    const std::optional<std::string> old_xdg =
        old_xdg_value ? std::optional<std::string>(old_xdg_value) : std::nullopt;
    const char* old_appdata_value = std::getenv("APPDATA");
    const std::optional<std::string> old_appdata =
        old_appdata_value ? std::optional<std::string>(old_appdata_value) : std::nullopt;

    const auto set_home = [](const std::filesystem::path& path) {
#ifdef _WIN32
        _putenv_s("HOME", path.string().c_str());
#else
        setenv("HOME", path.string().c_str(), 1);
#endif
    };
    const auto set_appdata = [](const std::filesystem::path& path) {
#ifdef _WIN32
        _putenv_s("APPDATA", path.string().c_str());
#else
        (void)path;
#endif
    };
    const auto restore = [&] {
#ifdef _WIN32
        _putenv_s("HOME", old_home ? old_home->c_str() : "");
        _putenv_s("XDG_CONFIG_HOME", old_xdg ? old_xdg->c_str() : "");
        _putenv_s("APPDATA", old_appdata ? old_appdata->c_str() : "");
#else
        if (old_home) setenv("HOME", old_home->c_str(), 1);
        else unsetenv("HOME");
        if (old_xdg) setenv("XDG_CONFIG_HOME", old_xdg->c_str(), 1);
        else unsetenv("XDG_CONFIG_HOME");
#endif
    };

    const TelegramSettings expected{
        "12345:private-token", "Fixture Bot", "fixture_bot",
        {"-100123"}, {{"-100123", "Example Group", "example", "supergroup"}},
        44, false, true,
    };
    std::string error;

    const auto native_home = root / "native-home";
    set_home(native_home);
#ifdef _WIN32
    const auto appdata = native_home / "AppData" / "Roaming";
    set_appdata(appdata);
    const auto native_path = appdata / "textlt" / "telegram-settings.json";
#else
    const auto native_path = native_home / ".config" / "textlt" / "telegram-settings.json";
#endif
    TelegramSettingsStore native_store;
    if (!Check(native_store.Path() == native_path, "native Telegram config path")) {
        restore();
        return false;
    }
    if (!Check(native_store.Save(expected, error), "save native Telegram settings")) {
        restore();
        return false;
    }
    const auto native_loaded = TelegramSettingsStore{}.Load();
    if (!Check(native_loaded.bot_token == expected.bot_token &&
               native_loaded.authorized_chat_ids == expected.authorized_chat_ids &&
               native_loaded.last_update_id == expected.last_update_id &&
               native_loaded.known_chats == expected.known_chats,
               "load native Telegram settings")) {
        restore();
        return false;
    }

    const auto docker_home = root / "state";
    set_home(docker_home);
#ifdef _WIN32
    // Docker's Linux container resolves HOME=/state; exercise that directory
    // layout explicitly when this native test executable is running on Windows.
    const auto docker_path = docker_home / ".config" / "textlt" / "telegram-settings.json";
    TelegramSettingsStore docker_store(docker_path);
#else
    const auto docker_path = docker_home / ".config" / "textlt" / "telegram-settings.json";
    TelegramSettingsStore docker_store;
#endif
    if (!Check(docker_store.Path() == docker_path, "Docker Telegram config path")) {
        restore();
        return false;
    }
    if (!Check(docker_store.Save(expected, error), "save Docker Telegram settings")) {
        restore();
        return false;
    }
    const auto docker_loaded = docker_store.Load();
    restore();
    if (!Check(docker_loaded.bot_token == expected.bot_token &&
               docker_loaded.start_on_launch && !docker_loaded.auto_reconnect,
               "load Docker Telegram settings")) return false;

#ifndef _WIN32
    struct stat info {};
    if (!Check(::stat(docker_path.c_str(), &info) == 0 &&
               (info.st_mode & 0077) == 0,
               "Telegram settings file permissions")) return false;
#endif
    return true;
}

bool TestProvider(const std::filesystem::path& root) {
    constexpr char token[] = "12345:do-not-log-this-token";
    const std::string multiline = "Hello, world!\nПривіт, світе — Καλημέρα";
    auto api = std::make_shared<MockTelegramApi>();
    const auto settings_path = root / "provider" / "telegram-settings.json";
    TelegramProvider provider(api, settings_path, {}, 10ms);

    const auto test = provider.TestConnection(token);
    if (!Check(test.success && test.bot.first_name == "Fixture Bot" &&
               test.bot.username == "fixture_bot",
               "getMe test connection")) return false;
    if (!Check(api->SentTexts().empty(), "getMe must not send messages")) return false;
    api->QueueGetMeResponse({0, {}, std::string("request failed for ") + token});
    const auto rejected_test = provider.TestConnection(token);
    if (!Check(!rejected_test.success &&
               rejected_test.error.find(token) == std::string::npos,
               "redact bot token from connection diagnostics")) return false;

    TelegramSettings settings;
    settings.bot_token = token;
    settings.auto_reconnect = true;
    std::string error;
    if (!Check(provider.SaveSettings(settings, error), "save provider settings")) return false;

    EventCollector events;
    if (!Check(provider.Start([&](ProviderEvent event) { events.Push(std::move(event)); }).accepted,
               "start Telegram polling")) return false;

    api->QueueUpdateResponse({200, Update(10, "77", "not authorized"), {}});
    if (!WaitUntil([&] {
        const auto known = provider.KnownChats();
        return !known.empty() && known.front().id == "77" &&
            provider.Settings().last_update_id >= 10;
    })) {
        provider.Stop();
        return Check(false, "discover an unauthorized chat");
    }
    if (!Check(CountReceivedText(events.Snapshot(), "not authorized") == 0,
               "reject incoming messages from unauthorized chats")) {
        provider.Stop();
        return false;
    }

    settings = provider.Settings();
    settings.authorized_chat_ids.push_back("77");
    if (!Check(provider.SaveSettings(settings, error), "authorize discovered chat")) {
        provider.Stop();
        return false;
    }
    api->QueueUpdateResponse({200, Update(11, "77", multiline), {}});
    if (!events.WaitFor([&](const auto& snapshot) {
        return HasReceivedText(snapshot, multiline);
    })) {
        provider.Stop();
        return Check(false, "receive authorized UTF-8 multiline message");
    }
    if (!Check(WaitUntil([&] { return provider.Settings().last_update_id >= 11; }),
               "persist Telegram update offset")) {
        provider.Stop();
        return false;
    }

    const int deliveries_before_duplicate = api->UpdatesDelivered();
    api->QueueUpdateResponse({200, Update(11, "77", multiline), {}});
    if (!api->WaitForUpdatesDelivered(deliveries_before_duplicate + 1)) {
        provider.Stop();
        return Check(false, "deliver duplicate update fixture");
    }
    std::this_thread::sleep_for(100ms);
    if (!Check(CountReceivedText(events.Snapshot(), multiline) == 1,
               "suppress duplicate Telegram update")) {
        provider.Stop();
        return false;
    }

    OutgoingTransfer outgoing{
        {{"telegram", "77"}, "outgoing-1"},
        {kTextTypeId, std::nullopt, std::nullopt, static_cast<std::uint64_t>(multiline.size())},
        TextPayload{multiline},
    };
    if (!Check(provider.Send(std::move(outgoing)).accepted, "queue Telegram sendMessage")) {
        provider.Stop();
        return false;
    }
    if (!api->WaitForSendCalls(1) ||
        !events.WaitFor([](const auto& snapshot) {
            return HasCompletedTransfer(snapshot, "outgoing-1");
        })) {
        provider.Stop();
        return Check(false, "complete Telegram sendMessage");
    }
    if (!Check(api->SentChatIds().front() == "77" &&
               api->SentTexts().front() == multiline,
               "preserve Telegram chat ID and UTF-8 multiline payload")) {
        provider.Stop();
        return false;
    }

    OutgoingTransfer unauthorized{
        {{"telegram", "999"}, "unauthorized-send"},
        {kTextTypeId, std::nullopt, std::nullopt, 1},
        TextPayload{"x"},
    };
    if (!Check(!provider.Send(std::move(unauthorized)).accepted,
               "reject outgoing message to unauthorized chat")) {
        provider.Stop();
        return false;
    }

    api->QueueSendResponse({
        429, R"({"ok":false,"error_code":429,"description":"Too Many Requests","parameters":{"retry_after":1}})", {}});
    api->QueueSendResponse({200, R"({"ok":true,"result":{"message_id":2}})", {}});
    OutgoingTransfer rate_limited{
        {{"telegram", "77"}, "outgoing-rate-limit"},
        {kTextTypeId, std::nullopt, std::nullopt, 4},
        TextPayload{"slow"},
    };
    if (!Check(provider.Send(std::move(rate_limited)).accepted, "queue rate-limited send")) {
        provider.Stop();
        return false;
    }
    if (!api->WaitForSendCalls(3) ||
        !events.WaitFor([](const auto& snapshot) {
            return HasCompletedTransfer(snapshot, "outgoing-rate-limit");
        }, 4s)) {
        provider.Stop();
        return Check(false, "retry Telegram send after retry_after");
    }

    const int poll_calls = api->UpdateCalls();
    const int delivered = api->UpdatesDelivered();
    api->QueueUpdateResponse({
        429,
        R"({"ok":false,"error_code":429,"description":"Too Many Requests","parameters":{"retry_after":1}})",
        {},
    });
    if (!api->WaitForUpdatesDelivered(delivered + 1) ||
        !api->WaitForUpdateCalls(poll_calls + 2, 3s)) {
        provider.Stop();
        return Check(false, "reconnect Telegram polling after retry_after");
    }
    if (!Check(provider.Running(), "keep polling after a recoverable rate limit") ||
        !Check(api->MaxConcurrentUpdates() == 1, "use a single Telegram polling request")) {
        provider.Stop();
        return false;
    }
    provider.Stop();
    if (!Check(!provider.Running(), "reliable Telegram provider shutdown")) return false;
    const auto offsets = api->Offsets();
    if (!Check(std::find(offsets.begin(), offsets.end(), 11) != offsets.end(),
               "advance getUpdates offset")) return false;

    auto restarted_api = std::make_shared<MockTelegramApi>();
    TelegramProvider restarted(restarted_api, settings_path, {}, 10ms);
    if (!Check(restarted.Start([](ProviderEvent) {}).accepted,
               "restart Telegram provider from saved settings")) return false;
    if (!restarted_api->WaitForUpdateCalls(1) ||
        !Check(!restarted_api->Offsets().empty() &&
               restarted_api->Offsets().front() == 12,
               "resume Telegram polling after persisted update offset")) {
        restarted.Stop();
        return false;
    }
    restarted.Stop();

    settings = provider.Settings();
    settings.bot_token = "12345:new-bot-token";
    if (!Check(provider.SaveSettings(settings, error), "save replacement bot token")) return false;
    const auto rotated = provider.Settings();
    if (!Check(rotated.last_update_id == -1 && rotated.known_chats.empty() &&
               rotated.authorized_chat_ids.empty(),
               "reset poll offset and chat authorization when bot token changes")) return false;
    return true;
}

bool TestReconnectDisabled(const std::filesystem::path& root) {
    auto api = std::make_shared<MockTelegramApi>();
    TelegramProvider provider(api, root / "no-reconnect" / "telegram-settings.json", {}, 10ms);
    TelegramSettings settings;
    settings.bot_token = "12345:token";
    settings.auto_reconnect = false;
    std::string error;
    if (!Check(provider.SaveSettings(settings, error), "save reconnect-disabled setting")) return false;
    api->QueueUpdateResponse({
        500, R"({"ok":false,"error_code":500,"description":"fixture server error"})", {}});
    if (!Check(provider.Start([](ProviderEvent) {}).accepted, "start reconnect-disabled provider")) return false;
    const bool stopped = WaitUntil([&] { return !provider.Running(); });
    const auto status = provider.Status();
    provider.Stop();
    return Check(stopped && status.find("reconnect is disabled") != std::string::npos,
                 "stop polling after server error when reconnect is disabled");
}

bool TestFiles(const std::filesystem::path& root) {
    auto api = std::make_shared<MockTelegramApi>();
    EventCollector events;
    TelegramProvider provider(api, root / "files" / "telegram-settings.json");
    TelegramSettings settings;
    settings.bot_token = "12345:file-token";
    std::string error;
    if (!Check(provider.SaveSettings(settings, error), "save document test bot token")) return false;
    settings.authorized_chat_ids = {"77"};
    if (!Check(provider.SaveSettings(settings, error), "authorize document test chat") ||
        !Check(provider.Start([&](ProviderEvent event) { events.Push(std::move(event)); }).accepted,
               "start Telegram for document transfers")) return false;

    auto make_upload = [](const char* id, std::uint64_t size) -> OutgoingTransfer {
        return {{{"telegram", "77"}, id}, {kFileTypeId, {}, "archive.zip", size},
            std::make_unique<std::istringstream>(kFixtureBytes)};
    };
    if (!Check(!provider.Send(make_upload("too-large", kTelegramUploadLimit + 1)).accepted,
               "reject oversize Telegram upload") ||
        !Check(provider.Send(make_upload("upload", kFixtureBytes.size())).accepted,
               "queue binary Telegram document") ||
        !events.WaitFor([](const auto& snapshot) { return HasCompletedTransfer(snapshot, "upload"); })) {
        return Check(false, "send Telegram document bytes and report completion");
    }

    auto offer = [&](int update_id, const char* chat_id, const char* file_id, bool bot = false) {
        auto update = Json::parse(Update(update_id, chat_id, ""));
        auto& message = update["result"][0]["message"];
        message["from"]["is_bot"] = bot;
        message["document"] = {{"file_id", file_id}, {"file_name", "../CON.zip"}, {"file_size", 11}};
        api->QueueUpdateResponse({200, update.dump(), {}});
        return TransferIdentity{{"telegram", chat_id}, "telegram-update-" + std::to_string(update_id)};
    };
    offer(1, "88", "unauthorized");
    offer(2, "77", "bot", true);
    const auto accepted = offer(3, "77", "download");
    if (!events.WaitFor([&](const auto& snapshot) {
        return std::any_of(snapshot.begin(), snapshot.end(), [&](const ProviderEvent& event) {
            const auto* incoming = std::get_if<IncomingOffer>(&event);
            return incoming && incoming->identity.transfer_id == accepted.transfer_id &&
                incoming->metadata.display_name == "_CON.zip" && incoming->metadata.byte_length == 11;
        });
    })) return Check(false, "offer a document with a safe filename and size");

    const auto snapshot = events.Snapshot();
    if (!Check(std::count_if(snapshot.begin(), snapshot.end(), [](const ProviderEvent& event) {
            return std::holds_alternative<IncomingOffer>(event);
        }) == 1 && api->GetFileCalls() == 0, "filter documents and wait for acceptance")) return false;

    const auto downloads = root / "saved";
    const auto destination = [&] {
        return connect_ui::MakeDownloadDestination(downloads, "_CON.zip");
    };
    if (!Check(provider.AcceptFile(accepted, destination).accepted, "accept Telegram file") ||
        !events.WaitFor([&](const auto& seen) {
            return HasCompletedTransfer(seen, accepted.transfer_id);
        })) return Check(false, "download and commit Telegram document");

    std::ifstream saved(downloads / "_CON.zip", std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>(saved), std::istreambuf_iterator<char>()};
    const auto progress = events.Snapshot();
    if (!Check(bytes == kFixtureBytes && std::any_of(progress.begin(), progress.end(), [&](const auto& event) {
            const auto* transfer = std::get_if<TransferEvent>(&event);
            return transfer && transfer->identity.transfer_id == accepted.transfer_id &&
                transfer->state == TransferState::InProgress && transfer->bytes_transferred == 5 &&
                transfer->total_bytes == 11;
        }), "save binary bytes and report download progress")) return false;

    const auto rejected = offer(4, "77", "declined");
    const auto failed = offer(5, "77", "failure");
    if (!events.WaitFor([&](const auto& seen) {
        return std::any_of(seen.begin(), seen.end(), [&](const ProviderEvent& event) {
            const auto* incoming = std::get_if<IncomingOffer>(&event);
            return incoming && incoming->identity.transfer_id == failed.transfer_id;
        });
    })) return Check(false, "queue consecutive Telegram offers");
    if (!Check(provider.Decline(rejected, "Declined by recipient").accepted &&
               api->GetFileCalls() == 1, "decline without downloading") ||
        !Check(provider.AcceptFile(failed, destination).accepted, "queue failed download")) return false;
    if (!Check(events.WaitFor([&](const auto& seen) {
        return std::any_of(seen.begin(), seen.end(), [&](const ProviderEvent& event) {
            const auto* transfer = std::get_if<TransferEvent>(&event);
            return transfer && transfer->identity.transfer_id == failed.transfer_id &&
                transfer->state == TransferState::Failed && transfer->reason &&
                transfer->reason->find("[redacted]") != std::string::npos &&
                transfer->reason->find(settings.bot_token) == std::string::npos;
        });
    }), "redact Telegram token from download failures")) return false;

    auto media_offer = [&](int id, const char* field, Json media) {
        auto update = Json::parse(Update(id, "77", ""));
        update["result"][0]["message"][field] = std::move(media);
        api->QueueUpdateResponse({200, update.dump(), {}});
        return TransferIdentity{{"telegram", "77"}, "telegram-update-" + std::to_string(id)};
    };
    const auto photo = media_offer(6, "photo", Json::array({
        {{"file_id", "full-photo"}, {"width", 100}, {"height", 100}, {"file_size", 11}},
        {{"file_id", "thumbnail"}, {"width", 25}, {"height", 25}, {"file_size", 2}},
    }));
    const auto audio = media_offer(7, "audio",
        {{"file_id", "audio-track"}, {"file_name", "track.mp3"}, {"file_size", 11}});
    const auto has_offer = [](const auto& seen, const TransferIdentity& identity, const char* filename) {
        return std::any_of(seen.begin(), seen.end(), [&](const ProviderEvent& event) {
            const auto* incoming = std::get_if<IncomingOffer>(&event);
            return incoming && incoming->identity.transfer_id == identity.transfer_id &&
                incoming->metadata.display_name == filename && incoming->metadata.byte_length == 11;
        });
    };
    if (!Check(events.WaitFor([&](const auto& seen) {
            return has_offer(seen, photo, "photo.jpg") && has_offer(seen, audio, "track.mp3");
        }) && api->GetFileCalls() == 2, "offer Telegram photo and audio before requesting their files") ||
        !Check(provider.AcceptFile(photo, [&] {
            return connect_ui::MakeDownloadDestination(downloads, "photo.jpg");
        }).accepted, "accept Telegram photo")) return false;

    return Check(events.WaitFor([&](const auto& seen) {
        return HasCompletedTransfer(seen, photo.transfer_id);
    }), "download the largest Telegram photo variant");
}

} // namespace

int main() {
    TemporaryDirectory temporary;
    return TestSettingsPersistence(temporary.Path()) &&
        TestProvider(temporary.Path()) &&
        TestFiles(temporary.Path()) &&
        TestReconnectDisabled(temporary.Path()) ? 0 : 1;
}
