#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace textlt::telegram {

struct TelegramChat {
    std::string id;
    std::string title;
    std::string username;
    std::string type;

    bool operator==(const TelegramChat& other) const {
        return id == other.id && title == other.title &&
            username == other.username && type == other.type;
    }
};

struct TelegramSettings {
    std::string bot_token;
    std::string bot_first_name;
    std::string bot_username;
    std::vector<std::string> authorized_chat_ids;
    std::vector<TelegramChat> known_chats;
    std::int64_t last_update_id = -1;
    bool auto_reconnect = true;
    bool start_on_launch = false;
};

} // namespace textlt::telegram
