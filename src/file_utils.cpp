#include "file_utils.hpp"

#include <fstream>

namespace textlt {
namespace {

bool HasExtension(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    const size_t dot = path.find_last_of('.');
    return dot != std::string::npos &&
           (slash == std::string::npos || dot > slash + 1);
}

} // namespace

std::string EnsureTextExtension(std::string path) {
    if (!path.empty() && !HasExtension(path)) {
        path += ".txt";
    }
    return path;
}

std::string TrimTrailingNewlines(std::string value) {
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) {
        value.pop_back();
    }
    return value;
}

std::string FileTypeLabel(const std::string& path) {
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".cpp") {
        return "C++ Source";
    }
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".hpp") {
        return "C++ Header";
    }
    if (path.size() >= 3 && path.substr(path.size() - 3) == ".md") {
        return "Markdown Document";
    }
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".txt") {
        return "Text Document";
    }
    return "Unknown";
}

std::vector<std::string> LoadTextFileLines(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        return {"Unable to load " + path + "."};
    }

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(file, line)) {
        lines.push_back(line);
    }
    if (lines.empty()) {
        lines.push_back("");
    }
    return lines;
}

} // namespace textlt
