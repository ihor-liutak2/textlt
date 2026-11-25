#include "page.hpp"

#include "browser_assets.hpp"

#include <algorithm>

namespace textlt::connect::browser {
namespace {

using namespace localsend;

std::string Authority(Https& client) {
    const auto endpoint = client.Socket().local_endpoint();
    auto authority = endpoint.address().to_string();
    if (endpoint.port() != 443) authority += ':' + std::to_string(endpoint.port());
    return authority;
}

bool ValidColor(const std::string& color) {
    return color.size() == 7 && color.front() == '#' &&
        std::all_of(color.begin() + 1, color.end(), [](char ch) {
            return (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F') || (ch >= 'a' && ch <= 'f');
        });
}

std::string RenderCss(const BrowserTheme& theme) {
    const BrowserTheme defaults;
    std::string css = ":root{";
    const auto append = [&](const char* property, const std::string& color, const std::string& fallback) {
        css += property;
        css += ':';
        css += ValidColor(color) ? color : fallback;
        css += ';';
    };
    append("--background", theme.background, defaults.background);
    append("--foreground", theme.foreground, defaults.foreground);
    append("--input-background", theme.input_background, defaults.input_background);
    append("--input-foreground", theme.input_foreground, defaults.input_foreground);
    append("--accent", theme.accent, defaults.accent);
    append("--accent-text", theme.accent_text, defaults.accent_text);
    css += "}\n";
    css += kCss;
    return css;
}

std::string RenderHtml(Protocol protocol) {
    std::string html = kHtml;
    const std::string_view mode = "%PROTOCOL%";
    html.replace(html.find(mode), mode.size(), protocol == Protocol::TextLT ? "textlt" : "localsend");
    const std::string_view label = "%PROTOCOL_LABEL%";
    const char* name = protocol == Protocol::TextLT ? "TextLT transfer" : "LocalSend transfer";
    for (auto position = html.find(label); position != std::string::npos; position = html.find(label)) {
        html.replace(position, label.size(), name);
    }
    return html;
}

} // namespace

bool SameOrigin(Https& client, const Headers& request) {
    const auto host = Authority(client);
    return request.get()[http::field::host] == host && request.get()["Origin"] == "https://" + host;
}

bool ServePage(Https& client, const Headers& request, std::string_view path, Protocol protocol,
               const BrowserThemeSource& theme, Milliseconds timeout) {
    if (!theme || (path != "/" && path != "/browser.css" && path != "/browser.js")) return false;
    const auto host = Authority(client);
    if (request.get()[http::field::host] != host) {
        client.Reply(http::status::forbidden, {}, timeout);
        return true;
    }
    const auto policy = "default-src 'none'; script-src 'self'; style-src 'self'; connect-src 'self' wss://" +
        host + "; base-uri 'none'; frame-ancestors 'none'; form-action 'none'";
    if (path == "/browser.css") client.ReplyPage(RenderCss(theme()), "text/css; charset=utf-8", policy, timeout);
    else if (path == "/browser.js") client.ReplyPage(kJavascript, "text/javascript; charset=utf-8", policy, timeout);
    else client.ReplyPage(RenderHtml(protocol), "text/html; charset=utf-8", policy, timeout);
    return true;
}

} // namespace textlt::connect::browser
