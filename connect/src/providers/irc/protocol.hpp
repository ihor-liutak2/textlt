#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace textlt::connect::irc_detail {

struct Message {
    std::string_view prefix;
    std::string_view command;
    std::vector<std::string_view> parameters;
};

Message Parse(std::string_view line);
std::string Fold(std::string_view value, std::string_view mapping);
bool Token(std::string_view value);
bool Nickname(std::string_view value);
bool Utf8(std::string_view value);
std::vector<std::string> Privmsg(std::string_view target, std::string_view text);
std::vector<std::string> PlainAuthentication(std::string_view account, std::string_view password);

} // namespace textlt::connect::irc_detail
