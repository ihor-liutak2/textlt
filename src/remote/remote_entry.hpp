#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace textlt {

enum class RemoteEntryType {
    Directory,
    File,
    Symlink,
    Other,
};

struct RemoteEntry {
    std::string path;
    std::string name;
    RemoteEntryType type = RemoteEntryType::Other;
    std::optional<std::uintmax_t> size;
    bool hidden = false;
    std::string modified;
};

} // namespace textlt
