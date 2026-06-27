#include "text_importer.hpp"

#include <archive.h>
#include <archive_entry.h>

#include <cassert>
#include <filesystem>
#include <string>

namespace {

void WriteFb2Zip(const std::filesystem::path& path, const std::string& content) {
    archive* writer = archive_write_new();
    assert(writer);
    assert(archive_write_set_format_zip(writer) == ARCHIVE_OK);
    assert(archive_write_open_filename(writer, path.string().c_str()) == ARCHIVE_OK);

    archive_entry* entry = archive_entry_new();
    assert(entry);
    archive_entry_set_pathname(entry, "nested/book.fb2");
    archive_entry_set_filetype(entry, AE_IFREG);
    archive_entry_set_perm(entry, 0644);
    archive_entry_set_size(entry, static_cast<la_int64_t>(content.size()));
    assert(archive_write_header(writer, entry) == ARCHIVE_OK);
    assert(archive_write_data(writer, content.data(), content.size()) ==
           static_cast<la_ssize_t>(content.size()));

    archive_entry_free(entry);
    assert(archive_write_close(writer) == ARCHIVE_OK);
    assert(archive_write_free(writer) == ARCHIVE_OK);
}

} // namespace

int main() {
    using textlt::TextImportFormat;
    using textlt::TextImporter;

    assert(TextImporter::DetectFormat("book.fb2.zip") == TextImportFormat::Fb2Zip);
    assert(TextImporter::DetectFormat("BOOK.FB2.ZIP") == TextImportFormat::Fb2Zip);
    assert(TextImporter::DetectFormat("book.zip") == TextImportFormat::Unsupported);

    const std::filesystem::path archive_path =
        std::filesystem::temp_directory_path() / "textlt_text_importer_test.fb2.zip";
    WriteFb2Zip(
        archive_path,
        R"(<?xml version="1.0" encoding="utf-8"?>
<FictionBook xmlns="http://www.gribuser.ru/xml/fictionbook/2.0">
  <body><section><title><p>Test title</p></title><p>Imported text.</p></section></body>
</FictionBook>)");

    const textlt::TextImportResult result = TextImporter().ImportFile(archive_path);
    std::filesystem::remove(archive_path);

    assert(result.success);
    assert(result.text.find("Test title") != std::string::npos);
    assert(result.text.find("Imported text.") != std::string::npos);
    return 0;
}
