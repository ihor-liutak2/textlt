#include "textlt/connect/service.hpp"

#include <algorithm>
#include <utility>

namespace textlt::connect {

RegistrationResult ConnectService::RegisterProvider(std::unique_ptr<IConnectProvider> provider) {
    if (!provider) {
        return RegistrationResult::InvalidRegistration;
    }

    auto id = provider->Id();
    if (id.empty()) {
        return RegistrationResult::InvalidRegistration;
    }

    auto insertion = providers_.try_emplace(std::move(id), std::move(provider));
    return insertion.second ? RegistrationResult::Registered : RegistrationResult::DuplicateId;
}

RegistrationResult ConnectService::RegisterHandler(std::unique_ptr<ContentHandler> handler) {
    if (!handler) {
        return RegistrationResult::InvalidRegistration;
    }

    auto id = handler->TypeId();
    if (id.empty() || id == kTextTypeId || id == kFileTypeId) {
        return RegistrationResult::InvalidRegistration;
    }

    auto versions = handler->SupportedVersions();
    std::sort(versions.begin(), versions.end());
    if (versions.empty() || versions.front() == 0 ||
        std::adjacent_find(versions.begin(), versions.end()) != versions.end()) {
        return RegistrationResult::InvalidRegistration;
    }

    auto insertion = handlers_.try_emplace(std::move(id),
                                           HandlerEntry{std::move(versions), std::move(handler)});
    return insertion.second ? RegistrationResult::Registered : RegistrationResult::DuplicateId;
}

IConnectProvider* ConnectService::FindProvider(std::string_view provider_id) const {
    auto provider = providers_.find(provider_id);
    return provider == providers_.end() ? nullptr : provider->second.get();
}

std::vector<Capability> ConnectService::StructuredCapabilities() const {
    std::vector<Capability> capabilities;
    for (const auto& [type, entry] : handlers_) capabilities.push_back({type, entry.versions});
    return capabilities;
}

HandlerResolution ConnectService::ResolveHandler(std::string_view type_id,
                                                 SchemaVersion version) const {
    auto entry = handlers_.find(type_id);
    if (entry == handlers_.end()) {
        return {HandlerResolutionStatus::UnknownType, nullptr};
    }

    const auto& versions = entry->second.versions;
    if (!std::binary_search(versions.begin(), versions.end(), version)) {
        return {HandlerResolutionStatus::UnsupportedVersion, nullptr};
    }

    return {HandlerResolutionStatus::Found, entry->second.handler.get()};
}

} // namespace textlt::connect
