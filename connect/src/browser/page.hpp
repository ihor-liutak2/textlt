#pragma once

#include "../providers/localsend/https.hpp"

namespace textlt::connect::browser {

enum class Protocol { TextLT, LocalSend };

bool SameOrigin(localsend::Https& client, const localsend::Headers& request);

// Returns true when an asset path was handled.
bool ServePage(localsend::Https& client, const localsend::Headers& request, std::string_view path,
               Protocol protocol, const BrowserThemeSource& theme, localsend::Milliseconds timeout);

} // namespace textlt::connect::browser
