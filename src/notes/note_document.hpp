#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace textlt::notes {

enum class NoteMark : uint8_t {
    Bold = 1,
    Italic = 2,
    Underlined = 4,
    Strikethrough = 8,
};

enum class NoteBlockType {
    Paragraph,
    BulletItem,
    NumberedItem,
    CheckItem,
};

struct NoteRun {
    std::string text;
    uint8_t marks = 0;
};

struct NoteBlock {
    std::string id;
    NoteBlockType type = NoteBlockType::Paragraph;
    int indent = 0;
    bool checked = false;
    std::vector<NoteRun> runs;
};

struct NoteDocument {
    int schema_version = 1;
    std::string id;
    std::string title;
    std::optional<std::string> section_id;
    bool pinned = false;
    std::optional<std::string> pinned_at;
    std::string created_at;
    std::string updated_at;
    std::string saved_at;
    std::optional<std::string> deleted_at;
    uint64_t revision = 0;
    std::string modified_by_device_id;
    std::vector<NoteBlock> blocks;
};

struct NoteSection {
    std::string id;
    std::string name;
    std::string created_at;
    std::string updated_at;
    std::optional<std::string> deleted_at;
};

std::string GenerateUuid();
std::string UtcNow();
NoteDocument MakeNewNote(const std::string& device_id);
std::string BlockText(const NoteBlock& block);
std::string PlainText(const NoteDocument& note);
std::string BlockTypeName(NoteBlockType type);
bool ParseBlockType(const std::string& name, NoteBlockType& type);
std::string MarkName(NoteMark mark);

inline uint8_t MarkBit(NoteMark mark) {
    return static_cast<uint8_t>(mark);
}

} // namespace textlt::notes
