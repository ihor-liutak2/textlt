#include "notes/note_document.hpp"

#include <array>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <random>
#include <sstream>

namespace textlt::notes {

std::string GenerateUuid() {
    static thread_local std::mt19937_64 generator(std::random_device{}());
    std::uniform_int_distribution<uint32_t> distribution(0, 255);
    std::array<uint8_t, 16> bytes{};
    for (auto& value : bytes) {
        value = static_cast<uint8_t>(distribution(generator));
    }
    bytes[6] = static_cast<uint8_t>((bytes[6] & 0x0f) | 0x40);
    bytes[8] = static_cast<uint8_t>((bytes[8] & 0x3f) | 0x80);

    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (size_t index = 0; index < bytes.size(); ++index) {
        if (index == 4 || index == 6 || index == 8 || index == 10) {
            out << '-';
        }
        out << std::setw(2) << static_cast<int>(bytes[index]);
    }
    return out.str();
}

std::string UtcNow() {
    const auto now = std::chrono::system_clock::now();
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &time);
#else
    gmtime_r(&time, &utc);
#endif
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S")
        << '.' << std::setw(3) << std::setfill('0') << milliseconds.count() << 'Z';
    return out.str();
}

NoteDocument MakeNewNote(const std::string& device_id) {
    NoteDocument note;
    note.id = GenerateUuid();
    note.created_at = UtcNow();
    note.updated_at = note.created_at;
    note.saved_at = note.created_at;
    note.modified_by_device_id = device_id;
    note.blocks.push_back({GenerateUuid(), NoteBlockType::Paragraph, 0, false, {{"", 0}}});
    return note;
}

std::string BlockText(const NoteBlock& block) {
    std::string result;
    for (const auto& run : block.runs) {
        result += run.text;
    }
    return result;
}

std::string PlainText(const NoteDocument& note) {
    std::string result;
    for (size_t index = 0; index < note.blocks.size(); ++index) {
        if (index > 0) {
            result += '\n';
        }
        result += BlockText(note.blocks[index]);
    }
    return result;
}

std::string BlockTypeName(NoteBlockType type) {
    switch (type) {
        case NoteBlockType::Paragraph: return "paragraph";
        case NoteBlockType::BulletItem: return "bullet_item";
        case NoteBlockType::NumberedItem: return "numbered_item";
        case NoteBlockType::CheckItem: return "check_item";
    }
    return "paragraph";
}

bool ParseBlockType(const std::string& name, NoteBlockType& type) {
    if (name == "paragraph") type = NoteBlockType::Paragraph;
    else if (name == "bullet_item") type = NoteBlockType::BulletItem;
    else if (name == "numbered_item") type = NoteBlockType::NumberedItem;
    else if (name == "check_item") type = NoteBlockType::CheckItem;
    else return false;
    return true;
}

std::string MarkName(NoteMark mark) {
    switch (mark) {
        case NoteMark::Bold: return "bold";
        case NoteMark::Italic: return "italic";
        case NoteMark::Underlined: return "underlined";
        case NoteMark::Strikethrough: return "strikethrough";
    }
    return {};
}

} // namespace textlt::notes
