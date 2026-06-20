#pragma once

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <system_error>

#include "nlohmann/json.hpp"

namespace textlt {

using Json = nlohmann::ordered_json;

inline Json LoadJsonObject(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return Json::object();
    }

    Json parsed = Json::parse(file, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        return Json::object();
    }
    return parsed;
}

inline bool WriteJsonAtomically(const std::filesystem::path& path, const Json& json) {
    std::error_code error;
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, error);
        if (error) {
            return false;
        }
    }

    const std::filesystem::path temp_path = path.string() + ".tmp";
    {
        std::ofstream file(temp_path, std::ios::binary | std::ios::trunc);
        if (!file) {
            return false;
        }

        file << json.dump(2) << '\n';
        file.flush();
        if (!file) {
            std::filesystem::remove(temp_path, error);
            return false;
        }
    }

    std::filesystem::rename(temp_path, path, error);
    if (error) {
        std::filesystem::remove(temp_path, error);
        return false;
    }
    return true;
}

inline std::string JsonString(const Json& json, const char* key, std::string fallback = {}) {
    const auto iter = json.find(key);
    return iter != json.end() && iter->is_string() ? iter->get<std::string>() : fallback;
}

inline bool JsonBool(const Json& json, const char* key, bool fallback) {
    const auto iter = json.find(key);
    return iter != json.end() && iter->is_boolean() ? iter->get<bool>() : fallback;
}

inline int JsonInt(const Json& json, const char* key, int fallback) {
    const auto iter = json.find(key);
    if (iter == json.end() || !iter->is_number_integer()) {
        return fallback;
    }
    const auto value = iter->get<long long>();
    if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max()) {
        return fallback;
    }
    return static_cast<int>(value);
}

inline size_t JsonSize(const Json& json, const char* key, size_t fallback) {
    const auto iter = json.find(key);
    if (iter == json.end()) {
        return fallback;
    }
    if (iter->is_number_unsigned()) {
        const auto value = iter->get<unsigned long long>();
        if (value <= static_cast<unsigned long long>(std::numeric_limits<size_t>::max())) {
            return static_cast<size_t>(value);
        }
    }
    if (iter->is_number_integer()) {
        const auto value = iter->get<long long>();
        if (value >= 0 &&
            static_cast<unsigned long long>(value) <=
                static_cast<unsigned long long>(std::numeric_limits<size_t>::max())) {
            return static_cast<size_t>(value);
        }
    }
    return fallback;
}

} // namespace textlt
