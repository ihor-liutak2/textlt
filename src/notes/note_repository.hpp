#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "notes/note_document.hpp"

namespace textlt::notes {

class NoteRepository {
public:
    explicit NoteRepository(std::filesystem::path root = {});
    bool Load(std::string& warning);
    bool Save(NoteDocument& note, std::string& error);
    bool SaveSections(std::string& error) const;
    bool MoveToTrash(NoteDocument& note, std::string& error);
    bool Restore(NoteDocument& note, std::string& error);
    NoteSection& CreateSection(const std::string& name);
    bool RenameSection(const std::string& id, const std::string& name, std::string& error);
    bool DeleteSection(const std::string& id, std::string& error);
    const std::filesystem::path& Root() const { return root_; }
    const std::string& DeviceId() const { return device_id_; }
    std::vector<NoteDocument>& Notes() { return notes_; }
    const std::vector<NoteDocument>& Notes() const { return notes_; }
    std::vector<NoteSection>& Sections() { return sections_; }
    const std::vector<NoteSection>& Sections() const { return sections_; }
    static std::filesystem::path DefaultRoot();

private:
    bool LoadOrCreateDeviceId(std::string& error);
    std::filesystem::path NotePath(const std::string& id) const;
    std::filesystem::path root_;
    std::string device_id_;
    std::vector<NoteDocument> notes_;
    std::vector<NoteSection> sections_;
};

} // namespace textlt::notes
