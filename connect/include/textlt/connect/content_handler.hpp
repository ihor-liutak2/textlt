#pragma once

#include "textlt/connect/types.hpp"

namespace textlt::connect {

struct ImportDestination {
    std::string id;
    std::string label;
};

class ContentHandler {
public:
    virtual ~ContentHandler() = default;

    // The type ID and supported versions are stable for the handler's lifetime.
    virtual std::string TypeId() const = 0;
    virtual std::vector<SchemaVersion> SupportedVersions() const = 0;

    // Returns an error message for invalid content, or nullopt on success.
    virtual std::optional<std::string> Validate(SchemaVersion version,
                                                const StructuredPayload& payload) const = 0;
    // Call after validation succeeds.
    virtual std::string Preview(SchemaVersion version,
                                const StructuredPayload& payload) const = 0;
    virtual std::vector<ImportDestination> Destinations() const { return {}; }

    // Call after validation and user confirmation.
    virtual std::optional<std::string> Import(SchemaVersion, const StructuredPayload&) {
        return "Select a local import destination";
    }
    virtual std::optional<std::string> ImportTo(SchemaVersion version,
                                                const StructuredPayload& payload,
                                                const std::string&) {
        return Import(version, payload);
    }
};

} // namespace textlt::connect
