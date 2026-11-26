#include "protocol.hpp"

#include <algorithm>

#include <openssl/evp.h>

#include "textlt/connect/irc_provider.hpp"

namespace textlt::connect {
namespace irc_detail {
namespace {

std::string_view TakeWord(std::string_view& input) {
    const auto end = input.find(' ');
    const auto word = input.substr(0, end);
    input.remove_prefix(word.size());
    const auto next = input.find_first_not_of(' ');
    input.remove_prefix(next == std::string_view::npos ? input.size() : next);
    return word;
}

bool Continuation(unsigned char byte) {
    return (byte & 0xc0) == 0x80;
}

} // namespace

Message Parse(std::string_view line) {
    Message message;
    if (!line.empty() && line.front() == '@') TakeWord(line);
    if (!line.empty() && line.front() == ':') {
        line.remove_prefix(1);
        message.prefix = TakeWord(line);
    }
    message.command = TakeWord(line);
    while (!line.empty()) {
        if (line.front() == ':') {
            message.parameters.push_back(line.substr(1));
            break;
        }
        message.parameters.push_back(TakeWord(line));
    }
    return message;
}

std::string Fold(std::string_view value, std::string_view mapping) {
    std::string folded(value);
    for (char& ch : folded) {
        if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
        else if (mapping != "ascii") {
            if (ch == '[') ch = '{';
            else if (ch == ']') ch = '}';
            else if (ch == '\\') ch = '|';
            else if (ch == '^' && mapping == "rfc1459") ch = '~';
        }
    }
    return folded;
}

bool Token(std::string_view value) {
    return !value.empty() && value.size() <= 200 &&
        std::none_of(value.begin(), value.end(), [](unsigned char ch) {
            return ch <= 32 || ch == 127 || ch == ':' || ch == ',';
        });
}

bool Nickname(std::string_view value) {
    return Token(value) && !IsIrcChannel(value) &&
        value.find_first_of("!@*?") == std::string_view::npos;
}

bool Utf8(std::string_view value) {
    for (std::size_t i = 0; i < value.size();) {
        const auto lead = static_cast<unsigned char>(value[i++]);
        if (lead < 0x80) continue;
        const unsigned count = lead >= 0xc2 && lead <= 0xdf ? 1
            : lead >= 0xe0 && lead <= 0xef ? 2 : lead >= 0xf0 && lead <= 0xf4 ? 3 : 0;
        if (!count || i + count > value.size()) return false;
        const auto second = static_cast<unsigned char>(value[i]);
        if ((lead == 0xe0 && second < 0xa0) || (lead == 0xed && second >= 0xa0) ||
            (lead == 0xf0 && second < 0x90) || (lead == 0xf4 && second >= 0x90)) return false;
        for (unsigned n = 0; n < count; ++n) {
            if (!Continuation(value[i++])) return false;
        }
    }
    return true;
}

std::vector<std::string> Privmsg(std::string_view target, std::string_view text) {
    if (!Token(target) || !Utf8(text) || text.find('\0') != std::string_view::npos) return {};
    const std::string prefix = "PRIVMSG " + std::string(target) + " :";
    const auto limit = 512 - prefix.size() - 2;
    std::vector<std::string> lines;
    while (!text.empty()) {
        const auto end = text.find_first_of("\r\n");
        auto line = text.substr(0, end);
        while (!line.empty()) {
            auto count = std::min(limit, line.size());
            if (count < line.size()) {
                while (Continuation(line[count])) --count;
                const auto space = line.substr(0, count).find_last_of(' ');
                if (space != std::string_view::npos && space >= count / 2) count = space + 1;
            }
            lines.push_back(prefix + std::string(line.substr(0, count)) + "\r\n");
            line.remove_prefix(count);
        }
        if (end == std::string_view::npos) break;
        text.remove_prefix(end + 1);
    }
    return lines;
}

std::vector<std::string> PlainAuthentication(std::string_view account, std::string_view password) {
    std::string payload(1, '\0');
    payload += account;
    payload += '\0';
    payload += password;
    std::string encoded(4 * ((payload.size() + 2) / 3) + 1, '\0');
    const auto count = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(encoded.data()),
        reinterpret_cast<const unsigned char*>(payload.data()), static_cast<int>(payload.size()));
    encoded.resize(count);
    std::vector<std::string> commands;
    for (std::size_t pos = 0; pos < encoded.size(); pos += 400) {
        commands.push_back("AUTHENTICATE " + encoded.substr(pos, 400) + "\r\n");
    }
    if (encoded.size() % 400 == 0) commands.emplace_back("AUTHENTICATE +\r\n");
    return commands;
}

} // namespace irc_detail

bool IsIrcChannel(std::string_view target) {
    return !target.empty() && std::string_view("#&+!").find(target.front()) != std::string_view::npos;
}

std::string IrcServerFromPeer(std::string_view peer_id) {
    return std::string(peer_id.substr(0, peer_id.find('/')));
}

std::string ValidateIrcProfile(const IrcProfile& profile) {
    using namespace irc_detail;
    const auto clean = [](std::string_view value, std::size_t limit) {
        return value.size() <= limit && Utf8(value) &&
            std::none_of(value.begin(), value.end(), [](unsigned char ch) { return ch < 32 || ch == 127; });
    };
    if (!Token(profile.id) || profile.id.find('/') != std::string::npos) return "Invalid connection ID.";
    if (profile.name.empty() || !clean(profile.name, 100)) return "Enter a valid connection name.";
    if (!clean(profile.server, 253) || profile.server.empty() ||
        profile.server.find_first_of(" /?#@[]") != std::string::npos || !profile.port) {
        return "Enter a server hostname or IP address and a valid port.";
    }
    if (!Nickname(profile.nickname) || !Utf8(profile.nickname) ||
        !Token(profile.username) || !Utf8(profile.username)) return "Enter a valid nickname and username.";
    const bool sasl = !profile.sasl_account.empty() || !profile.sasl_password.empty();
    if (sasl && (!profile.tls || profile.sasl_account.empty() || profile.sasl_password.empty())) {
        return "SASL PLAIN requires TLS, an account and a password.";
    }
    if (!clean(profile.sasl_account, 200) || !clean(profile.sasl_password, 400) ||
        !clean(profile.server_password, 400)) return "An IRC credential is too long or invalid.";
    for (const auto& channel : profile.channels) {
        if (!Token(channel) || !Utf8(channel) || !IsIrcChannel(channel) || channel.size() < 2) {
            return "Auto-join channels must start with #, &, + or ! and contain no spaces.";
        }
    }
    return {};
}

} // namespace textlt::connect
