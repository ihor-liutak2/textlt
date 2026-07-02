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

void WriteDocxZip(
    const std::filesystem::path& path,
    const std::string& document_xml) {
    archive* writer = archive_write_new();
    assert(writer);
    assert(archive_write_set_format_zip(writer) == ARCHIVE_OK);
    assert(archive_write_open_filename(writer, path.string().c_str()) == ARCHIVE_OK);

    auto write_entry = [&](const char* name, const std::string& data) {
        archive_entry* e = archive_entry_new();
        assert(e);
        archive_entry_set_pathname(e, name);
        archive_entry_set_filetype(e, AE_IFREG);
        archive_entry_set_perm(e, 0644);
        archive_entry_set_size(e, static_cast<la_int64_t>(data.size()));
        assert(archive_write_header(writer, e) == ARCHIVE_OK);
        assert(archive_write_data(writer, data.data(), data.size()) ==
               static_cast<la_ssize_t>(data.size()));
        archive_entry_free(e);
    };

    write_entry("[Content_Types].xml",
        R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>)"
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/>)"
        R"(</Types>)");

    write_entry("_rels/.rels",
        R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>)"
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/>)"
        R"(</Relationships>)");

    write_entry("word/_rels/document.xml.rels",
        R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>)"
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(</Relationships>)");

    write_entry("word/document.xml", document_xml);

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

    // FB2 zip test
    const std::filesystem::path archive_path =
        std::filesystem::temp_directory_path() / "textlt_text_importer_test.fb2.zip";
    WriteFb2Zip(
        archive_path,
        R"(<?xml version="1.0" encoding="utf-8"?>
<FictionBook xmlns="http://www.gribuser.ru/xml/fictionbook/2.0">
  <body><section><title><p>Test title</p></title><p>Imported text.</p></section></body>
</FictionBook>)");

    const textlt::TextImportResult fb2_result = TextImporter().ImportFile(archive_path);
    std::filesystem::remove(archive_path);

    assert(fb2_result.success);
    assert(fb2_result.text.find("Test title") != std::string::npos);
    assert(fb2_result.text.find("Imported text.") != std::string::npos);

    // DOCX: paragraph only
    const std::filesystem::path docx_path =
        std::filesystem::temp_directory_path() / "textlt_text_importer_test_no_table.docx";
    WriteDocxZip(
        docx_path,
        R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>)"
        R"(<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">)"
        R"(<w:body>)"
        R"(<w:p><w:r><w:t>Hello world</w:t></w:r></w:p>)"
        R"(</w:body>)"
        R"(</w:document>)");

    const textlt::TextImportResult docx_no_table = TextImporter().ImportFile(docx_path);
    std::filesystem::remove(docx_path);

    assert(docx_no_table.success);
    assert(docx_no_table.text.find("Hello world") != std::string::npos);

    // DOCX: table with content
    const std::filesystem::path docx_table_path =
        std::filesystem::temp_directory_path() / "textlt_text_importer_test_table.docx";
    WriteDocxZip(
        docx_table_path,
        R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>)"
        R"(<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">)"
        R"(<w:body>)"
        R"(<w:p><w:r><w:t>Before table</w:t></w:r></w:p>)"
        R"(<w:tbl>)"
        R"(<w:tblPr><w:tblW w:w="5000" w:type="pct"/></w:tblPr>)"
        R"(<w:tr>)"
        R"(<w:tc><w:p><w:r><w:t>Cell1</w:t></w:r></w:p></w:tc>)"
        R"(<w:tc><w:p><w:r><w:t>Cell2</w:t></w:r></w:p></w:tc>)"
        R"(<w:tc><w:p><w:r><w:t>Cell3</w:t></w:r></w:p></w:tc>)"
        R"(</w:tr>)"
        R"(<w:tr>)"
        R"(<w:tc><w:p><w:r><w:t>Row2Col1</w:t></w:r></w:p></w:tc>)"
        R"(<w:tc><w:p><w:r><w:t>Row2Col2</w:t></w:r></w:p></w:tc>)"
        R"(<w:tc><w:p><w:r><w:t>Row2Col3</w:t></w:r></w:p></w:tc>)"
        R"(</w:tr>)"
        R"(</w:tbl>)"
        R"(<w:p><w:r><w:t>After table</w:t></w:r></w:p>)"
        R"(</w:body>)"
        R"(</w:document>)");

    const textlt::TextImportResult docx_table = TextImporter().ImportFile(docx_table_path);
    std::filesystem::remove(docx_table_path);

    assert(docx_table.success);
    assert(docx_table.text.find("Before table") != std::string::npos);
    assert(docx_table.text.find("After table") != std::string::npos);
    assert(docx_table.text.find("Cell1") != std::string::npos);
    assert(docx_table.text.find("Cell2") != std::string::npos);
    assert(docx_table.text.find("Cell3") != std::string::npos);
    assert(docx_table.text.find("Row2Col1") != std::string::npos);
    assert(docx_table.text.find("Row2Col2") != std::string::npos);
    assert(docx_table.text.find("Row2Col3") != std::string::npos);

    // DOCX: table inside sdt
    const std::filesystem::path docx_sdt_path =
        std::filesystem::temp_directory_path() / "textlt_text_importer_test_sdt.docx";
    WriteDocxZip(
        docx_sdt_path,
        R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>)"
        R"(<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">)"
        R"(<w:body>)"
        R"(<w:sdt>)"
        R"(<w:sdtContent>)"
        R"(<w:tbl>)"
        R"(<w:tblPr><w:tblW w:w="5000" w:type="pct"/></w:tblPr>)"
        R"(<w:tr>)"
        R"(<w:tc><w:p><w:r><w:t>SdtCell</w:t></w:r></w:p></w:tc>)"
        R"(</w:tr>)"
        R"(</w:tbl>)"
        R"(</w:sdtContent>)"
        R"(</w:sdt>)"
        R"(</w:body>)"
        R"(</w:document>)");

    const textlt::TextImportResult docx_sdt = TextImporter().ImportFile(docx_sdt_path);
    std::filesystem::remove(docx_sdt_path);

    assert(docx_sdt.success);
    assert(docx_sdt.text.find("SdtCell") != std::string::npos);

    // DOCX: table nested in sdt with surrounding paragraphs
    const std::filesystem::path docx_deep_path =
        std::filesystem::temp_directory_path() / "textlt_text_importer_test_deep.docx";
    WriteDocxZip(
        docx_deep_path,
        R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>)"
        R"(<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">)"
        R"(<w:body>)"
        R"(<w:sdt>)"
        R"(<w:sdtContent>)"
        R"(<w:p><w:r><w:t>Before</w:t></w:r></w:p>)"
        R"(<w:tbl>)"
        R"(<w:tblPr><w:tblW w:w="5000" w:type="pct"/></w:tblPr>)"
        R"(<w:tr>)"
        R"(<w:tc><w:p><w:r><w:t>DeepCell</w:t></w:r></w:p></w:tc>)"
        R"(</w:tr>)"
        R"(</w:tbl>)"
        R"(<w:p><w:r><w:t>After</w:t></w:r></w:p>)"
        R"(</w:sdtContent>)"
        R"(</w:sdt>)"
        R"(</w:body>)"
        R"(</w:document>)");

    const textlt::TextImportResult docx_deep = TextImporter().ImportFile(docx_deep_path);
    std::filesystem::remove(docx_deep_path);

    assert(docx_deep.success);
    assert(docx_deep.text.find("Before") != std::string::npos);
    assert(docx_deep.text.find("DeepCell") != std::string::npos);
    assert(docx_deep.text.find("After") != std::string::npos);

    // DOCX: nested table inside another table cell
    const std::filesystem::path docx_nested_path =
        std::filesystem::temp_directory_path() / "textlt_text_importer_test_nested.docx";
    WriteDocxZip(
        docx_nested_path,
        R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>)"
        R"(<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">)"
        R"(<w:body>)"
        R"(<w:tbl>)"
        R"(<w:tblPr><w:tblW w:w="5000" w:type="pct"/></w:tblPr>)"
        R"(<w:tr>)"
        R"(<w:tc>)"
        R"(<w:p><w:r><w:t>OuterCell</w:t></w:r></w:p>)"
        R"(<w:tbl>)"
        R"(<w:tblPr><w:tblW w:w="5000" w:type="pct"/></w:tblPr>)"
        R"(<w:tr>)"
        R"(<w:tc><w:p><w:r><w:t>NestedCell</w:t></w:r></w:p></w:tc>)"
        R"(</w:tr>)"
        R"(</w:tbl>)"
        R"(</w:tc>)"
        R"(</w:tr>)"
        R"(</w:tbl>)"
        R"(</w:body>)"
        R"(</w:document>)");

    const textlt::TextImportResult docx_nested = TextImporter().ImportFile(docx_nested_path);
    std::filesystem::remove(docx_nested_path);

    assert(docx_nested.success);
    assert(docx_nested.text.find("OuterCell") != std::string::npos);
    assert(docx_nested.text.find("NestedCell") != std::string::npos);

    return 0;
}
