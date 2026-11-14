#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace textlt::actions {

struct ImportedActionValue {
    std::string name;
    std::string value;
};

struct ActionConfigImportResult {
    bool success = false;
    std::vector<ImportedActionValue> values;
    std::string error;
};

class ActionConfigImporter {
public:
    static ActionConfigImportResult ImportFile(const std::filesystem::path& path);
    static ActionConfigImportResult Parse(
        const std::filesystem::path& path,
        const std::string& content);
};

} // namespace textlt::actions
