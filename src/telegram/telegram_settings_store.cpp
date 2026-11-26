#include "telegram_settings_store.hpp"

#include <algorithm>
#include <set>
#include <utility>

#include "editor_config.hpp"
#include "json_utils.hpp"

namespace textlt::telegram {
namespace {

using textlt::Json;

std::string ChatTitle(const Json& value) {
    return JsonString(value, "title");
}

} // namespace

TelegramSettingsStore::TelegramSettingsStore(std::filesystem::path path)
    : path_(path.empty() ? DefaultPath() : std::move(path)) {}

std::filesystem::path TelegramSettingsStore::DefaultPath() {
    return EditorConfig::DefaultConfigPath().parent_path() / "telegram-settings.json";
}

TelegramSettings TelegramSettingsStore::Load() const {
    TelegramSettings settings;
    const Json root = LoadJsonObject(path_);
    settings.bot_token = JsonString(root, "bot_token");
    settings.bot_first_name = JsonString(root, "bot_first_name");
    settings.bot_username = JsonString(root, "bot_username");
    settings.auto_reconnect = JsonBool(root, "auto_reconnect", true);
    settings.start_on_launch = JsonBool(root, "start_on_launch", false);
    const auto update_id = root.find("last_update_id");
    if (update_id != root.end() && update_id->is_number_integer()) {
        try {
            settings.last_update_id = update_id->get<std::int64_t>();
        } catch (const Json::exception&) {
            settings.last_update_id = -1;
        }
    }

    const auto authorized = root.find("authorized_chat_ids");
    if (authorized != root.end() && authorized->is_array()) {
        std::set<std::string> unique;
        for (const auto& entry : *authorized) {
            if (entry.is_string()) {
                const auto id = entry.get<std::string>();
                if (!id.empty() && unique.insert(id).second) {
                    settings.authorized_chat_ids.push_back(id);
                }
            }
        }
    }

    const auto known = root.find("known_chats");
    if (known != root.end() && known->is_array()) {
        for (const auto& entry : *known) {
            if (!entry.is_object()) continue;
            TelegramChat chat;
            chat.id = JsonString(entry, "id");
            chat.title = ChatTitle(entry);
            chat.username = JsonString(entry, "username");
            chat.type = JsonString(entry, "type");
            if (!chat.id.empty()) settings.known_chats.push_back(std::move(chat));
        }
    }
    return settings;
}

bool TelegramSettingsStore::Save(const TelegramSettings& settings, std::string& error) const {
    error.clear();
    if (path_.empty()) {
        error = "Telegram settings path is empty.";
        return false;
    }

    auto authorized = Json::array();
    std::set<std::string> unique;
    for (const auto& id : settings.authorized_chat_ids) {
        if (!id.empty() && unique.insert(id).second) authorized.push_back(id);
    }
    auto known = Json::array();
    for (const auto& chat : settings.known_chats) {
        if (chat.id.empty()) continue;
        known.push_back({
            {"id", chat.id}, {"title", chat.title},
            {"username", chat.username}, {"type", chat.type},
        });
    }

    const Json root = {
        {"bot_token", settings.bot_token},
        {"bot_first_name", settings.bot_first_name},
        {"bot_username", settings.bot_username},
        {"authorized_chat_ids", std::move(authorized)},
        {"known_chats", std::move(known)},
        {"last_update_id", settings.last_update_id},
        {"auto_reconnect", settings.auto_reconnect},
        {"start_on_launch", settings.start_on_launch},
    };
    if (WriteJsonAtomically(path_, root, true)) return true;
    error = "Could not save Telegram settings.";
    return false;
}

} // namespace textlt::telegram
