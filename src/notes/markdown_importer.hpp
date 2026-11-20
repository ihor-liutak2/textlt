#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "notes/note_document.hpp"

namespace textlt::notes {

bool ReadMarkdownNote(const std::filesystem::path& path,
    std::vector<NoteBlock>& blocks, std::string& error);

} // namespace textlt::notes
