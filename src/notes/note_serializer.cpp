#include "notes/note_serializer.hpp"

#include <fstream>
#include <system_error>

#include "nlohmann/json.hpp"

namespace textlt::notes {
namespace {

using Json = nlohmann::ordered_json;

Json MarksToJson(uint8_t marks) {
    Json result = Json::array();
    for (NoteMark mark : {NoteMark::Bold, NoteMark::Italic, NoteMark::Underlined, NoteMark::Strikethrough}) {
        if ((marks & MarkBit(mark)) != 0) result.push_back(MarkName(mark));
    }
    return result;
}

uint8_t MarksFromJson(const Json& json) {
    uint8_t marks = 0;
    if (!json.is_array()) return marks;
    for (const auto& value : json) {
        if (!value.is_string()) continue;
        const std::string name = value.get<std::string>();
        if (name == "bold") marks |= MarkBit(NoteMark::Bold);
        else if (name == "italic") marks |= MarkBit(NoteMark::Italic);
        else if (name == "underlined") marks |= MarkBit(NoteMark::Underlined);
        else if (name == "strikethrough") marks |= MarkBit(NoteMark::Strikethrough);
    }
    return marks;
}

Json NoteToJson(const NoteDocument& note) {
    Json json;
    json["format"] = "textlt-note";
    json["schema_version"] = note.schema_version;
    json["id"] = note.id;
    json["title"] = note.title;
    json["section_id"] = note.section_id ? Json(*note.section_id) : Json(nullptr);
    json["pinned"] = note.pinned;
    json["pinned_at"] = note.pinned_at ? Json(*note.pinned_at) : Json(nullptr);
    json["created_at"] = note.created_at;
    json["updated_at"] = note.updated_at;
    json["saved_at"] = note.saved_at;
    json["deleted_at"] = note.deleted_at ? Json(*note.deleted_at) : Json(nullptr);
    json["revision"] = note.revision;
    json["modified_by_device_id"] = note.modified_by_device_id;
    json["blocks"] = Json::array();
    for (const auto& block : note.blocks) {
        Json item;
        item["id"] = block.id;
        item["type"] = BlockTypeName(block.type);
        item["indent"] = block.indent;
        if (block.type == NoteBlockType::CheckItem) item["checked"] = block.checked;
        item["runs"] = Json::array();
        for (const auto& run : block.runs) {
            item["runs"].push_back({{"text", run.text}, {"marks", MarksToJson(run.marks)}});
        }
        json["blocks"].push_back(std::move(item));
    }
    return json;
}

bool AtomicWrite(const std::filesystem::path& path, const std::string& content, std::string& error) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) { error = "Could not create notes directory: " + ec.message(); return false; }
    const auto temporary = path.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) { error = "Could not open temporary note file."; return false; }
        output << content;
        output.flush();
        if (!output) { error = "Could not write temporary note file."; return false; }
    }
#ifdef _WIN32
    std::filesystem::remove(path, ec);
    ec.clear();
#endif
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        std::filesystem::remove(temporary);
        error = "Could not replace note file: " + ec.message();
        return false;
    }
    return true;
}

} // namespace

bool NoteSerializer::SaveNote(const NoteDocument& note, const std::filesystem::path& path, std::string& error) {
    return AtomicWrite(path, NoteToJson(note).dump(2) + "\n", error);
}

bool NoteSerializer::LoadNote(const std::filesystem::path& path, NoteDocument& note, std::string& error) {
    try {
        std::ifstream input(path, std::ios::binary);
        if (!input) { error = "Could not open note file."; return false; }
        Json json; input >> json;
        if (json.value("format", "") != "textlt-note" || json.value("schema_version", 0) != 1) {
            error = "Unsupported note format."; return false;
        }
        NoteDocument parsed;
        parsed.schema_version = 1;
        parsed.id = json.at("id").get<std::string>();
        parsed.title = json.value("title", "");
        if (json.contains("section_id") && !json["section_id"].is_null()) parsed.section_id = json["section_id"].get<std::string>();
        parsed.pinned = json.value("pinned", false);
        if (json.contains("pinned_at") && !json["pinned_at"].is_null()) parsed.pinned_at = json["pinned_at"].get<std::string>();
        parsed.created_at = json.at("created_at").get<std::string>();
        parsed.updated_at = json.at("updated_at").get<std::string>();
        parsed.saved_at = json.value("saved_at", parsed.updated_at);
        if (json.contains("deleted_at") && !json["deleted_at"].is_null()) parsed.deleted_at = json["deleted_at"].get<std::string>();
        parsed.revision = json.value("revision", uint64_t{0});
        parsed.modified_by_device_id = json.value("modified_by_device_id", "");
        for (const auto& item : json.at("blocks")) {
            NoteBlock block;
            block.id = item.at("id").get<std::string>();
            if (!ParseBlockType(item.value("type", ""), block.type)) { error = "Unknown note block type."; return false; }
            block.indent = item.value("indent", 0);
            block.checked = item.value("checked", false);
            for (const auto& run_json : item.at("runs")) {
                block.runs.push_back({run_json.value("text", ""), MarksFromJson(run_json.value("marks", Json::array()))});
            }
            if (block.runs.empty()) block.runs.push_back({"", 0});
            parsed.blocks.push_back(std::move(block));
        }
        if (parsed.blocks.empty()) parsed.blocks.push_back({GenerateUuid(), NoteBlockType::Paragraph, 0, false, {{"", 0}}});
        note = std::move(parsed);
        return true;
    } catch (const std::exception& exception) {
        error = std::string("Invalid note JSON: ") + exception.what();
        return false;
    }
}

bool NoteSerializer::SaveSections(const std::vector<NoteSection>& sections, const std::filesystem::path& path, std::string& error) {
    Json json = {{"format", "textlt-note-sections"}, {"schema_version", 1}, {"sections", Json::array()}};
    for (const auto& section : sections) {
        json["sections"].push_back({{"id", section.id}, {"name", section.name}, {"created_at", section.created_at}, {"updated_at", section.updated_at}});
    }
    return AtomicWrite(path, json.dump(2) + "\n", error);
}

bool NoteSerializer::LoadSections(const std::filesystem::path& path, std::vector<NoteSection>& sections, std::string& error) {
    if (!std::filesystem::exists(path)) { sections.clear(); return true; }
    try {
        std::ifstream input(path, std::ios::binary); Json json; input >> json;
        if (json.value("format", "") != "textlt-note-sections") { error = "Unsupported sections format."; return false; }
        std::vector<NoteSection> parsed;
        for (const auto& item : json.value("sections", Json::array())) {
            parsed.push_back({item.at("id").get<std::string>(), item.at("name").get<std::string>(), item.at("created_at").get<std::string>(), item.at("updated_at").get<std::string>()});
        }
        sections = std::move(parsed); return true;
    } catch (const std::exception& exception) { error = std::string("Invalid sections JSON: ") + exception.what(); return false; }
}

} // namespace textlt::notes
