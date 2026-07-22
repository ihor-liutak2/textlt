#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "notes/note_document.hpp"

namespace textlt::notes {

class NoteSerializer {
public:
    static bool SaveNote(const NoteDocument& note, const std::filesystem::path& path, std::string& error);
    static bool LoadNote(const std::filesystem::path& path, NoteDocument& note, std::string& error);
    static bool SaveSections(const std::vector<NoteSection>& sections, const std::filesystem::path& path, std::string& error);
    static bool LoadSections(const std::filesystem::path& path, std::vector<NoteSection>& sections, std::string& error);
};

} // namespace textlt::notes
