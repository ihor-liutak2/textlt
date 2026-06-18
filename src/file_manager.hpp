#pragma once

#include "document.hpp"
#include <memory>
#include <string>
#include <filesystem>

namespace textlt {

class FileManager {
public:
    FileManager() = default;

    // Opens an existing file and returns a populated Document.
    std::shared_ptr<Document> Open(const std::filesystem::path& path, std::string& error);

    // Saves the document content to its current path.
    bool Save(std::shared_ptr<Document> doc, std::string& error);

    // Saves the document content to a new path and updates document path/type after success.
    bool SaveAs(std::shared_ptr<Document> doc, const std::filesystem::path& path, std::string& error);
};

} // namespace textlt
