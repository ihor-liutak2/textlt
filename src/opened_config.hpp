#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace textlt {

struct OpenedFileState {
    std::filesystem::path path;
    std::string content;
    size_t row = 0;
    size_t column = 0;
    bool memory_only = false;
};

struct OpenedConfig {
    std::vector<OpenedFileState> files;
    size_t active_index = 0;
};

class OpenedConfigStore {
public:
    OpenedConfigStore();

    OpenedConfig Load() const;
    bool Save(const OpenedConfig& config) const;
    std::filesystem::path Path() const;

private:
    std::filesystem::path path_;
};

} // namespace textlt
