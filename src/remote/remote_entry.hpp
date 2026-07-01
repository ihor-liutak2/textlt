#pragma once

#include <cstdint>
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
    std::uintmax_t size = 0;
    bool hidden = false;
};

} // namespace textlt
