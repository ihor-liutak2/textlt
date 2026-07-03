#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace textlt {

enum class TextImportFormat {
    Unsupported,
    Fb2,
    Fb2Zip,
    Docx,
    Rtf,
    Odt,
    GoogleDocShortcut,
};

enum class TextImportEntryKind {
    ParentDirectory,
    Directory,
    File,
};

struct TextImportEntry {
    std::filesystem::path path;
    std::string name;
    TextImportEntryKind kind = TextImportEntryKind::File;
    TextImportFormat format = TextImportFormat::Unsupported;
};

struct TextImportResult {
    bool success = false;
    std::string text;
    std::string error;
};

class TextImporter {
public:
    TextImportResult ImportFile(const std::filesystem::path& path) const;

    std::vector<TextImportEntry> ListDirectory(
        const std::filesystem::path& directory,
        std::string& error) const;

    static TextImportFormat DetectFormat(const std::filesystem::path& path);
    static bool IsSupportedFile(const std::filesystem::path& path);

private:
    TextImportResult ImportFb2File(const std::filesystem::path& path) const;
    TextImportResult ImportFb2ZipFile(const std::filesystem::path& path) const;
    TextImportResult ImportDocxFile(const std::filesystem::path& path) const;
    TextImportResult ImportRtfFile(const std::filesystem::path& path) const;
    TextImportResult ImportOdtFile(const std::filesystem::path& path) const;
    TextImportResult ImportGoogleDocShortcut(const std::filesystem::path& path) const;
};

} // namespace textlt
