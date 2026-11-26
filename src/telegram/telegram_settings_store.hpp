#pragma once

#include <filesystem>
#include <string>

#include "telegram_settings.hpp"

namespace textlt::telegram {

class TelegramSettingsStore {
public:
    explicit TelegramSettingsStore(std::filesystem::path path = {});

    const std::filesystem::path& Path() const { return path_; }
    TelegramSettings Load() const;
    bool Save(const TelegramSettings& settings, std::string& error) const;

    static std::filesystem::path DefaultPath();

private:
    std::filesystem::path path_;
};

} // namespace textlt::telegram
