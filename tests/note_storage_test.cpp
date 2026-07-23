#include "notes/note_repository.hpp"
#include "notes/note_serializer.hpp"

#include <cassert>
#include <filesystem>
#include <string>

int main() {
    namespace notes = textlt::notes;
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / ("textlt-note-test-" + notes::GenerateUuid());

    notes::NoteRepository repository(root);
    std::string warning;
    assert(repository.Load(warning));
    assert(repository.Notes().empty());
    assert(!repository.DeviceId().empty());

    notes::NoteSection& work = repository.CreateSection("Work");
    const std::string work_id = work.id;
    std::string error;
    assert(repository.SaveSections(error));

    notes::NoteDocument note = notes::MakeNewNote(repository.DeviceId());
    note.title = "Release plan";
    note.section_id = work_id;
    note.pinned = true;
    note.pinned_at = notes::UtcNow();
    note.blocks = {
        {notes::GenerateUuid(), notes::NoteBlockType::Paragraph, 0, false,
            {{"Important", notes::MarkBit(notes::NoteMark::Bold)}, {" text", 0}}},
        {notes::GenerateUuid(), notes::NoteBlockType::CheckItem, 1, true,
            {{"Run tests", notes::MarkBit(notes::NoteMark::Italic)}},},
    };
    repository.Notes().push_back(note);
    assert(repository.Save(repository.Notes().back(), error));

    notes::NoteRepository reloaded(root);
    assert(reloaded.Load(warning));
    assert(reloaded.Sections().size() == 1);
    assert(reloaded.Sections().front().name == "Work");
    assert(reloaded.Notes().size() == 1);
    const notes::NoteDocument& loaded = reloaded.Notes().front();
    assert(loaded.id == note.id);
    assert(loaded.title == "Release plan");
    assert(loaded.pinned);
    assert(loaded.section_id && *loaded.section_id == work_id);
    assert(loaded.blocks.size() == 2);
    assert(loaded.blocks[0].runs[0].marks == notes::MarkBit(notes::NoteMark::Bold));
    assert(loaded.blocks[1].type == notes::NoteBlockType::CheckItem);
    assert(loaded.blocks[1].checked);

    assert(reloaded.DeleteSection(work_id, error));
    assert(!reloaded.Notes().front().section_id);
    assert(reloaded.Sections().front().deleted_at.has_value());
    assert(reloaded.SaveSections(error));
    assert(reloaded.MoveToTrash(reloaded.Notes().front(), error));
    assert(reloaded.Notes().front().deleted_at.has_value());
    assert(reloaded.Restore(reloaded.Notes().front(), error));
    assert(!reloaded.Notes().front().deleted_at.has_value());

    notes::NoteRepository after_delete(root);
    assert(after_delete.Load(warning));
    assert(after_delete.Sections().front().deleted_at.has_value());

    std::filesystem::remove_all(root);
    return 0;
}
