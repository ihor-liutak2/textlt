#include "notes/note_repository.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <system_error>

#include "notes/note_serializer.hpp"

namespace textlt::notes {

NoteRepository::NoteRepository(std::filesystem::path root)
    : root_(root.empty() ? DefaultRoot() : std::move(root)) {}

std::filesystem::path NoteRepository::DefaultRoot() {
#ifdef _WIN32
    if (const char* value = std::getenv("LOCALAPPDATA")) return std::filesystem::path(value) / "textlt" / "notes";
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME")) return std::filesystem::path(home) / "Library" / "Application Support" / "textlt" / "notes";
#else
    if (const char* value = std::getenv("XDG_DATA_HOME")) return std::filesystem::path(value) / "textlt" / "notes";
    if (const char* home = std::getenv("HOME")) return std::filesystem::path(home) / ".local" / "share" / "textlt" / "notes";
#endif
    return std::filesystem::current_path() / ".textlt" / "notes";
}

bool NoteRepository::LoadOrCreateDeviceId(std::string& error) {
    const auto path = root_ / "device_id";
    if (std::filesystem::exists(path)) {
        std::ifstream input(path); std::getline(input, device_id_);
        if (!device_id_.empty()) return true;
    }
    std::error_code ec; std::filesystem::create_directories(root_, ec);
    if (ec) { error = ec.message(); return false; }
    device_id_ = GenerateUuid();
    std::ofstream output(path, std::ios::trunc); output << device_id_ << '\n';
    if (!output) { error = "Could not persist notes device ID."; return false; }
    return true;
}

bool NoteRepository::Load(std::string& warning) {
    notes_.clear(); sections_.clear(); warning.clear();
    std::string error;
    if (!LoadOrCreateDeviceId(error)) { warning = error; return false; }
    if (!NoteSerializer::LoadSections(root_ / "sections.json", sections_, error)) warning = error;
    const auto items = root_ / "items";
    std::error_code ec; std::filesystem::create_directories(items, ec);
    if (ec) { warning = ec.message(); return false; }
    for (const auto& entry : std::filesystem::directory_iterator(items, ec)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".tlnote") continue;
        NoteDocument note; std::string note_error;
        if (NoteSerializer::LoadNote(entry.path(), note, note_error)) notes_.push_back(std::move(note));
        else if (warning.empty()) warning = entry.path().filename().string() + ": " + note_error;
    }
    std::sort(notes_.begin(), notes_.end(), [](const auto& left, const auto& right) { return left.updated_at > right.updated_at; });
    return true;
}

std::filesystem::path NoteRepository::NotePath(const std::string& id) const { return root_ / "items" / (id + ".tlnote"); }

bool NoteRepository::Save(NoteDocument& note, std::string& error) {
    const std::string saved = UtcNow();
    note.modified_by_device_id = device_id_;
    NoteDocument copy = note; copy.saved_at = saved;
    if (!NoteSerializer::SaveNote(copy, NotePath(note.id), error)) return false;
    note.saved_at = saved; return true;
}

bool NoteRepository::SaveSections(std::string& error) const { return NoteSerializer::SaveSections(sections_, root_ / "sections.json", error); }

bool NoteRepository::MoveToTrash(NoteDocument& note, std::string& error) { note.deleted_at = UtcNow(); note.updated_at = *note.deleted_at; ++note.revision; return Save(note, error); }
bool NoteRepository::Restore(NoteDocument& note, std::string& error) { note.deleted_at.reset(); note.updated_at = UtcNow(); ++note.revision; return Save(note, error); }

NoteSection& NoteRepository::CreateSection(const std::string& name) {
    const std::string now = UtcNow();
    sections_.push_back({GenerateUuid(), name, now, now, std::nullopt});
    return sections_.back();
}

bool NoteRepository::RenameSection(const std::string& id, const std::string& name, std::string& error) {
    auto found = std::find_if(sections_.begin(), sections_.end(), [&](const auto& value) { return value.id == id; });
    if (found == sections_.end() || found->deleted_at) { error = "Section was not found."; return false; }
    found->name = name; found->updated_at = UtcNow(); return SaveSections(error);
}

bool NoteRepository::DeleteSection(const std::string& id, std::string& error) {
    for (auto& note : notes_) if (note.section_id && *note.section_id == id) { note.section_id.reset(); note.updated_at = UtcNow(); ++note.revision; if (!Save(note, error)) return false; }
    auto found = std::find_if(
        sections_.begin(), sections_.end(),
        [&](const NoteSection& section) { return section.id == id; });
    if (found == sections_.end()) {
        error = "Section was not found.";
        return false;
    }
    found->deleted_at = UtcNow();
    found->updated_at = *found->deleted_at;
    return SaveSections(error);
}

} // namespace textlt::notes
