#include "file_manager.hpp"

#include <fstream>
#include <iterator>
#include <stdexcept>

namespace textlt {

namespace {

std::string ReadFileContent(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Unable to open file: " + path.string());
    }

    return {
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>()};
}

void WriteFileContent(const std::filesystem::path& path, const std::string& content) {
    if (path.empty() || path == "Untitled") {
        throw std::runtime_error("No file path selected.");
    }

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Unable to open file for writing: " + path.string());
    }

    file << content;
    if (!file) {
        throw std::runtime_error("Unable to write file: " + path.string());
    }
}

} // namespace

std::shared_ptr<Document> FileManager::Open(const std::filesystem::path& path, std::string& error) {
    try {
        auto doc = std::make_shared<Document>(path);
        doc->LoadContent(ReadFileContent(path), path);
        return doc;
    } catch (const std::exception& e) {
        error = e.what();
        return nullptr;
    }
}

bool FileManager::Save(std::shared_ptr<Document> doc, std::string& error) {
    if (!doc) {
        error = "Document is null.";
        return false;
    }

    return SaveAs(doc, doc->path, error);
}

bool FileManager::SaveAs(std::shared_ptr<Document> doc, const std::filesystem::path& path, std::string& error) {
    if (!doc) {
        error = "Document is null.";
        return false;
    }

    try {
        WriteFileContent(path, doc->ToContent());
        doc->SetPath(path);
        doc->is_dirty = false;
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

} // namespace textlt
