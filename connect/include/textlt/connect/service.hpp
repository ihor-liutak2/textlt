#pragma once

#include <map>
#include <string_view>

#include "textlt/connect/content_handler.hpp"
#include "textlt/connect/provider.hpp"

namespace textlt::connect {

enum class RegistrationResult { Registered, DuplicateId, InvalidRegistration };

enum class HandlerResolutionStatus { Found, UnknownType, UnsupportedVersion };

struct HandlerResolution {
    HandlerResolutionStatus status;
    ContentHandler* handler;
};

// The host serializes access to the registry and stops started providers before
// destroying it. Lookup pointers are borrowed from the registry.
class ConnectService {
public:
    RegistrationResult RegisterProvider(std::unique_ptr<IConnectProvider> provider);
    RegistrationResult RegisterHandler(std::unique_ptr<ContentHandler> handler);

    IConnectProvider* FindProvider(std::string_view provider_id) const;
    std::vector<Capability> StructuredCapabilities() const;
    HandlerResolution ResolveHandler(std::string_view type_id, SchemaVersion version) const;

private:
    struct HandlerEntry {
        std::vector<SchemaVersion> versions;
        std::unique_ptr<ContentHandler> handler;
    };

    std::map<std::string, std::unique_ptr<IConnectProvider>, std::less<>> providers_;
    std::map<std::string, HandlerEntry, std::less<>> handlers_;
};

} // namespace textlt::connect
