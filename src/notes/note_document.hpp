#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace textlt::notes {

enum class NoteBlockType {
    Paragraph,
    BulletItem,
    NumberedItem,
    CheckItem,
};

struct NoteRun {
    std::string text;
};

struct NoteBlock {
    std::string id;
    NoteBlockType type = NoteBlockType::Paragraph;
    int indent = 0;
    bool checked = false;
    std::vector<NoteRun> runs;
    // 0 = normal paragraph, 1..3 = heading level. Headings never carry a list
    // type; setting one clears the other.
    int heading = 0;
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
    // When set, this group is owned by a project. Ownership lives on the
    // group, not on individual notes, so moving a note between groups is the
    // only way to change its project association.
    std::optional<std::string> project_id;
    // True when the group name was generated from the project name and may be
    // updated automatically. Renaming is enabled after the group is converted
    // to a General group.
    bool auto_named = false;

    bool IsProjectGroup() const { return project_id.has_value(); }
};

std::string GenerateUuid();
std::string UtcNow();
NoteDocument MakeNewNote(const std::string& device_id);
std::string BlockText(const NoteBlock& block);
std::string PlainText(const NoteDocument& note);
std::string BlockTypeName(NoteBlockType type);
bool ParseBlockType(const std::string& name, NoteBlockType& type);

} // namespace textlt::notes
