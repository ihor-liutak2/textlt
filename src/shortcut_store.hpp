#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>

namespace textlt {

class ShortcutStore {
public:
    explicit ShortcutStore(std::filesystem::path path = {});

    std::unordered_map<std::string, std::string> Load() const;
    bool Save(const std::unordered_map<std::string, std::string>& overrides, std::string* error = nullptr) const;
    const std::filesystem::path& Path() const { return path_; }

private:
    std::filesystem::path path_;
};

} // namespace textlt
