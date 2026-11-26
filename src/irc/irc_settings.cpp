#include "irc/irc_settings.hpp"

#include <random>
#include <set>

#include "json_utils.hpp"

namespace textlt::irc {

std::string NewProfileId() {
    std::random_device random;
    return std::to_string(random()) + "-" + std::to_string(random()) + "-" + std::to_string(random());
}

std::vector<connect::IrcProfile> LoadSettings(const std::filesystem::path& path) {
    const auto json = LoadJsonObject(path);
    std::vector<connect::IrcProfile> profiles;
    const auto list = json.find("connections");
    if (list == json.end() || !list->is_array()) return profiles;
    std::set<std::string> ids;
    for (const auto& item : *list) {
        if (!item.is_object()) continue;
        connect::IrcProfile profile;
        profile.id = JsonString(item, "id");
        profile.name = JsonString(item, "name", profile.name);
        profile.server = JsonString(item, "server", profile.server);
        const auto port = JsonInt(item, "port", profile.port);
        if (port < 1 || port > 65535) continue;
        profile.port = static_cast<std::uint16_t>(port);
        profile.tls = JsonBool(item, "tls", true);
        profile.nickname = JsonString(item, "nickname");
        profile.username = JsonString(item, "username");
        profile.sasl_account = JsonString(item, "sasl_account");
        profile.sasl_password = JsonString(item, "sasl_password");
        profile.server_password = JsonString(item, "server_password");
        profile.auto_reconnect = JsonBool(item, "auto_reconnect", true);
        profile.start_on_launch = JsonBool(item, "start_on_launch", false);
        const auto channels = item.find("channels");
        if (channels != item.end() && channels->is_array()) {
            for (const auto& channel : *channels) {
                if (channel.is_string()) profile.channels.push_back(channel.get<std::string>());
            }
        }
        if (connect::ValidateIrcProfile(profile).empty() && ids.insert(profile.id).second) {
            profiles.push_back(std::move(profile));
        }
    }
    return profiles;
}

bool SaveSettings(const std::filesystem::path& path, const std::vector<connect::IrcProfile>& profiles) {
    Json list = Json::array();
    for (const auto& p : profiles) {
        list.push_back({{"id", p.id}, {"name", p.name}, {"server", p.server}, {"port", p.port},
            {"tls", p.tls}, {"nickname", p.nickname}, {"username", p.username},
            {"sasl_account", p.sasl_account}, {"sasl_password", p.sasl_password},
            {"server_password", p.server_password}, {"channels", p.channels},
            {"auto_reconnect", p.auto_reconnect}, {"start_on_launch", p.start_on_launch}});
    }
    return WriteJsonAtomically(path, {{"connections", std::move(list)}}, true);
}

} // namespace textlt::irc
