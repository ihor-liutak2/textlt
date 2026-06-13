#pragma once

#include <filesystem>

#include "editor_config.hpp"

namespace textlt {

class ConfigManager {
public:
    explicit ConfigManager(std::filesystem::path path = "config.json");

    EditorConfig Load() const;
    void Save(const EditorConfig& config) const;

private:
    std::filesystem::path path_;
};

} // namespace textlt
