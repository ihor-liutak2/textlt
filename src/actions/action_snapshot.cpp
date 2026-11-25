#include "actions/action_snapshot.hpp"

#include <stdexcept>

#include "nlohmann/json.hpp"

namespace textlt::actions {

using Json = nlohmann::json;

std::string EncodeActionSnapshot(const ActionItem& action) {
    if (action.title.empty() || action.tabs.empty() || action.tabs.size() > 4) {
        throw std::invalid_argument("An action needs a title and one to four tabs.");
    }
    auto tabs = Json::array();
    for (const auto& tab : action.tabs) {
        if (LimitActionCaption(tab.caption) != tab.caption) {
            throw std::invalid_argument("Action captions are limited to 12 characters.");
        }
        tabs.push_back({{"caption", tab.caption}, {"content", tab.content}, {"hide_text", tab.hide_text}});
    }
    return Json({{"title", action.title}, {"tabs", std::move(tabs)}}).dump();
}

std::optional<ActionItem> DecodeActionSnapshot(std::string_view json) {
    if (json.find('\0') != std::string_view::npos) return std::nullopt;
    try {
        const auto object = Json::parse(json.begin(), json.end(), [](int depth, Json::parse_event_t, Json&) {
            if (depth > 8) throw std::runtime_error("snapshot is nested too deeply");
            return true;
        });
        ActionItem action;
        action.title = object.at("title").get<std::string>();
        const auto& tabs = object.at("tabs");
        if (action.title.empty() || !tabs.is_array() || tabs.empty() || tabs.size() > 4) return std::nullopt;
        for (const auto& item : tabs) {
            ActionTab tab{
                item.at("caption").get<std::string>(), item.at("content").get<std::string>(),
                item.at("hide_text").get<bool>()
            };
            if (LimitActionCaption(tab.caption) != tab.caption) return std::nullopt;
            action.tabs.push_back(std::move(tab));
        }
        return action;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

} // namespace textlt::actions
