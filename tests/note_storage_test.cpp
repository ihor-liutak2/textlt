#include "notes/note_repository.hpp"
#include "notes/note_serializer.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

textlt::notes::NoteSection* FindSection(
    textlt::notes::NoteRepository& repository,
    const std::string& id) {
    for (auto& section : repository.Sections()) {
        if (section.id == id) return &section;
    }
    return nullptr;
}

textlt::notes::NoteDocument* FindNote(
    textlt::notes::NoteRepository& repository,
    const std::string& id) {
    for (auto& note : repository.Notes()) {
        if (note.id == id) return &note;
    }
    return nullptr;
}

} // namespace

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

    // Project groups are identified by project_id, never by name.
    notes::NoteRepository projects(root);
    assert(projects.Load(warning));
    notes::NoteSection* alpha = projects.EnsureProjectSection("project-alpha", "Alpha", error);
    assert(alpha != nullptr);
    const std::string alpha_id = alpha->id;
    assert(alpha->project_id && *alpha->project_id == "project-alpha");
    assert(alpha->auto_named);
    notes::NoteSection* alpha_again = projects.EnsureProjectSection("project-alpha", "Alpha", error);
    assert(alpha_again != nullptr);
    assert(alpha_again->id == alpha_id);

    // A name conflict gets a deterministic suffix instead of adopting an
    // unrelated group.
    notes::NoteSection* beta = projects.EnsureProjectSection("project-beta", "Alpha", error);
    assert(beta != nullptr);
    const std::string beta_id = beta->id;
    assert(beta->name == "Alpha (2)");

    // Project metadata survives a reload.
    notes::NoteRepository projects_reloaded(root);
    assert(projects_reloaded.Load(warning));
    const notes::NoteSection* loaded_alpha = projects_reloaded.FindProjectSection("project-alpha");
    assert(loaded_alpha != nullptr);
    assert(loaded_alpha->id == alpha_id);
    assert(loaded_alpha->auto_named);
    assert(projects_reloaded.FindProjectSection("project-beta")->name == "Alpha (2)");

    // Managed groups cannot be renamed or deleted through the General CRUD.
    assert(!projects_reloaded.RenameSection(alpha_id, "Renamed", error));
    assert(!projects_reloaded.DeleteSection(alpha_id, error));
    assert(projects_reloaded.FindProjectSection("project-alpha") != nullptr);

    // An orphaned group can be converted to a General group without losing
    // its notes.
    notes::NoteDocument orphan_note = notes::MakeNewNote(projects_reloaded.DeviceId());
    orphan_note.title = "Orphan note";
    orphan_note.section_id = beta_id;
    projects_reloaded.Notes().push_back(orphan_note);
    assert(projects_reloaded.Save(projects_reloaded.Notes().back(), error));
    assert(projects_reloaded.ConvertSectionToGeneral(beta_id, error));
    notes::NoteSection* converted = FindSection(projects_reloaded, beta_id);
    assert(converted != nullptr);
    assert(!converted->project_id);
    assert(!converted->auto_named);
    const notes::NoteDocument* converted_note =
        FindNote(projects_reloaded, orphan_note.id);
    assert(converted_note != nullptr);
    assert(converted_note->section_id && *converted_note->section_id == beta_id);

    // Relinking an orphaned group into a project that already has a group
    // moves the notes instead of dropping them.
    notes::NoteSection* orphan = projects_reloaded.EnsureProjectSection("project-gone", "Gone", error);
    assert(orphan != nullptr);
    const std::string orphan_id = orphan->id;
    notes::NoteDocument relink_note = notes::MakeNewNote(projects_reloaded.DeviceId());
    relink_note.title = "Relink note";
    relink_note.section_id = orphan_id;
    projects_reloaded.Notes().push_back(relink_note);
    assert(projects_reloaded.Save(projects_reloaded.Notes().back(), error));
    std::string linked_id;
    assert(projects_reloaded.RelinkSectionToProject(
        orphan_id, "project-alpha", "Alpha", linked_id, error));
    assert(linked_id == alpha_id);
    assert(FindSection(projects_reloaded, orphan_id)->deleted_at.has_value());
    const notes::NoteDocument* relinked_note =
        FindNote(projects_reloaded, relink_note.id);
    assert(relinked_note != nullptr);
    assert(relinked_note->section_id && *relinked_note->section_id == alpha_id);

    // Relinking to a project without an existing group claims the orphan and
    // gives it the project name.
    notes::NoteSection* claim =
        projects_reloaded.EnsureProjectSection("project-orphan", "Orphan", error);
    assert(claim != nullptr);
    const std::string claim_id = claim->id;
    std::string claimed_id;
    assert(projects_reloaded.RelinkSectionToProject(
        claim_id, "project-empty", "Fresh", claimed_id, error));
    assert(claimed_id == claim_id);
    notes::NoteSection* claimed = FindSection(projects_reloaded, claim_id);
    assert(claimed != nullptr);
    assert(claimed->project_id && *claimed->project_id == "project-empty");
    assert(claimed->auto_named);
    assert(claimed->name == "Fresh");

    // Relinking can free the original project name: the orphan that blocked it
    // is merged away, so the managed group drops its suffix.
    notes::NoteSection* old_project =
        projects_reloaded.EnsureProjectSection("project-old", "pr", error);
    assert(old_project != nullptr);
    const std::string old_project_id = old_project->id;
    notes::NoteSection* new_project =
        projects_reloaded.EnsureProjectSection("project-new", "pr", error);
    assert(new_project != nullptr);
    const std::string new_project_id = new_project->id;
    assert(new_project->name == "pr (2)");
    std::string normalized_id;
    assert(projects_reloaded.RelinkSectionToProject(
        old_project_id, "project-new", "pr", normalized_id, error));
    assert(normalized_id == new_project_id);
    assert(FindSection(projects_reloaded, old_project_id)->deleted_at.has_value());
    assert(FindSection(projects_reloaded, new_project_id)->name == "pr");

    // A schema v1 sections file still loads; its groups become General groups.
    const std::filesystem::path legacy_root =
        std::filesystem::temp_directory_path() / ("textlt-note-legacy-" + notes::GenerateUuid());
    std::filesystem::create_directories(legacy_root);
    {
        std::ofstream legacy_file(legacy_root / "sections.json", std::ios::binary | std::ios::trunc);
        legacy_file << R"({"format":"textlt-note-sections","schema_version":1,"sections":[)"
                    << R"({"id":"legacy-group","name":"Legacy","created_at":"2024-01-01T00:00:00Z",)"
                    << R"("updated_at":"2024-01-01T00:00:00Z","deleted_at":null}]})" << "\n";
    }
    notes::NoteRepository legacy(legacy_root);
    assert(legacy.Load(warning));
    assert(legacy.Sections().size() == 1);
    assert(legacy.Sections().front().name == "Legacy");
    assert(!legacy.Sections().front().project_id);
    assert(!legacy.Sections().front().auto_named);
    assert(legacy.SaveSections(error));
    notes::NoteRepository legacy_reloaded(legacy_root);
    assert(legacy_reloaded.Load(warning));
    assert(legacy_reloaded.Sections().size() == 1);
    assert(!legacy_reloaded.Sections().front().project_id);
    std::filesystem::remove_all(legacy_root);

    std::filesystem::remove_all(root);
    return 0;
}
