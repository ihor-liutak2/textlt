#include "notes/notes_workspace_component.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <regex>
#include <utility>

#include "editor_utils.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/screen/string.hpp"
#include "ftxui/screen/terminal.hpp"
#include "keyboard_shortcuts.hpp"
#include "modals/delete_confirmation_popup.hpp"
#include "ui_button.hpp"
#include "workspace_tabs.hpp"

namespace textlt::notes {
namespace {

constexpr int kMouseWheelScrollRows = 3;
constexpr size_t kEditorScrollbarColumns = 2;
constexpr int kSidebarWidth = 28;
constexpr int kSidebarGap = 2;

// Breathing room between the editor border and the note text.
constexpr size_t kEditorTopPadding = 1;
constexpr size_t kEditorLeftPadding = 3;
// Total gap between the text and the right border; the scrollbar occupies the
// last kEditorScrollbarColumns of it.
constexpr size_t kEditorRightPadding = 6;
// Note titles are clamped to this many display columns.
constexpr size_t kNoteTitleMaxChars = 20;

int ToolbarItemWidth(const std::string& label) {
    return ftxui::string_width(" " + label + " ");
}

std::string TrimWhitespace(const std::string& value) {
    size_t begin = 0;
    while (begin < value.size() &&
        std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }
    size_t end = value.size();
    while (end > begin &&
        std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(begin, end - begin);
}

struct EditorScrollMetrics {
    size_t viewport_rows = 0;
    size_t page_rows = 0;
    size_t total_rows = 0;
    size_t max_scroll = 0;
    size_t thumb_height = 0;
    size_t track_space = 0;
    bool needs_scrollbar = false;
};

EditorScrollMetrics ComputeEditorScrollMetrics(
    size_t viewport_rows, size_t page_rows, size_t total_rows) {
    EditorScrollMetrics metrics;
    metrics.viewport_rows = std::max<size_t>(1, viewport_rows);
    metrics.page_rows = std::max<size_t>(1, page_rows);
    metrics.total_rows = total_rows;
    metrics.needs_scrollbar = total_rows > metrics.page_rows;
    metrics.max_scroll = metrics.needs_scrollbar
        ? total_rows - metrics.page_rows : 0;
    if (!metrics.needs_scrollbar) {
        metrics.thumb_height = metrics.viewport_rows;
        return metrics;
    }

    const size_t min_thumb = metrics.viewport_rows >= 2 ? 2 : 1;
    const size_t proportional =
        (metrics.viewport_rows * metrics.page_rows) /
        std::max<size_t>(1, total_rows);
    metrics.thumb_height = std::min(
        metrics.viewport_rows, std::max(min_thumb, proportional));
    metrics.track_space = metrics.viewport_rows > metrics.thumb_height
        ? metrics.viewport_rows - metrics.thumb_height : 0;
    return metrics;
}

bool IsCtrl(const ftxui::Event& event, char key) {
    return MatchesShortcut(event, ShortcutModifier::Ctrl, key);
}

bool IsBackspace(const ftxui::Event& event) { return event == ftxui::Event::Backspace || event.input() == "\x7f" || event.input() == "\x08"; }
bool IsDelete(const ftxui::Event& event) { return event == ftxui::Event::Delete || event.input() == "\x1B[3~"; }
bool IsShift(const ftxui::Event& event, char direction) {
    const std::string input = event.input();
    return input == std::string("Shift+") + direction ||
        (direction == 'L' && (input == "\x1B[1;2D" || input == "\x1B[68;2u")) ||
        (direction == 'R' && (input == "\x1B[1;2C" || input == "\x1B[67;2u")) ||
        (direction == 'U' && (input == "\x1B[1;2A" || input == "\x1B[65;2u")) ||
        (direction == 'D' && (input == "\x1B[1;2B" || input == "\x1B[66;2u"));
}

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

// Truncates text to a display-column budget without splitting UTF-8
// codepoints or wide glyphs.
std::string FitText(const std::string& value, size_t max_columns) {
    if (max_columns == 0) return {};
    if (textlt::utils::Utf8DisplayWidth(value) <= max_columns) return value;
    if (max_columns <= 3) {
        const size_t end = textlt::utils::Utf8ByteIndexAtDisplayColumn(
            value, 0, max_columns);
        return value.substr(0, end);
    }
    const size_t end = textlt::utils::Utf8ByteIndexAtDisplayColumn(
        value, 0, max_columns - 3);
    return value.substr(0, end) + "...";
}

} // namespace

NotesWorkspaceComponent::NotesWorkspaceComponent(
    const Theme* theme,
    StatusCallback on_status,
    DocumentsCallback on_documents,
    ActionsCallback on_actions,
    ClipboardWriteCallback write_clipboard,
    std::filesystem::path root)
    : theme_(theme),
      on_status_(std::move(on_status)),
      on_documents_(std::move(on_documents)),
      on_actions_(std::move(on_actions)),
      write_clipboard_(std::move(write_clipboard)),
      repository_(std::move(root)),
      session_(nullptr),
      context_menu_(theme) {
    repository_.Load(load_warning_); RebuildSidebar(); RebuildVisibleNotes();
}

void NotesWorkspaceComponent::Open() { TakeFocus(); if (!load_warning_.empty()) Notify("Notes warning: " + load_warning_); }

void NotesWorkspaceComponent::SetSidebarVisible(bool visible) {
    if (sidebar_visible_ == visible) return;
    sidebar_visible_ = visible;
    if (visible) return;

    if (focus_ == FocusArea::Sidebar || focus_ == FocusArea::GroupName) {
        focus_ = editing_ ? FocusArea::Body : FocusArea::Cards;
    }

    // Clear hitboxes from the previous render so clicks in the space where the
    // hidden sidebar used to be do not trigger stale sidebar controls.
    const ftxui::Box empty_box{1, 0, 1, 0};
    sidebar_box_ = empty_box;
    documents_tab_box_ = empty_box;
    notes_tab_box_ = empty_box;
    actions_tab_box_ = empty_box;
    sidebar_notes_tab_box_ = empty_box;
    sidebar_content_tab_box_ = empty_box;
    sidebar_content_box_ = empty_box;
    footer_new_note_box_ = empty_box;
    footer_delete_note_box_ = empty_box;
    footer_restore_note_box_ = empty_box;
    footer_permanent_delete_box_ = empty_box;
    footer_empty_trash_box_ = empty_box;
    footer_new_group_box_ = empty_box;
    footer_rename_group_box_ = empty_box;
    footer_delete_group_box_ = empty_box;
    footer_project_notes_box_ = empty_box;
    footer_orphan_general_box_ = empty_box;
    footer_orphan_relink_box_ = empty_box;
    sidebar_row_boxes_.clear();
}

void NotesWorkspaceComponent::SetEditorColumns(int columns) {
    const int clamped = std::clamp(columns, 1, 3);
    if (editor_columns_ == clamped) return;
    context_menu_.Close();
    editor_columns_ = clamped;
    has_last_editor_cursor_ = false;
    editor_column_boxes_.clear();
    if (on_columns_changed_) on_columns_changed_(clamped);
}

void NotesWorkspaceComponent::FocusSidebar() {
    if (!sidebar_visible_) return;
    sidebar_content_selected_ = false;
    focus_ = FocusArea::Sidebar;
}

void NotesWorkspaceComponent::FocusMainArea() {
    if (focus_ == FocusArea::Sidebar || focus_ == FocusArea::GroupName) {
        focus_ = editing_ ? FocusArea::Body : FocusArea::Cards;
    }
}

void NotesWorkspaceComponent::SetProjectContext(
    std::optional<ProjectContext> project,
    std::vector<std::string> known_project_ids,
    bool project_registry_usable) {
    const bool had_project = project_context_.has_value();
    const std::string previous_project_id =
        project_context_ ? project_context_->id : std::string{};
    const bool active_project_changed = had_project != project.has_value() ||
        (had_project && project.has_value() && previous_project_id != project->id);

    known_project_ids_ = std::move(known_project_ids);
    project_registry_usable_ = project_registry_usable;
    project_context_ = std::move(project);
    active_project_section_id_.reset();
    if (project_context_) {
        std::string error;
        if (NoteSection* section = repository_.EnsureProjectSection(
                project_context_->id, project_context_->name, error)) {
            active_project_section_id_ = section->id;
        } else if (!error.empty()) {
            Notify("Project notes unavailable: " + error);
        }
    }
    RebuildSidebar();
    if (editing_) return;
    if (project_context_ && active_project_section_id_ && active_project_changed) {
        SelectSectionById(*active_project_section_id_);
    } else if (!project_context_ && had_project) {
        // A non-project folder has no Projects footer; return to the
        // repository-wide Home view rather than leaving a stale project
        // filter active.
        SelectSidebarEntry(0);
    }
}

bool NotesWorkspaceComponent::UpdateProjectGroupName(
    const ProjectContext& project, std::string& error) {
    const NoteSection* section = repository_.FindProjectSection(project.id);
    const std::string previous_name = section ? section->name : std::string{};
    if (!repository_.UpdateProjectSectionName(project.id, project.name, error)) return false;
    if (project_context_ && project_context_->id == project.id) {
        project_context_->name = project.name;
    }
    if (section && section->name != previous_name) RebuildSidebar();
    return true;
}

bool NotesWorkspaceComponent::DetachProjectGroup(
    const std::string& project_id, std::string& error) {
    error.clear();
    const NoteSection* section = repository_.FindProjectSection(project_id);
    if (!section) return true;
    if (!repository_.ConvertSectionToGeneral(section->id, error)) return false;
    if (active_project_section_id_ == section->id) active_project_section_id_.reset();
    RebuildSidebar();
    return true;
}

void NotesWorkspaceComponent::Notify(const std::string& message) { save_status_ = message; if (on_status_) on_status_(message); }

uint64_t NotesWorkspaceComponent::NoteRevision() const {
    const auto* note = session_.Note();
    return note ? note->revision : 0;
}

std::vector<NotesWorkspaceComponent::NoteSearchResult>
NotesWorkspaceComponent::SearchNote(const std::string& query,
    bool match_case, bool whole_word, size_t limit, bool& truncated) {
    truncated = false;
    note_search_matches_.clear();
    note_search_active_ = 0;
    std::vector<NoteSearchResult> results;
    auto* note = session_.Note();
    if (!editing_ || !note || query.empty()) return results;

    const std::string needle = match_case ? query : Lower(query);
    for (size_t block_index = 0; block_index < note->blocks.size(); ++block_index) {
        const NoteBlock& block = note->blocks[block_index];
        const std::string text = BlockText(block);
        if (text.empty()) continue;
        const std::string searchable = match_case ? text : Lower(text);
        size_t position = searchable.find(needle);
        while (position != std::string::npos) {
            const bool word_before = position > 0 &&
                textlt::utils::IsWordCharacter(text[position - 1]);
            const size_t after = position + needle.size();
            const bool word_after = after < text.size() &&
                textlt::utils::IsWordCharacter(text[after]);
            if (!whole_word || (!word_before && !word_after)) {
                NoteSearchResult match;
                match.block = block_index;
                match.byte = position;
                match.length = needle.size();
                match.preview = text;
                match.preview_match_start = position;
                match.preview_match_length = needle.size();
                note_search_matches_.push_back(match);
                if (results.size() < limit) {
                    results.push_back(std::move(match));
                } else {
                    truncated = true;
                }
            }
            position = searchable.find(
                needle, position + std::max<size_t>(1, needle.size()));
        }
    }
    return results;
}

void NotesWorkspaceComponent::ActivateNoteSearchResult(size_t index) {
    auto* note = session_.Note();
    if (!editing_ || !note || index >= note_search_matches_.size()) return;
    const NoteSearchResult& match = note_search_matches_[index];
    if (match.block >= note->blocks.size()) return;
    note_search_active_ = index;
    JumpToNotePosition({match.block, match.byte});
}

void NotesWorkspaceComponent::JumpToNotePosition(NotePosition position) {
    focus_ = FocusArea::Body;
    session_.SetCursor(position, false);
    has_last_editor_cursor_ = false;
}

bool NotesWorkspaceComponent::ReplaceNoteSearchResult(size_t index,
    const std::string& replacement, std::string& error) {
    auto* note = session_.Note();
    if (!editing_ || !note) {
        error = "Open a note in edit mode to replace text";
        return false;
    }
    if (index >= note_search_matches_.size()) {
        error = "No match to replace";
        return false;
    }
    const NoteSearchResult match = note_search_matches_[index];
    if (!session_.ReplaceTextAt(match.block, match.byte, match.length,
            replacement)) {
        error = "Could not replace the match";
        return false;
    }
    note_search_matches_.clear();
    note_search_active_ = 0;
    TouchAndSave();
    return true;
}

bool NotesWorkspaceComponent::ReplaceAllNoteText(const std::string& query,
    const std::string& replacement, bool match_case, bool whole_word,
    size_t& count, std::string& error) {
    count = 0;
    if (!editing_ || !session_.Note()) {
        error = "Open a note in edit mode to replace text";
        return false;
    }
    if (query.empty()) {
        error = "Enter text to replace";
        return false;
    }
    bool truncated = false;
    SearchNote(query, match_case, whole_word,
        static_cast<size_t>(-1), truncated);
    if (note_search_matches_.empty()) return true;
    std::vector<NoteTextRange> ranges;
    ranges.reserve(note_search_matches_.size());
    for (const auto& match : note_search_matches_) {
        ranges.push_back({match.block, match.byte, match.length});
    }
    if (!session_.ReplaceTextRanges(ranges, replacement)) {
        error = "Could not replace matches";
        return false;
    }
    count = ranges.size();
    note_search_matches_.clear();
    note_search_active_ = 0;
    TouchAndSave();
    return true;
}

void NotesWorkspaceComponent::ClearNoteSearchHighlights() {
    note_search_matches_.clear();
    note_search_active_ = 0;
}

std::optional<size_t> NotesWorkspaceComponent::ActiveNoteIndex() const {
    if (!active_note_id_) return std::nullopt;
    const auto& notes = repository_.Notes();
    for (size_t index = 0; index < notes.size(); ++index) if (notes[index].id == *active_note_id_) return index;
    return std::nullopt;
}

std::string NotesWorkspaceComponent::SectionName(const NoteDocument& note) const {
    if (!note.section_id) return "Home";
    for (const auto& section : repository_.Sections()) {
        if (!section.deleted_at && section.id == *note.section_id) return section.name;
    }
    return "Home";
}

void NotesWorkspaceComponent::RebuildSidebar() {
    std::optional<SidebarEntryKind> previous_kind;
    std::string previous_id;
    if (selected_sidebar_ >= 0 && selected_sidebar_ < static_cast<int>(sidebar_entries_.size())) {
        previous_kind = sidebar_entries_[selected_sidebar_].kind;
        previous_id = sidebar_entries_[selected_sidebar_].id;
    }

    sidebar_entries_.clear();
    size_t home_count = 0, trash_count = 0;
    for (const auto& note : repository_.Notes()) note.deleted_at ? ++trash_count : ++home_count;
    sidebar_entries_.push_back({SidebarEntryKind::Home, "", "Home", home_count});

    std::vector<const NoteSection*> project_sections;
    std::vector<const NoteSection*> general_sections;
    for (const auto& section : repository_.Sections()) {
        if (section.deleted_at) continue;
        (section.project_id ? project_sections : general_sections).push_back(&section);
    }
    // Keep the active project's group above every other project group. This
    // is a view rule and is deliberately not persisted.
    std::stable_sort(project_sections.begin(), project_sections.end(),
        [&](const NoteSection* left, const NoteSection* right) {
            const bool left_active = active_project_section_id_ &&
                left->id == *active_project_section_id_;
            const bool right_active = active_project_section_id_ &&
                right->id == *active_project_section_id_;
            return left_active && !right_active;
        });

    auto add_section = [&](const NoteSection& section) {
        size_t count = 0;
        for (const auto& note : repository_.Notes()) {
            if (!note.deleted_at && note.section_id && *note.section_id == section.id) ++count;
        }
        const bool project = section.project_id.has_value();
        const bool orphaned =
            project_registry_usable_ && project && !IsKnownProject(*section.project_id);
        sidebar_entries_.push_back(
            {SidebarEntryKind::Section, section.id, section.name, count, project, orphaned});
    };
    for (const NoteSection* section : project_sections) add_section(*section);
    for (const NoteSection* section : general_sections) add_section(*section);
    sidebar_entries_.push_back({SidebarEntryKind::Trash, "", "Trash", trash_count});

    bool restored = false;
    if (previous_kind) {
        for (size_t index = 0; index < sidebar_entries_.size(); ++index) {
            const SidebarEntry& entry = sidebar_entries_[index];
            if (entry.kind != *previous_kind) continue;
            if (*previous_kind == SidebarEntryKind::Section && entry.id != previous_id) continue;
            selected_sidebar_ = static_cast<int>(index);
            restored = true;
            break;
        }
    }
    if (!restored) selected_sidebar_ = 0;
    selected_sidebar_ = std::clamp(selected_sidebar_, 0, std::max(0, static_cast<int>(sidebar_entries_.size()) - 1));
}

bool NotesWorkspaceComponent::IsKnownProject(const std::string& id) const {
    return std::find(known_project_ids_.begin(), known_project_ids_.end(), id) != known_project_ids_.end();
}

bool NotesWorkspaceComponent::SelectSectionById(const std::string& id) {
    if (id.empty()) return false;
    for (size_t index = 0; index < sidebar_entries_.size(); ++index) {
        const SidebarEntry& entry = sidebar_entries_[index];
        if (entry.kind == SidebarEntryKind::Section && entry.id == id) {
            SelectSidebarEntry(static_cast<int>(index));
            return true;
        }
    }
    return false;
}

bool NotesWorkspaceComponent::NoteMatchesFilter(const NoteDocument& note) const {
    if (trash_selected_ != note.deleted_at.has_value()) return false;
    if (!trash_selected_ && !selected_section_id_.empty() && (!note.section_id || *note.section_id != selected_section_id_)) return false;
    if (!search_.empty()) {
        const std::string needle = Lower(search_); const std::string haystack = Lower(note.title + "\n" + PlainText(note));
        if (haystack.find(needle) == std::string::npos) return false;
    }
    return true;
}

void NotesWorkspaceComponent::RebuildVisibleNotes() {
    visible_notes_.clear(); const auto& notes = repository_.Notes();
    for (size_t index = 0; index < notes.size(); ++index) if (NoteMatchesFilter(notes[index])) visible_notes_.push_back(index);
    std::stable_sort(visible_notes_.begin(), visible_notes_.end(), [&](size_t left, size_t right) {
        switch (sort_mode_) {
            case SortMode::Created:
                return notes[left].created_at > notes[right].created_at;
            case SortMode::Title:
                return Lower(notes[left].title) < Lower(notes[right].title);
            case SortMode::Pinned:
                if (notes[left].pinned != notes[right].pinned) return notes[left].pinned > notes[right].pinned;
                return notes[left].updated_at > notes[right].updated_at;
            case SortMode::Updated:
                return notes[left].updated_at > notes[right].updated_at;
        }
        return false;
    });
    selected_card_ = std::clamp(selected_card_, 0, std::max(0, static_cast<int>(visible_notes_.size()) - 1));
}

void NotesWorkspaceComponent::SelectSidebarEntry(int index) {
    if (index < 0 || index >= static_cast<int>(sidebar_entries_.size())) return;
    selected_sidebar_ = index; const auto& entry = sidebar_entries_[index];
    if (entry.kind == SidebarEntryKind::NewSection) { StartCreateSection(); return; }
    selected_section_id_ = entry.kind == SidebarEntryKind::Section ? entry.id : "";
    trash_selected_ = entry.kind == SidebarEntryKind::Trash; selected_card_ = 0; RebuildVisibleNotes(); focus_ = FocusArea::Cards;
}

void NotesWorkspaceComponent::StartCreateSection() {
    editing_section_id_.reset();
    group_name_input_.clear();
    group_name_cursor_ = 0;
    focus_ = FocusArea::GroupName;
}

void NotesWorkspaceComponent::StartRenameSection() {
    if (selected_sidebar_ < 0 || selected_sidebar_ >= static_cast<int>(sidebar_entries_.size())) return;
    const SidebarEntry& entry = sidebar_entries_[selected_sidebar_];
    if (entry.kind != SidebarEntryKind::Section) return;
    if (entry.project) {
        Notify("Project groups cannot be renamed; keep the group as General first.");
        return;
    }
    editing_section_id_ = entry.id;
    group_name_input_ = entry.label;
    group_name_cursor_ = group_name_input_.size();
    focus_ = FocusArea::GroupName;
}

void NotesWorkspaceComponent::RequestDeleteSelectedSection() {
    if (selected_sidebar_ < 0 ||
        selected_sidebar_ >= static_cast<int>(sidebar_entries_.size())) return;
    const SidebarEntry& entry = sidebar_entries_[selected_sidebar_];
    if (entry.kind != SidebarEntryKind::Section) return;
    if (entry.project && !ConvertSelectedGroupToGeneral()) return;
    RequestConfirmation(ConfirmAction::DeleteGroup, "Delete group",
        "Delete this group? Its notes will remain in Home.");
}

void NotesWorkspaceComponent::DeleteSelectedSection() {
    if (selected_sidebar_ < 0 || selected_sidebar_ >= static_cast<int>(sidebar_entries_.size())) return;
    const SidebarEntry entry = sidebar_entries_[selected_sidebar_];
    if (entry.kind != SidebarEntryKind::Section) return;
    if (entry.project) {
        Notify("Project groups cannot be deleted; keep the group as General first.");
        return;
    }
    std::string error;
    if (!repository_.DeleteSection(entry.id, error)) {
        Notify("Group delete failed: " + error);
        return;
    }
    selected_section_id_.clear();
    trash_selected_ = false;
    selected_sidebar_ = 0;
    RebuildSidebar();
    RebuildVisibleNotes();
    Notify("Group deleted; its notes remain in Home");
}

void NotesWorkspaceComponent::OpenProjectNotes() {
    if (!project_context_) {
        Notify("No active project");
        return;
    }
    std::string error;
    NoteSection* section = repository_.EnsureProjectSection(
        project_context_->id, project_context_->name, error);
    if (!section) {
        Notify("Project notes unavailable: " + error);
        return;
    }
    const std::string section_id = section->id;
    const std::string section_name = section->name;
    active_project_section_id_ = section_id;
    if (editing_) CloseEditor();
    RebuildSidebar();
    SelectSectionById(section_id);
    Notify("Project notes: " + section_name);
}

bool NotesWorkspaceComponent::ConvertSelectedGroupToGeneral() {
    if (selected_sidebar_ < 0 || selected_sidebar_ >= static_cast<int>(sidebar_entries_.size())) return false;
    const SidebarEntry entry = sidebar_entries_[selected_sidebar_];
    if (entry.kind != SidebarEntryKind::Section || !entry.orphaned) return false;
    std::string error;
    if (!repository_.ConvertSectionToGeneral(entry.id, error)) {
        Notify("Group update failed: " + error);
        return false;
    }
    if (active_project_section_id_ && *active_project_section_id_ == entry.id) {
        active_project_section_id_.reset();
    }
    RebuildSidebar();
    RebuildVisibleNotes();
    Notify("Group kept as General: " + entry.label);
    return true;
}

void NotesWorkspaceComponent::RelinkSelectedGroupToActiveProject() {
    if (!project_context_) {
        Notify("No active project");
        return;
    }
    if (selected_sidebar_ < 0 || selected_sidebar_ >= static_cast<int>(sidebar_entries_.size())) return;
    const SidebarEntry entry = sidebar_entries_[selected_sidebar_];
    if (entry.kind != SidebarEntryKind::Section || !entry.orphaned) return;
    std::string resulting_id, error;
    if (!repository_.RelinkSectionToProject(
            entry.id, project_context_->id, project_context_->name, resulting_id, error)) {
        Notify("Relink failed: " + error);
        return;
    }
    active_project_section_id_ = resulting_id;
    if (editing_) CloseEditor();
    RebuildSidebar();
    SelectSectionById(resulting_id);
    Notify("Group linked to project: " + project_context_->name);
}

void NotesWorkspaceComponent::DeleteSelectedNote() {
    const bool has_target = editing_
        ? session_.Note() && !session_.Note()->deleted_at
        : !trash_selected_ && !visible_notes_.empty() && selected_card_ >= 0 &&
            selected_card_ < static_cast<int>(visible_notes_.size());
    if (!has_target) return;
    if (confirm_action_ == ConfirmAction::None) {
        RequestConfirmation(ConfirmAction::TrashNote, "Delete note", "Move this note to Trash?");
        return;
    }
    if (editing_) {
        DeleteOrRestoreActiveNote();
        return;
    }
    if (trash_selected_ || visible_notes_.empty() ||
        selected_card_ < 0 || selected_card_ >= static_cast<int>(visible_notes_.size())) {
        return;
    }
    NoteDocument& note = repository_.Notes()[visible_notes_[selected_card_]];
    std::string error;
    if (!repository_.MoveToTrash(note, error)) {
        Notify("Note delete failed: " + error);
        return;
    }
    Notify("Note moved to Trash");
    RebuildSidebar();
    RebuildVisibleNotes();
}

void NotesWorkspaceComponent::RestoreSelectedNote() {
    if (!trash_selected_ || visible_notes_.empty() ||
        selected_card_ < 0 || selected_card_ >= static_cast<int>(visible_notes_.size())) return;
    NoteDocument& note = repository_.Notes()[visible_notes_[selected_card_]];
    std::string error;
    if (!repository_.Restore(note, error)) Notify("Note restore failed: " + error);
    else Notify("Note restored");
    RebuildSidebar();
    RebuildVisibleNotes();
}

void NotesWorkspaceComponent::PermanentlyDeleteSelectedNote() {
    if (!trash_selected_ || visible_notes_.empty() || selected_card_ < 0 ||
        selected_card_ >= static_cast<int>(visible_notes_.size())) return;
    if (confirm_action_ == ConfirmAction::None) {
        RequestConfirmation(ConfirmAction::DeletePermanently, "Delete permanently",
            "Permanently delete this note? This cannot be undone.");
        return;
    }
    if (!trash_selected_ || visible_notes_.empty() ||
        selected_card_ < 0 || selected_card_ >= static_cast<int>(visible_notes_.size())) return;
    const std::string id = repository_.Notes()[visible_notes_[selected_card_]].id;
    std::string error;
    if (!repository_.PermanentlyDelete(id, error)) Notify("Permanent delete failed: " + error);
    else Notify("Note permanently deleted");
    RebuildSidebar();
    RebuildVisibleNotes();
}

void NotesWorkspaceComponent::EmptyTrash() {
    if (!trash_selected_ || visible_notes_.empty()) return;
    if (confirm_action_ == ConfirmAction::None) {
        RequestConfirmation(ConfirmAction::EmptyTrash, "Empty Trash",
            "Permanently delete every note in Trash? This cannot be undone.");
        return;
    }
    std::string error;
    if (!repository_.EmptyTrash(error)) Notify("Empty Trash failed: " + error);
    else Notify("Trash emptied");
    RebuildSidebar();
    RebuildVisibleNotes();
}

void NotesWorkspaceComponent::RequestConfirmation(
    ConfirmAction action, std::string title, std::string message) {
    confirm_action_ = action;
    confirm_title_ = std::move(title);
    confirm_message_ = std::move(message);
}

void NotesWorkspaceComponent::AcceptConfirmation() {
    const ConfirmAction action = confirm_action_;
    if (action == ConfirmAction::None) return;
    switch (action) {
        case ConfirmAction::TrashNote: DeleteSelectedNote(); break;
        case ConfirmAction::DeleteGroup: DeleteSelectedSection(); break;
        case ConfirmAction::DeletePermanently: PermanentlyDeleteSelectedNote(); break;
        case ConfirmAction::EmptyTrash: EmptyTrash(); break;
        case ConfirmAction::None: break;
    }
    CloseConfirmation();
}

void NotesWorkspaceComponent::CloseConfirmation() {
    confirm_action_ = ConfirmAction::None;
    confirm_title_.clear();
    confirm_message_.clear();
    confirm_yes_box_ = {};
    confirm_cancel_box_ = {};
}

void NotesWorkspaceComponent::OpenGroupPicker() {
    if (session_.Note() && !session_.Note()->deleted_at) group_picker_open_ = true;
}

void NotesWorkspaceComponent::SelectGroupFromPicker(size_t index) {
    if (!session_.Note()) return;
    auto& note = *session_.Note();
    if (index == 0) note.section_id.reset();
    else {
        size_t current = 1;
        for (const NoteSection& section : repository_.Sections()) {
            if (section.deleted_at) continue;
            if (current++ == index) { note.section_id = section.id; break; }
        }
    }
    note.updated_at = UtcNow();
    ++note.revision;
    std::string error;
    if (!repository_.Save(note, error)) Notify("Move failed: " + error);
    else Notify("Group: " + SectionName(note));
    group_picker_open_ = false;
    RebuildSidebar();
}

void NotesWorkspaceComponent::CycleSortMode() {
    sort_mode_ = sort_mode_ == SortMode::Updated ? SortMode::Created :
        sort_mode_ == SortMode::Created ? SortMode::Title :
        sort_mode_ == SortMode::Title ? SortMode::Pinned : SortMode::Updated;
    selected_card_ = 0;
    RebuildVisibleNotes();
}

void NotesWorkspaceComponent::FinishCreateSection() {
    if (group_name_input_.empty()) {
        editing_section_id_.reset();
        focus_ = FocusArea::Sidebar;
        return;
    }
    std::string error;
    if (editing_section_id_) {
        if (!repository_.RenameSection(*editing_section_id_, group_name_input_, error)) {
            Notify("Group rename failed: " + error);
        } else {
            selected_section_id_ = *editing_section_id_;
            Notify("Group renamed: " + group_name_input_);
        }
    } else {
        NoteSection& section = repository_.CreateSection(group_name_input_);
        selected_section_id_ = section.id;
        if (!repository_.SaveSections(error)) Notify("Group save failed: " + error);
        else Notify("Group created: " + section.name);
    }
    trash_selected_ = false;
    editing_section_id_.reset();
    group_name_input_.clear(); RebuildSidebar();
    for (size_t i = 0; i < sidebar_entries_.size(); ++i) if (sidebar_entries_[i].id == selected_section_id_) selected_sidebar_ = static_cast<int>(i);
    RebuildVisibleNotes(); focus_ = FocusArea::Cards;
}

void NotesWorkspaceComponent::NewNote() {
    NoteDocument note = MakeNewNote(repository_.DeviceId());
    if (!selected_section_id_.empty() && !trash_selected_) note.section_id = selected_section_id_;
    repository_.Notes().insert(repository_.Notes().begin(), std::move(note)); active_note_id_ = repository_.Notes().front().id;
    editing_ = true; session_.SetNote(&repository_.Notes().front()); ResetEditorViewport(); focus_ = FocusArea::Body;
    std::string error; if (!repository_.Save(repository_.Notes().front(), error)) Notify("Note save failed: " + error); else Notify("New note");
    RebuildSidebar(); RebuildVisibleNotes();
}

bool NotesWorkspaceComponent::ImportNote(
    std::string title, std::vector<NoteBlock> blocks, std::string& error) {
    if (!Save(error)) return false;

    NoteDocument note = MakeNewNote(repository_.DeviceId());
    note.title = std::move(title);
    note.blocks = std::move(blocks);
    if (!selected_section_id_.empty() && !trash_selected_) {
        note.section_id = selected_section_id_;
    }
    if (!repository_.Save(note, error)) return false;

    repository_.Notes().insert(repository_.Notes().begin(), std::move(note));
    if (trash_selected_) {
        trash_selected_ = false;
        selected_section_id_.clear();
        selected_sidebar_ = 0;
    }
    OpenNote(0);
    save_status_ = "Saved";
    RebuildSidebar();
    RebuildVisibleNotes();
    return true;
}

void NotesWorkspaceComponent::EditSelectedNote() {
    if (editing_) return;
    if (trash_selected_) {
        Notify("Restore the note before editing");
        return;
    }
    if (visible_notes_.empty() || selected_card_ < 0 ||
        selected_card_ >= static_cast<int>(visible_notes_.size())) {
        Notify("Select a note to edit");
        return;
    }
    OpenSelectedNote();
    Notify("Editing note");
}

void NotesWorkspaceComponent::DeleteSelection() {
    if (trash_selected_ && !editing_) {
        PermanentlyDeleteSelectedNote();
    } else {
        DeleteSelectedNote();
    }
}

void NotesWorkspaceComponent::OpenNote(size_t note_index) {
    if (note_index >= repository_.Notes().size()) return;
    active_note_id_ = repository_.Notes()[note_index].id;
    editing_ = true;
    session_.SetNote(&repository_.Notes()[note_index]);
    note_search_matches_.clear();
    note_search_active_ = 0;
    ResetEditorViewport();
    focus_ = FocusArea::Body;
}

void NotesWorkspaceComponent::OpenSelectedNote() { if (!visible_notes_.empty()) OpenNote(visible_notes_[selected_card_]); }

bool NotesWorkspaceComponent::Save(std::string& error) {
    if (!session_.Note() || !session_.Dirty()) return true;
    if (!repository_.Save(*session_.Note(), error)) { Notify("Save failed: " + error); return false; }
    session_.MarkSaved(); last_saved_ = std::chrono::steady_clock::now(); save_status_ = "Saved"; RebuildSidebar(); RebuildVisibleNotes(); return true;
}

void NotesWorkspaceComponent::TouchAndSave() { std::string error; Save(error); }

void NotesWorkspaceComponent::CloseEditor() { TouchAndSave(); editing_ = false; session_.SetNote(nullptr); note_search_matches_.clear(); note_search_active_ = 0; ResetEditorViewport(); active_note_id_.reset(); RebuildSidebar(); RebuildVisibleNotes(); focus_ = FocusArea::Cards; }

void NotesWorkspaceComponent::TogglePin() {
    if (!session_.Note() || session_.Note()->deleted_at) return;
    auto& note = *session_.Note();
    note.pinned = !note.pinned;
    if (note.pinned) note.pinned_at = UtcNow(); else note.pinned_at.reset(); note.updated_at = UtcNow(); ++note.revision;
    std::string error; if (!repository_.Save(note, error)) Notify("Pin save failed: " + error); else Notify(note.pinned ? "Note pinned" : "Note unpinned");
}

void NotesWorkspaceComponent::MoveActiveNoteToNextSection() {
    if (!session_.Note() || session_.Note()->deleted_at) return;
    auto& note = *session_.Note();
    std::vector<const NoteSection*> sections;
    for (const NoteSection& section : repository_.Sections()) {
        if (!section.deleted_at) sections.push_back(&section);
    }
    if (sections.empty()) note.section_id.reset();
    else if (!note.section_id) note.section_id = sections.front()->id;
    else {
        auto found = std::find_if(
            sections.begin(), sections.end(),
            [&](const NoteSection* value) { return value->id == *note.section_id; });
        if (found == sections.end() || ++found == sections.end()) note.section_id.reset();
        else note.section_id = (*found)->id;
    }
    note.updated_at = UtcNow(); ++note.revision; std::string error; if (!repository_.Save(note, error)) Notify("Move failed: " + error); else Notify("Group: " + SectionName(note)); RebuildSidebar();
}

void NotesWorkspaceComponent::DeleteOrRestoreActiveNote() {
    auto index = ActiveNoteIndex(); if (!index) return; std::string error; auto& note = repository_.Notes()[*index];
    const bool restoring = note.deleted_at.has_value(); const bool ok = restoring ? repository_.Restore(note, error) : repository_.MoveToTrash(note, error);
    if (!ok) Notify("Note update failed: " + error); else { Notify(restoring ? "Note restored" : "Note moved to Trash"); CloseEditor(); }
}

bool NotesWorkspaceComponent::HandleTextFieldEvent(std::string& value, size_t& cursor, ftxui::Event event, std::function<void()> on_change) {
    if (event == ftxui::Event::ArrowLeft && cursor > 0) { cursor = textlt::utils::PreviousUtf8CodepointStart(value, cursor); return true; }
    if (event == ftxui::Event::ArrowRight && cursor < value.size()) { cursor = textlt::utils::NextUtf8CodepointStart(value, cursor); return true; }
    if (event == ftxui::Event::Home) { cursor = 0; return true; }
    if (event == ftxui::Event::End) { cursor = value.size(); return true; }
    if (IsBackspace(event) && cursor > 0) { size_t previous = textlt::utils::PreviousUtf8CodepointStart(value, cursor); value.erase(previous, cursor - previous); cursor = previous; on_change(); return true; }
    if (IsDelete(event) && cursor < value.size()) { size_t next = textlt::utils::NextUtf8CodepointStart(value, cursor); value.erase(cursor, next - cursor); on_change(); return true; }
    const auto pasted = ClipboardController::PastedText(event, false);
    if (pasted || (event.is_character() && !event.input().empty() &&
                   static_cast<unsigned char>(event.input()[0]) >= 0x20)) {
        const auto& text = pasted ? *pasted : event.input();
        value.insert(cursor, text);
        cursor += text.size();
        on_change();
        return true;
    }
    return false;
}

bool NotesWorkspaceComponent::HandleOverviewEvent(ftxui::Event event) {
    if ((!sidebar_visible_ || sidebar_content_selected_) &&
        (focus_ == FocusArea::Sidebar || focus_ == FocusArea::GroupName)) {
        focus_ = FocusArea::Cards;
    }
    if (focus_ == FocusArea::GroupName) {
        if (event == ftxui::Event::Return) { FinishCreateSection(); return true; }
        if (event == ftxui::Event::Escape) { focus_ = FocusArea::Sidebar; group_name_input_.clear(); editing_section_id_.reset(); return true; }
        return HandleTextFieldEvent(group_name_input_, group_name_cursor_, event, [] {});
    }
    if (event == ftxui::Event::Tab) {
        if (!sidebar_visible_ || sidebar_content_selected_) {
            focus_ = focus_ == FocusArea::Search ? FocusArea::Cards : FocusArea::Search;
            return true;
        }
        focus_ = focus_ == FocusArea::Sidebar ? FocusArea::Search : focus_ == FocusArea::Search ? FocusArea::Cards : FocusArea::Sidebar;
        return true;
    }
    if (focus_ == FocusArea::Search) {
        if (event == ftxui::Event::Escape) { search_.clear(); search_cursor_ = 0; RebuildVisibleNotes(); focus_ = FocusArea::Cards; return true; }
        return HandleTextFieldEvent(search_, search_cursor_, event, [this] { RebuildVisibleNotes(); });
    }
    if (focus_ == FocusArea::Sidebar) {
        if (event == ftxui::Event::ArrowUp) { selected_sidebar_ = std::max(0, selected_sidebar_ - 1); return true; }
        if (event == ftxui::Event::ArrowDown) { selected_sidebar_ = std::min(static_cast<int>(sidebar_entries_.size()) - 1, selected_sidebar_ + 1); return true; }
        if (event == ftxui::Event::Return) { SelectSidebarEntry(selected_sidebar_); return true; }
    } else {
        if (event == ftxui::Event::ArrowLeft || event == ftxui::Event::ArrowUp) { selected_card_ = std::max(0, selected_card_ - 1); return true; }
        if (event == ftxui::Event::ArrowRight || event == ftxui::Event::ArrowDown) { selected_card_ = std::min(static_cast<int>(visible_notes_.size()) - 1, selected_card_ + 1); return true; }
        if (event == ftxui::Event::Return) { OpenSelectedNote(); return true; }
    }
    return false;
}

void NotesWorkspaceComponent::CopySelectionToClipboard(bool cut) {
    if (!session_.HasSelection()) return;
    if (!write_clipboard_ || !write_clipboard_(session_.SelectedText())) {
        Notify(ClipboardController::kWriteFailed);
        return;
    }
    if (cut && session_.DeleteSelection()) TouchAndSave();
}

void NotesWorkspaceComponent::PasteText(const std::string& text) {
    if (!text.empty() && session_.Insert(text)) TouchAndSave();
}

void NotesWorkspaceComponent::EditHistory(bool redo) {
    if (redo) session_.Redo();
    else session_.Undo();
    TouchAndSave();
}

void NotesWorkspaceComponent::PasteFromClipboard() {
    const auto* note = EditingNote();
    if (!note || note->deleted_at || !read_clipboard_) return;
    const auto cursor = session_.Cursor();
    const auto selection = session_.Selection();
    read_clipboard_([this, id = note->id, revision = note->revision, cursor, selection](
                       std::optional<std::string> text) {
        const auto* current = EditingNote();
        if (!current || current->deleted_at || current->id != id || current->revision != revision ||
            session_.Cursor() != cursor || session_.Selection() != selection) return;
        if (text) *text = ClipboardController::NormalizeText(*text);
        if (text && !text->empty()) PasteText(*text);
        else Notify("Clipboard has no text");
    });
}

void NotesWorkspaceComponent::ExecuteContextCommand(NotesContextMenu::Command command) {
    using Command = NotesContextMenu::Command;
    switch (command) {
        case Command::Undo: EditHistory(false); break;
        case Command::Redo: EditHistory(true); break;
        case Command::Cut: CopySelectionToClipboard(true); break;
        case Command::Copy: CopySelectionToClipboard(false); break;
        case Command::Paste: PasteFromClipboard(); break;
        case Command::SelectAll: session_.SelectAll(); break;
        case Command::NormalText: SetBlockType(NoteBlockType::Paragraph); break;
        case Command::Heading1: ToggleHeading(1); break;
        case Command::Heading2: ToggleHeading(2); break;
        case Command::Heading3: ToggleHeading(3); break;
        case Command::BulletedList: SetBlockType(NoteBlockType::BulletItem); break;
        case Command::NumberedList: SetBlockType(NoteBlockType::NumberedItem); break;
    }
}

bool NotesWorkspaceComponent::HandleContextMenuEvent(ftxui::Event event) {
    if (context_menu_.IsOpen()) {
        return context_menu_.OnEvent(std::move(event),
            [this](NotesContextMenu::Command command) { ExecuteContextCommand(command); });
    }

    const auto* note = EditingNote();
    if (!note || !event.is_mouse()) return false;
    const auto& mouse = event.mouse();
    if (mouse.button != ftxui::Mouse::Right || mouse.motion != ftxui::Mouse::Pressed ||
        !editor_view_box_.Contain(mouse.x, mouse.y) ||
        editor_scrollbar_box_.Contain(mouse.x, mouse.y)) return false;
    const bool in_text_column = std::any_of(editor_column_boxes_.begin(), editor_column_boxes_.end(),
        [&mouse](const ftxui::Box& column) { return column.Contain(mouse.x, mouse.y); });
    if (!in_text_column) return false;

    using Command = NotesContextMenu::Command;
    const bool writable = !note->deleted_at.has_value();
    const bool can_copy = session_.HasSelection() && static_cast<bool>(write_clipboard_);
    const bool has_text = note->blocks.size() > 1 ||
        (!note->blocks.empty() && !BlockText(note->blocks.front()).empty());
    editor_mouse_selecting_ = false;
    editor_scrollbar_dragging_ = false;
    context_menu_.Open(mouse.x, mouse.y, {
        {"Undo", Command::Undo, writable && session_.CanUndo()},
        {"Redo", Command::Redo, writable && session_.CanRedo()},
        {},
        {"Cut", Command::Cut, writable && can_copy},
        {"Copy", Command::Copy, can_copy},
        {"Paste", Command::Paste, writable && static_cast<bool>(read_clipboard_)},
        {"Select All", Command::SelectAll, has_text},
        {},
        {"Normal Text", Command::NormalText, writable && session_.CanSetBlockType(NoteBlockType::Paragraph)},
        {"Heading 1", Command::Heading1, writable && session_.CanSetHeading(1)},
        {"Heading 2", Command::Heading2, writable && session_.CanSetHeading(2)},
        {"Heading 3", Command::Heading3, writable && session_.CanSetHeading(3)},
        {},
        {"Bulleted List", Command::BulletedList, writable && session_.CanSetBlockType(NoteBlockType::BulletItem)},
        {"Numbered List", Command::NumberedList, writable && session_.CanSetBlockType(NoteBlockType::NumberedItem)},
    });
    return true;
}

bool NotesWorkspaceComponent::HandleEditorEvent(ftxui::Event event) {
    if (event == ftxui::Event::Escape) { CloseEditor(); return true; }
    if (event == ftxui::Event::PageDown) {
        ScrollEditor(static_cast<int>(std::max<size_t>(1, editor_visible_rows_)));
        return true;
    }
    if (event == ftxui::Event::PageUp) {
        ScrollEditor(-static_cast<int>(std::max<size_t>(1, editor_visible_rows_)));
        return true;
    }
    if (IsCtrl(event, 'a') && focus_ == FocusArea::Body) { session_.SelectAll(); return true; }
    if (IsCtrl(event, 'c') && focus_ == FocusArea::Body) {
        CopySelectionToClipboard(false);
        return true;
    }
    if (IsCtrl(event, 'x') && focus_ == FocusArea::Body) {
        CopySelectionToClipboard(true);
        return true;
    }
    if (focus_ == FocusArea::Body) {
        if (const auto text = ClipboardController::PastedText(event)) {
            PasteText(*text);
            return true;
        }
    }
    if (IsCtrl(event, 'z')) { EditHistory(false); return true; }
    if (IsCtrl(event, 'y')) { EditHistory(true); return true; }
    if (event == ftxui::Event::Tab) {
        if (session_.Note() &&
            session_.Note()->blocks[session_.Cursor().block].type != NoteBlockType::Paragraph) {
            session_.Indent(1);
            TouchAndSave();
        }
        return true;
    }
    if ((event.input() == "Shift+Tab" || event.input() == "\x1B[Z") && focus_ == FocusArea::Body) {
        session_.Indent(-1);
        TouchAndSave();
        return true;
    }
    bool changed = false;
    if (event == ftxui::Event::ArrowLeft) return session_.MoveLeft();
    if (event == ftxui::Event::ArrowRight) return session_.MoveRight();
    if (event == ftxui::Event::ArrowUp) return session_.MoveUp();
    if (event == ftxui::Event::ArrowDown) return session_.MoveDown();
    if (IsShift(event, 'L')) return session_.MoveLeft(true);
    if (IsShift(event, 'R')) return session_.MoveRight(true);
    if (IsShift(event, 'U')) return session_.MoveUp(true);
    if (IsShift(event, 'D')) return session_.MoveDown(true);
    if (event == ftxui::Event::Home) { session_.MoveHome(); return true; }
    if (event == ftxui::Event::End) { session_.MoveEnd(); return true; }
    if (event == ftxui::Event::Return) changed = session_.Enter();
    else if (IsBackspace(event)) changed = session_.Backspace();
    else if (IsDelete(event)) changed = session_.DeleteForward();
    else if (event.is_character() && !event.input().empty() && static_cast<unsigned char>(event.input()[0]) >= 0x20) changed = session_.Insert(event.input());
    if (changed) TouchAndSave();
    return changed;
}

bool NotesWorkspaceComponent::OnEvent(ftxui::Event event) {
    // Event::Custom is used by the app to request a redraw. It is never an
    // input event, so it must not be reported as handled: the dispatcher
    // posts another Event::Custom for every handled event, which would turn
    // this into an endless redraw loop while a popup is open.
    if (event == ftxui::Event::Custom) return false;
    if (confirm_action_ != ConfirmAction::None) {
        if (event == ftxui::Event::Escape) { CloseConfirmation(); return true; }
        if (event == ftxui::Event::Return) { AcceptConfirmation(); return true; }
        if (event.is_mouse() && event.mouse().button == ftxui::Mouse::Left &&
            event.mouse().motion == ftxui::Mouse::Pressed) {
            if (confirm_yes_box_.Contain(event.mouse().x, event.mouse().y)) { AcceptConfirmation(); return true; }
            if (confirm_cancel_box_.Contain(event.mouse().x, event.mouse().y)) { CloseConfirmation(); return true; }
        }
        return true;
    }
    if (title_editor_open_) {
        if (event == ftxui::Event::Escape) { CloseTitleEditor(); return true; }
        if (event == ftxui::Event::Return) { CommitTitleEditor(); return true; }
        if (event.is_mouse() && event.mouse().button == ftxui::Mouse::Left &&
            event.mouse().motion == ftxui::Mouse::Pressed) {
            if (title_editor_save_box_.Contain(event.mouse().x, event.mouse().y)) {
                CommitTitleEditor();
            } else if (title_editor_cancel_box_.Contain(event.mouse().x, event.mouse().y)) {
                CloseTitleEditor();
            }
            return true;
        }
        HandleTextFieldEvent(title_editor_input_, title_editor_cursor_, event,
            [this] { LimitTitleEditorInput(); });
        return true;
    }
    if (group_picker_open_) {
        if (event == ftxui::Event::Escape) { group_picker_open_ = false; return true; }
        if (event.is_mouse() && event.mouse().button == ftxui::Mouse::Left &&
            event.mouse().motion == ftxui::Mouse::Pressed) {
            for (size_t i = 0; i < group_picker_boxes_.size(); ++i) {
                if (group_picker_boxes_[i].Contain(event.mouse().x, event.mouse().y)) {
                    SelectGroupFromPicker(i);
                    return true;
                }
            }
            if (group_picker_cancel_box_.Contain(event.mouse().x, event.mouse().y)) {
                group_picker_open_ = false;
                return true;
            }
        }
        return true;
    }
    if (HandleContextMenuEvent(event)) return true;
    if (sidebar_visible_ && sidebar_content_selected_ && event.is_mouse() &&
        sidebar_content_box_.Contain(event.mouse().x, event.mouse().y)) {
        const auto& mouse = event.mouse();
        if (mouse.button == ftxui::Mouse::WheelUp) {
            if (heading_scroll_ > 0) --heading_scroll_;
            return true;
        }
        if (mouse.button == ftxui::Mouse::WheelDown) {
            if (heading_scroll_ + 1 < heading_blocks_.size() && !heading_boxes_.empty()) {
                const auto& last = heading_boxes_.back();
                if (last.y_max - last.y_min + 1 < last_heading_height_) {
                    ++heading_scroll_;
                }
            }
            return true;
        }
        if (mouse.button == ftxui::Mouse::Left &&
            mouse.motion == ftxui::Mouse::Pressed) {
            const size_t first_rendered = heading_blocks_.size() - heading_boxes_.size();
            for (size_t i = 0; i < heading_boxes_.size(); ++i) {
                if (heading_boxes_[i].Contain(mouse.x, mouse.y)) {
                    JumpToNotePosition({heading_blocks_[first_rendered + i], 0});
                    break;
                }
            }
            return true;
        }
    }
    if (editing_ && event.is_mouse()) {
        const auto& mouse = event.mouse();
        const bool over_editor = editor_view_box_.Contain(mouse.x, mouse.y) ||
            editor_scrollbar_box_.Contain(mouse.x, mouse.y);
        if (mouse.button == ftxui::Mouse::WheelUp && over_editor) {
            ScrollEditor(-kMouseWheelScrollRows);
            return true;
        }
        if (mouse.button == ftxui::Mouse::WheelDown && over_editor) {
            ScrollEditor(kMouseWheelScrollRows);
            return true;
        }
        if (editor_scrollbar_dragging_ && mouse.motion == ftxui::Mouse::Released) {
            editor_scrollbar_dragging_ = false;
            return true;
        }
        if (editor_scrollbar_dragging_ && mouse.motion == ftxui::Mouse::Moved) {
            const EditorScrollMetrics metrics = ComputeEditorScrollMetrics(
                editor_column_rows_, editor_visible_rows_, editor_rows_.size());
            if (metrics.needs_scrollbar && metrics.track_space > 0) {
                const long long delta_rows =
                    static_cast<long long>(mouse.y - editor_scrollbar_drag_y_) *
                    static_cast<long long>(metrics.max_scroll) /
                    static_cast<long long>(metrics.track_space);
                const long long target =
                    static_cast<long long>(editor_scrollbar_drag_scroll_) +
                    delta_rows;
                editor_scroll_ = static_cast<int>(std::clamp<long long>(
                    target, 0, static_cast<long long>(metrics.max_scroll)));
            }
            return true;
        }
        if (editor_mouse_selecting_ && mouse.motion == ftxui::Mouse::Released) {
            editor_mouse_selecting_ = false;
            return true;
        }
        if (editor_mouse_selecting_ && mouse.motion == ftxui::Mouse::Moved) {
            int overshoot = 0;
            if (mouse.y < editor_view_box_.y_min) {
                overshoot = mouse.y - editor_view_box_.y_min;
            } else if (mouse.y > editor_view_box_.y_max) {
                overshoot = mouse.y - editor_view_box_.y_max;
            }
            if (overshoot != 0) ScrollEditor(overshoot);
            const int clamped_x = std::clamp(
                mouse.x, editor_view_box_.x_min, editor_view_box_.x_max);
            const int clamped_y = std::clamp(
                mouse.y, editor_view_box_.y_min, editor_view_box_.y_max);
            if (const auto position = NotePositionAt(clamped_x, clamped_y)) {
                session_.SetCursor(*position, true);
            }
            return true;
        }
        if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed &&
            editor_scrollbar_box_.Contain(mouse.x, mouse.y)) {
            const EditorScrollMetrics metrics = ComputeEditorScrollMetrics(
                editor_column_rows_, editor_visible_rows_, editor_rows_.size());
            if (metrics.needs_scrollbar && metrics.track_space > 0) {
                const int relative_y = std::clamp(
                    mouse.y - editor_scrollbar_box_.y_min, 0,
                    static_cast<int>(metrics.viewport_rows) - 1);
                const int thumb_top = std::clamp(
                    relative_y - static_cast<int>(metrics.thumb_height / 2), 0,
                    static_cast<int>(metrics.track_space));
                editor_scroll_ = static_cast<int>(
                    (static_cast<size_t>(thumb_top) * metrics.max_scroll) /
                    metrics.track_space);
                editor_scrollbar_dragging_ = true;
                editor_scrollbar_drag_y_ = mouse.y;
                editor_scrollbar_drag_scroll_ = editor_scroll_;
            }
            return true;
        }
        if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed &&
            editor_view_box_.Contain(mouse.x, mouse.y)) {
            if (const auto position = NotePositionAt(mouse.x, mouse.y)) {
                focus_ = FocusArea::Body;
                session_.SetCursor(
                    *position, mouse.shift || mouse.control);
                editor_mouse_selecting_ = true;
                return true;
            }
        }
    }
    if (event.is_mouse() && event.mouse().button == ftxui::Mouse::Left && event.mouse().motion == ftxui::Mouse::Pressed) {
        const auto& mouse = event.mouse();
        if (sidebar_visible_) {
            if (documents_tab_box_.Contain(mouse.x, mouse.y)) { if (on_documents_) on_documents_(); return true; }
            if (actions_tab_box_.Contain(mouse.x, mouse.y)) { if (on_actions_) on_actions_(); return true; }
            if (sidebar_notes_tab_box_.Contain(mouse.x, mouse.y)) {
                sidebar_content_selected_ = false;
                if (!editing_) focus_ = FocusArea::Sidebar;
                return true;
            }
            if (sidebar_content_tab_box_.Contain(mouse.x, mouse.y)) {
                sidebar_content_selected_ = true;
                FocusMainArea();
                return true;
            }
            if (sidebar_content_selected_ && sidebar_box_.Contain(mouse.x, mouse.y)) {
                return true;
            }
            if (footer_new_note_box_.Contain(mouse.x, mouse.y)) { NewNote(); return true; }
            if (footer_delete_note_box_.Contain(mouse.x, mouse.y)) { DeleteSelectedNote(); return true; }
            if (footer_restore_note_box_.Contain(mouse.x, mouse.y)) { RestoreSelectedNote(); return true; }
            if (footer_permanent_delete_box_.Contain(mouse.x, mouse.y)) { PermanentlyDeleteSelectedNote(); return true; }
            if (footer_empty_trash_box_.Contain(mouse.x, mouse.y)) { EmptyTrash(); return true; }
            if (footer_new_group_box_.Contain(mouse.x, mouse.y)) { StartCreateSection(); return true; }
            if (footer_rename_group_box_.Contain(mouse.x, mouse.y)) { StartRenameSection(); return true; }
            if (footer_delete_group_box_.Contain(mouse.x, mouse.y)) {
                RequestDeleteSelectedSection();
                return true;
            }
            if (footer_project_notes_box_.Contain(mouse.x, mouse.y)) { OpenProjectNotes(); return true; }
            if (footer_orphan_general_box_.Contain(mouse.x, mouse.y)) { ConvertSelectedGroupToGeneral(); return true; }
            if (footer_orphan_relink_box_.Contain(mouse.x, mouse.y)) { RelinkSelectedGroupToActiveProject(); return true; }
            for (size_t i = 0; i < sidebar_row_boxes_.size(); ++i) if (sidebar_row_boxes_[i].Contain(mouse.x, mouse.y)) { focus_ = FocusArea::Sidebar; SelectSidebarEntry(static_cast<int>(i)); return true; }
        }
        if (!editing_) {
            if (sort_box_.Contain(mouse.x, mouse.y)) { CycleSortMode(); return true; }
            if (new_note_box_.Contain(mouse.x, mouse.y)) { NewNote(); return true; }
            if (search_box_.Contain(mouse.x, mouse.y)) { focus_ = FocusArea::Search; return true; }
            for (size_t i = 0; i < card_boxes_.size(); ++i) if (card_boxes_[i].Contain(mouse.x, mouse.y)) { selected_card_ = static_cast<int>(i); OpenSelectedNote(); return true; }
        }
        if (editing_) {
            if (back_box_.Contain(mouse.x, mouse.y)) { CloseEditor(); return true; }
            if (title_box_.Contain(mouse.x, mouse.y)) { OpenTitleEditor(); return true; }
            if (pin_box_.Contain(mouse.x, mouse.y)) { TogglePin(); return true; }
            if (section_box_.Contain(mouse.x, mouse.y)) { OpenGroupPicker(); return true; }
            if (trash_box_.Contain(mouse.x, mouse.y)) {
                if (session_.Note() && session_.Note()->deleted_at) DeleteOrRestoreActiveNote();
                else DeleteSelectedNote();
                return true;
            }
            if (heading_1_box_.Contain(mouse.x, mouse.y)) { ToggleHeading(1); return true; }
            if (heading_2_box_.Contain(mouse.x, mouse.y)) { ToggleHeading(2); return true; }
            if (heading_3_box_.Contain(mouse.x, mouse.y)) { ToggleHeading(3); return true; }
            if (copy_box_.Contain(mouse.x, mouse.y)) { CopyCurrentParagraph(); return true; }
            if (col_1_box_.Contain(mouse.x, mouse.y)) { SetEditorColumns(1); return true; }
            if (col_2_box_.Contain(mouse.x, mouse.y)) { SetEditorColumns(2); return true; }
            if (col_3_box_.Contain(mouse.x, mouse.y)) { SetEditorColumns(3); return true; }
        }
    }
    return editing_ ? HandleEditorEvent(std::move(event)) : HandleOverviewEvent(std::move(event));
}

ftxui::Element NotesWorkspaceComponent::RenderConfirmationPopup() {
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    return RenderDeleteConfirmationPopup(
        theme, confirm_title_, confirm_message_, confirm_cancel_box_, confirm_yes_box_);
}

ftxui::Element NotesWorkspaceComponent::RenderGroupPicker() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    Elements rows = {
        text(" Select group ") | bold | color(theme.modal_accent),
        separator() | color(theme.modal_border),
    };
    size_t count = 1;
    for (const NoteSection& section : repository_.Sections()) if (!section.deleted_at) ++count;
    group_picker_boxes_.assign(count, {});
    rows.push_back(text(" Home ") | reflect(group_picker_boxes_[0]) | color(theme.modal_text_color));
    size_t index = 1;
    for (const NoteSection& section : repository_.Sections()) {
        if (section.deleted_at) continue;
        rows.push_back(text(" " + section.name + " ") |
            reflect(group_picker_boxes_[index++]) | color(theme.modal_text_color));
    }
    ButtonSpec cancel = ButtonSpecFromLabel(
        "Cancel", ButtonRole::Cancel, ButtonVariant::Minimal, ButtonSize::Compact);
    rows.push_back(separator() | color(theme.modal_border));
    rows.push_back(hbox({filler(), RenderModalFlatButton(theme, cancel) |
        reflect(group_picker_cancel_box_), filler()}));
    return vbox(std::move(rows)) | size(WIDTH, EQUAL, 42) |
        borderStyled(HEAVY, theme.modal_border) | bgcolor(theme.modal_background) | clear_under;
}

void NotesWorkspaceComponent::LimitTitleEditorInput() {
    const size_t end = textlt::utils::Utf8ByteIndexAtDisplayColumn(
        title_editor_input_, 0, kNoteTitleMaxChars);
    if (end < title_editor_input_.size()) {
        title_editor_input_.erase(end);
    }
    if (title_editor_cursor_ > title_editor_input_.size()) {
        title_editor_cursor_ = title_editor_input_.size();
    }
}

void NotesWorkspaceComponent::OpenTitleEditor() {
    auto* note = session_.Note();
    if (!note) return;
    title_editor_input_ = note->title;
    LimitTitleEditorInput();
    title_editor_cursor_ = title_editor_input_.size();
    title_editor_open_ = true;
}

void NotesWorkspaceComponent::CloseTitleEditor() {
    title_editor_open_ = false;
    title_editor_input_.clear();
    title_editor_cursor_ = 0;
}

void NotesWorkspaceComponent::CommitTitleEditor() {
    auto* note = session_.Note();
    if (note && note->title != title_editor_input_) {
        note->title = title_editor_input_;
        note->updated_at = UtcNow();
        ++note->revision;
        std::string error;
        if (!repository_.Save(*note, error)) {
            Notify("Title save failed: " + error);
        } else {
            RebuildSidebar();
            RebuildVisibleNotes();
        }
    }
    CloseTitleEditor();
}

ftxui::Element NotesWorkspaceComponent::RenderTitleEditor() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    ButtonSpec save = ButtonSpecFromLabel(
        "Save", ButtonRole::Primary, ButtonVariant::AccentEdges, ButtonSize::Compact);
    ButtonSpec cancel = ButtonSpecFromLabel(
        "Cancel", ButtonRole::Cancel, ButtonVariant::Minimal, ButtonSize::Compact);
    const size_t cursor = std::min(title_editor_cursor_, title_editor_input_.size());
    std::string field = " " + title_editor_input_.substr(0, cursor) + "▌" +
        title_editor_input_.substr(cursor);
    const size_t field_width = static_cast<size_t>(std::max(0, string_width(field)));
    const size_t target_width = kNoteTitleMaxChars + 2;
    if (field_width < target_width) field.append(target_width - field_width, ' ');
    return vbox({
        text(" Note title ") | bold | color(theme.modal_accent),
        separator() | color(theme.modal_border),
        text(" Title ") | color(theme.modal_text_color),
        text(field) | borderStyled(LIGHT, theme.modal_accent),
        text(" Up to " + std::to_string(kNoteTitleMaxChars) +
            " characters ") | dim | color(theme.modal_text_color),
        separator() | color(theme.modal_border),
        hbox({
            filler(),
            RenderModalFlatButton(theme, cancel) | reflect(title_editor_cancel_box_),
            text("  "),
            RenderModalFlatButton(theme, save) | reflect(title_editor_save_box_),
            filler(),
        }),
    }) | size(WIDTH, EQUAL, 42) | borderStyled(HEAVY, theme.modal_border) |
        bgcolor(theme.modal_background) | clear_under;
}

void NotesWorkspaceComponent::ToggleHeading(int level) { if (session_.ToggleHeading(level)) TouchAndSave(); }
void NotesWorkspaceComponent::SetBlockType(NoteBlockType type) {
    if (type == NoteBlockType::CheckItem && session_.Note() &&
        session_.Note()->blocks[session_.Cursor().block].type == NoteBlockType::CheckItem) {
        if (session_.ToggleCheck()) TouchAndSave();
        return;
    }
    if (session_.SetBlockType(type)) TouchAndSave();
}
std::optional<NotesWorkspaceComponent::TextTarget> NotesWorkspaceComponent::CaptureTextTarget() const {
    const auto* note = EditingNote();
    if (!note || note->blocks.empty()) {
        return std::nullopt;
    }
    if (session_.HasSelection()) {
        const auto [start, end] = session_.Selection();
        return TextTarget{note->id, note->revision, start, end, session_.SelectedText()};
    }
    const size_t block = std::min(session_.Cursor().block, note->blocks.size() - 1);
    auto text = BlockText(note->blocks[block]);
    if (text.empty()) {
        return std::nullopt;
    }
    return TextTarget{note->id, note->revision, {block, 0}, {block, text.size()}, std::move(text)};
}

bool NotesWorkspaceComponent::ReplaceTextTarget(
    const TextTarget& target, const std::string& replacement, std::string& error) {
    const auto* note = EditingNote();
    if (!note || note->id != target.note_id || note->revision != target.revision ||
        note->deleted_at) {
        error = "The original note has changed. Reopen Quick AI to capture its text again.";
        return false;
    }
    if (replacement == target.text) {
        return true;
    }
    session_.SetCursor(target.start);
    session_.SetCursor(target.end, true);
    if (!session_.Insert(replacement)) {
        error = "Could not replace the note text.";
        return false;
    }
    JumpToNotePosition(session_.Cursor());
    ClearNoteSearchHighlights();
    if (!Save(error)) {
        error = "Note text changed, but saving failed: " + error;
    }
    return true;
}

void NotesWorkspaceComponent::CopyCurrentParagraph() {
    auto* note = session_.Note();
    if (!note || note->blocks.empty()) return;
    const size_t block_index = std::min(
        session_.Cursor().block, note->blocks.size() - 1);
    const std::string text = TrimWhitespace(BlockText(note->blocks[block_index]));
    if (text.empty()) {
        Notify("Nothing to copy");
        return;
    }
    if (!write_clipboard_) {
        Notify("Clipboard unavailable");
        return;
    }
    static const std::regex markdown_link(
        R"link(\[(?:\\.|[^\[\]\\])*\]\(\s*(?:<(https?://[^<>\s]+)>|(https?://(?:\\.|[^\s()<>\\]|\([^\s()<>]*\))+))(?:\s+(?:"[^"]*"|'[^']*'|\([^()]*\)))?\s*\))link",
        std::regex::icase);
    std::smatch link;
    if (std::regex_search(text, link, markdown_link)) {
        Notify(write_clipboard_(link[1].matched ? link[1].str() : link[2].str())
            ? "Copied link address" : ClipboardController::kWriteFailed);
    } else {
        Notify(write_clipboard_(text) ? "Copied paragraph" : ClipboardController::kWriteFailed);
    }
}

ftxui::Element NotesWorkspaceComponent::RenderSidebar() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    auto tab = [&](const std::string& label, bool selected, Box& box) {
        const ButtonSpec spec = TabButtonSpec(label, selected, "", ButtonSize::Compact);
        return RenderModalFlatButton(theme, spec, selected) | reflect(box);
    };
    Elements rows = {
        RenderWorkspaceTabs(theme, WorkspaceTab::Notes,
            documents_tab_box_, notes_tab_box_, actions_tab_box_),
        separator() | color(theme.gutter),
        hbox({
            tab("Notes", !sidebar_content_selected_, sidebar_notes_tab_box_),
            text(" "),
            tab("Content", sidebar_content_selected_, sidebar_content_tab_box_),
        }),
        separator() | color(theme.gutter),
    };
    auto render = [&] {
        return vbox(std::move(rows)) | borderStyled(LIGHT, theme.gutter) |
            size(WIDTH, EQUAL, kSidebarWidth) | reflect(sidebar_box_) |
            bgcolor(theme.background);
    };
    if (sidebar_content_selected_) {
        rows.push_back(RenderSidebarContents(theme));
        return render();
    }
    sidebar_row_boxes_.resize(sidebar_entries_.size());
    for (size_t i = 0; i < sidebar_entries_.size(); ++i) {
        const auto& entry = sidebar_entries_[i];
        std::string label = "  ";
        if (entry.project) label += entry.orphaned ? "! " : "P ";
        label += entry.label;
        if (entry.kind != SidebarEntryKind::NewSection) label += std::string(std::max(1, 22 - static_cast<int>(label.size())), ' ') + std::to_string(entry.count);
        Element row = text(label) | color(theme.foreground) | reflect(sidebar_row_boxes_[i]);
        if (entry.project) row = row | color(entry.orphaned ? theme.button_danger : theme.modal_accent);
        if (static_cast<int>(i) == selected_sidebar_) row = row | bgcolor(theme.modal_selected_item_bg) | color(theme.modal_selected_item_fg);
        if (entry.kind == SidebarEntryKind::NewSection) row = row | color(theme.modal_accent);
        rows.push_back(row);
    }
    if (focus_ == FocusArea::GroupName) {
        rows.push_back(text(" " + std::string(editing_section_id_ ? "Rename: " : "New: ") + group_name_input_ + "▌") |
            borderStyled(LIGHT, theme.modal_accent));
    }
    rows.push_back(filler());
    footer_new_note_box_ = {};
    footer_delete_note_box_ = {};
    footer_restore_note_box_ = {};
    footer_permanent_delete_box_ = {};
    footer_empty_trash_box_ = {};
    footer_new_group_box_ = {};
    footer_rename_group_box_ = {};
    footer_delete_group_box_ = {};
    footer_project_notes_box_ = {};
    footer_orphan_general_box_ = {};
    footer_orphan_relink_box_ = {};
    const bool can_delete_note = editing_
        ? session_.Note() && !session_.Note()->deleted_at
        : !trash_selected_ && !visible_notes_.empty();
    const bool section_selected = selected_sidebar_ >= 0 &&
        selected_sidebar_ < static_cast<int>(sidebar_entries_.size()) &&
        sidebar_entries_[selected_sidebar_].kind == SidebarEntryKind::Section;
    const bool project_group_selected = section_selected &&
        sidebar_entries_[selected_sidebar_].project;
    const bool orphaned_group_selected = section_selected &&
        sidebar_entries_[selected_sidebar_].orphaned;
    const bool custom_group_selected = section_selected && !project_group_selected;
    auto footer_button = [&](const std::string& label, ButtonRole role, bool enabled, Box& box) {
        ButtonSpec spec = ButtonSpecFromLabel(
            label, role, ButtonVariant::Minimal, ButtonSize::Compact);
        spec.enabled = enabled;
        return RenderModalFlatButton(theme, spec) | reflect(box);
    };
    rows.push_back(separator() | color(theme.gutter));
    rows.push_back(text(" Notes:") | bold | color(theme.foreground));
    if (trash_selected_ && !editing_) {
        const bool has_trash = !visible_notes_.empty();
        rows.push_back(hbox({
            footer_button("Restore", ButtonRole::Success, has_trash, footer_restore_note_box_),
            text(" "),
            footer_button("Delete", ButtonRole::Danger, has_trash, footer_permanent_delete_box_),
        }));
        rows.push_back(footer_button(
            "Empty Trash", ButtonRole::Danger, has_trash, footer_empty_trash_box_));
    } else {
        rows.push_back(hbox({
            footer_button("New", ButtonRole::Primary, true, footer_new_note_box_),
            text(" "),
            footer_button("Delete", ButtonRole::Danger, can_delete_note, footer_delete_note_box_),
        }));
    }
    rows.push_back(text(" Group:") | bold | color(theme.foreground));
    rows.push_back(hbox({
        footer_button("New", ButtonRole::Primary, true, footer_new_group_box_),
        text(" "),
        footer_button("Rename", ButtonRole::Utility, custom_group_selected, footer_rename_group_box_),
        text(" "),
        footer_button("Delete", ButtonRole::Danger,
            custom_group_selected || orphaned_group_selected, footer_delete_group_box_),
    }));
    if (orphaned_group_selected) {
        if (project_context_) {
            rows.push_back(hbox({
                footer_button("To General", ButtonRole::Utility, true, footer_orphan_general_box_),
                text(" "),
                footer_button("Relink", ButtonRole::Primary, true, footer_orphan_relink_box_),
            }));
        } else {
            rows.push_back(footer_button(
                "To General", ButtonRole::Utility, true, footer_orphan_general_box_));
        }
    }
    if (project_context_) {
        rows.push_back(text(" Project:") | bold | color(theme.foreground));
        rows.push_back(hbox({
            footer_button("Open Notes", ButtonRole::Primary, true, footer_project_notes_box_),
        }));
    }
    return render();
}

ftxui::Element NotesWorkspaceComponent::RenderSidebarContents(const Theme& theme) {
    using namespace ftxui;
    heading_blocks_.clear();
    heading_boxes_.clear();
    const auto* note = editing_ ? session_.Note() : nullptr;
    if (note) {
        for (size_t i = 0; i < note->blocks.size(); ++i) {
            const int heading = note->blocks[i].heading;
            if (heading == 1 || heading == 2) heading_blocks_.push_back(i);
        }
    }
    if (heading_blocks_.empty()) {
        return vbox({
            text(note ? " No H1 or H2 headings yet." : " Open a note to see"),
            note ? emptyElement() : text(" its H1 and H2 headings."),
            filler(),
        }) | color(theme.foreground) | dim | yflex | reflect(sidebar_content_box_);
    }

    heading_scroll_ = std::min(heading_scroll_, heading_blocks_.size() - 1);
    heading_boxes_.resize(heading_blocks_.size() - heading_scroll_);
    Elements rows;
    for (size_t i = heading_scroll_; i < heading_blocks_.size(); ++i) {
        const size_t block_index = heading_blocks_[i];
        const auto& block = note->blocks[block_index];
        const std::string indent(block.heading == 1 ? 2 : 4, ' ');
        const size_t width = kSidebarWidth - 3 - indent.size();
        std::string label = TrimWhitespace(BlockText(block));
        if (label.empty()) label = "Untitled heading";
        const auto first = utils::BuildUtf8WrapSegmentsLimited(label, width, 1).front();
        Elements lines = {text(indent + label.substr(0, first.end))};
        if (first.end < label.size()) {
            lines.push_back(text(indent + FitText(
                TrimWhitespace(label.substr(first.end)), width)));
        }
        if (i + 1 == heading_blocks_.size()) {
            last_heading_height_ = static_cast<int>(lines.size());
        }
        Element row = vbox(std::move(lines)) | color(theme.foreground);
        if (block.heading == 1) row = row | bold;
        if (session_.Cursor().block == block_index) {
            row = row | bgcolor(theme.modal_selected_item_bg) |
                color(theme.modal_selected_item_fg);
        }
        rows.push_back(row | reflect(heading_boxes_[i - heading_scroll_]));
    }
    return vbox({vbox(std::move(rows)), filler()}) | yframe | yflex |
        reflect(sidebar_content_box_);
}

ftxui::Element NotesWorkspaceComponent::RenderCard(
    NoteDocument& note, bool selected, size_t visible_index, int card_width) {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    const size_t content_width = static_cast<size_t>(
        std::max(1, card_width - 2));
    Elements lines;

    std::string title = note.title.empty() ? "Untitled note" : note.title;
    if (note.pinned) title = "◆ " + title;
    lines.push_back(
        text(FitText(title, content_width)) | bold | color(theme.modal_accent));

    size_t shown = 0;
    for (const auto& block : note.blocks) {
        if (shown++ >= 4) break;
        std::string prefix;
        if (block.type == NoteBlockType::BulletItem) prefix = "• ";
        else if (block.type == NoteBlockType::NumberedItem) {
            prefix = std::to_string(shown) + ". ";
        } else if (block.type == NoteBlockType::CheckItem) {
            prefix = block.checked ? "[x] " : "[ ] ";
        }
        const size_t prefix_width = textlt::utils::Utf8DisplayWidth(prefix);
        const size_t value_width = content_width > prefix_width
            ? content_width - prefix_width : 0;
        lines.push_back(text(
            prefix + FitText(BlockText(block), value_width)) |
            color(theme.foreground));
    }
    while (lines.size() < 5) lines.push_back(text(""));

    const std::string date = note.updated_at.substr(0, 10) + " ";
    const size_t date_width = textlt::utils::Utf8DisplayWidth(date);
    const size_t section_width = content_width > date_width
        ? content_width - date_width : 0;
    lines.push_back(hbox({
        text(FitText(" " + SectionName(note), section_width)) | dim,
        filler(),
        text(FitText(date, content_width)) | dim,
    }));

    Element card = vbox(std::move(lines)) |
        borderStyled(selected ? HEAVY : LIGHT,
            selected ? theme.modal_accent : theme.gutter) |
        bgcolor(theme.background) |
        size(WIDTH, EQUAL, card_width);
    if (visible_index < card_boxes_.size()) {
        card = card | reflect(card_boxes_[visible_index]);
    }
    return card;
}

ftxui::Element NotesWorkspaceComponent::RenderOverview() {
    using namespace ftxui; const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    std::string heading = trash_selected_ ? "Trash" : selected_section_id_.empty() ? "Home" : sidebar_entries_[selected_sidebar_].label;
    Element search = text(" Search: " + search_ + (focus_ == FocusArea::Search ? "▌" : "")) | borderStyled(LIGHT, focus_ == FocusArea::Search ? theme.modal_accent : theme.gutter) | reflect(search_box_) | xflex;
    const std::string sort_name = sort_mode_ == SortMode::Updated ? "Updated" :
        sort_mode_ == SortMode::Created ? "Created" :
        sort_mode_ == SortMode::Title ? "Title" : "Pinned";
    Element sort = text(" Sort: " + sort_name + " ↻ ") | reflect(sort_box_) |
        color(theme.foreground) | borderStyled(LIGHT, theme.gutter);
    Element add = vbox({
        text(""),
        text(" + New Note ") | bold,
    }) | color(theme.modal_selected_item_fg) |
        bgcolor(theme.modal_selected_item_bg) | reflect(new_note_box_);
    Elements rows;
    Elements current_row;
    card_boxes_.resize(visible_notes_.size());
    const int available_width = std::max(1, overview_box_.x_max - overview_box_.x_min + 1);
    const size_t columns = available_width >= 108 ? 3 : available_width >= 70 ? 2 : 1;
    const int card_width = std::max(
        28, available_width / static_cast<int>(columns));
    auto flush_cards = [&] {
        if (current_row.empty()) return;
        while (current_row.size() < columns) current_row.push_back(emptyElement() | xflex);
        rows.push_back(hbox(std::move(current_row)));
        current_row.clear();
    };
    bool pinned_label = false, others_label = false;
    for (size_t i = 0; i < visible_notes_.size(); ++i) {
        auto& note = repository_.Notes()[visible_notes_[i]];
        if (sort_mode_ == SortMode::Pinned && selected_section_id_.empty() && !trash_selected_) {
            if (note.pinned && !pinned_label) {
                flush_cards();
                rows.push_back(text(" PINNED") | bold | color(theme.modal_accent));
                pinned_label = true;
            }
            if (!note.pinned && !others_label) {
                flush_cards();
                rows.push_back(text(" OTHERS") | bold | color(theme.gutter));
                others_label = true;
            }
        }
        current_row.push_back(
            RenderCard(
                note,
                focus_ == FocusArea::Cards && static_cast<int>(i) == selected_card_,
                i, card_width));
        if (current_row.size() == columns) flush_cards();
    }
    flush_cards();
    if (rows.empty()) rows.push_back(vbox({filler(), text(trash_selected_ ? "Trash is empty" : "No notes here. Use New to create one.") | center | dim, filler()}) | flex);
    return vbox({
        hbox({
            text(" " + heading + " ") | bold | color(theme.modal_accent),
            text(std::to_string(visible_notes_.size()) + " notes ") | dim,
            search, text(" "), sort, text(" "), add,
        }),
        text(""),
        vbox(std::move(rows)) | frame | yflex,
    }) | flex | bgcolor(theme.background) | reflect(overview_box_);
}

ftxui::Element NotesWorkspaceComponent::ToolbarItem(const std::string& label, bool active, ftxui::Box& box) {
    const Theme& theme = theme_ ? *theme_ : FallbackTheme(); ftxui::Element item = ftxui::text(" " + label + " ") | ftxui::reflect(box);
    return active ? item | ftxui::bold | ftxui::bgcolor(theme.modal_selected_item_bg) | ftxui::color(theme.modal_selected_item_fg) : item | ftxui::color(theme.foreground);
}

void NotesWorkspaceComponent::ResetEditorViewport() {
    context_menu_.Close();
    heading_scroll_ = 0;
    heading_blocks_.clear();
    heading_boxes_.clear();
    editor_rows_.clear();
    editor_column_boxes_.clear();
    editor_scroll_ = 0;
    editor_visible_rows_ = 0;
    editor_column_rows_ = 0;
    editor_content_width_ = 0;
    editor_view_known_ = false;
    editor_scrollbar_dragging_ = false;
    editor_mouse_selecting_ = false;
    editor_scrollbar_drag_y_ = 0;
    editor_scrollbar_drag_scroll_ = 0;
    last_editor_cursor_ = {};
    has_last_editor_cursor_ = false;
    editor_view_box_ = {};
    editor_scrollbar_box_ = {};
}

void NotesWorkspaceComponent::BuildEditorRows(size_t width) {
    editor_rows_.clear();
    auto* note = session_.Note();
    if (!note) return;

    width = std::max<size_t>(1, width);
    size_t number = 1;
    for (size_t block_index = 0; block_index < note->blocks.size(); ++block_index) {
        const NoteBlock& block = note->blocks[block_index];
        std::string prefix(
            static_cast<size_t>(std::max(0, block.indent)) * 2, ' ');
        if (block.type == NoteBlockType::BulletItem) {
            prefix += "• ";
        } else if (block.type == NoteBlockType::NumberedItem) {
            prefix += std::to_string(number) + ". ";
            ++number;
        } else if (block.type == NoteBlockType::CheckItem) {
            prefix += block.checked ? "[x] " : "[ ] ";
        }
        if (block.type != NoteBlockType::NumberedItem) number = 1;

        const size_t prefix_width = textlt::utils::Utf8DisplayWidth(prefix);
        const size_t text_width = width > prefix_width ? width - prefix_width : 1;
        const auto segments = textlt::utils::BuildUtf8WrapSegments(
            BlockText(block), text_width);
        const std::string continuation(prefix_width, ' ');

        for (size_t segment_index = 0; segment_index < segments.size(); ++segment_index) {
            EditorRow row;
            row.block = block_index;
            row.start = segments[segment_index].start;
            row.end = segments[segment_index].end;
            row.prefix = segment_index == 0 ? prefix : continuation;
            row.last = segment_index + 1 == segments.size();
            editor_rows_.push_back(std::move(row));
        }
    }
}

std::optional<size_t> NotesWorkspaceComponent::EditorRowForPosition(
    const NotePosition& position) const {
    std::optional<size_t> fallback;
    for (size_t index = 0; index < editor_rows_.size(); ++index) {
        const EditorRow& row = editor_rows_[index];
        if (row.block != position.block) continue;
        fallback = index;
        if (position.byte < row.end) return index;
        if (row.last && position.byte <= row.end) return index;
    }
    return fallback;
}

std::optional<NotePosition> NotesWorkspaceComponent::NotePositionAt(int x, int y) const {
    auto* note = session_.Note();
    if (!note || editor_rows_.empty() || !editor_view_box_.Contain(x, y)) {
        return std::nullopt;
    }

    size_t column_index = 0;
    int column_x_min = editor_view_box_.x_min +
        static_cast<int>(kEditorLeftPadding);
    int column_y_min = editor_view_box_.y_min +
        static_cast<int>(kEditorTopPadding);
    size_t column_rows = static_cast<size_t>(std::max(
        1, editor_view_box_.y_max - editor_view_box_.y_min + 1 -
            static_cast<int>(kEditorTopPadding)));

    if (!editor_column_boxes_.empty()) {
        bool found = false;
        for (size_t index = 0; index < editor_column_boxes_.size(); ++index) {
            if (editor_column_boxes_[index].Contain(x, y)) {
                column_index = index;
                found = true;
                break;
            }
        }
        if (!found) {
            // Separators and padding do not belong to a column. Pick the
            // closest column so clicks on a boundary keep working.
            int best_distance = std::numeric_limits<int>::max();
            for (size_t index = 0; index < editor_column_boxes_.size(); ++index) {
                const ftxui::Box& box = editor_column_boxes_[index];
                const int distance = x < box.x_min ? box.x_min - x
                    : x > box.x_max ? x - box.x_max : 0;
                if (distance < best_distance) {
                    best_distance = distance;
                    column_index = index;
                }
            }
        }

        const ftxui::Box& box = editor_column_boxes_[column_index];
        column_x_min = box.x_min;
        column_y_min = box.y_min;
        if (editor_column_rows_ > 0) {
            column_rows = editor_column_rows_;
        } else {
            column_rows = static_cast<size_t>(std::max(
                1, box.y_max - box.y_min + 1));
        }
    }

    const size_t row_offset = static_cast<size_t>(std::clamp(
        y - column_y_min, 0, static_cast<int>(column_rows) - 1));
    const size_t row_index = std::min(
        static_cast<size_t>(std::max(0, editor_scroll_)) +
            column_index * column_rows + row_offset,
        editor_rows_.size() - 1);
    const EditorRow& row = editor_rows_[row_index];
    if (row.block >= note->blocks.size()) return std::nullopt;

    const int prefix_width = static_cast<int>(
        textlt::utils::Utf8DisplayWidth(row.prefix));
    const int content_x_min = column_x_min + prefix_width;
    if (x <= content_x_min) return NotePosition{row.block, row.start};

    const std::string text = BlockText(note->blocks[row.block]);
    const size_t columns = static_cast<size_t>(
        std::max(0, x - content_x_min));
    size_t byte = textlt::utils::Utf8ByteIndexAtDisplayColumn(
        text, row.start, columns);
    byte = std::clamp(byte, row.start, row.end);
    return NotePosition{row.block, byte};
}

void NotesWorkspaceComponent::ScrollEditor(int delta) {
    const size_t total_rows = editor_rows_.empty() && session_.Note()
        ? session_.Note()->blocks.size()
        : editor_rows_.size();
    const size_t page_rows = std::max<size_t>(1, editor_visible_rows_);
    const size_t max_scroll = total_rows > page_rows
        ? total_rows - page_rows : 0;
    const long long target = static_cast<long long>(editor_scroll_) + delta;
    editor_scroll_ = static_cast<int>(std::clamp<long long>(
        target, 0, static_cast<long long>(max_scroll)));
}

void NotesWorkspaceComponent::EnsureEditorCursorVisible(size_t page_rows) {
    if (!session_.Note() || editor_rows_.empty() || page_rows == 0) return;

    const NotePosition cursor = session_.Cursor();
    if (has_last_editor_cursor_ && cursor == last_editor_cursor_) return;

    const auto cursor_row = EditorRowForPosition(cursor);
    if (cursor_row) {
        if (*cursor_row < static_cast<size_t>(editor_scroll_)) {
            editor_scroll_ = static_cast<int>(*cursor_row);
        } else if (*cursor_row >= static_cast<size_t>(editor_scroll_) + page_rows) {
            editor_scroll_ = static_cast<int>(
                *cursor_row + 1 - page_rows);
        }
    }
    last_editor_cursor_ = cursor;
    has_last_editor_cursor_ = true;
}

ftxui::Element NotesWorkspaceComponent::RenderEditorRow(const EditorRow& row) {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    auto* note = session_.Note();
    if (!note || row.block >= note->blocks.size()) return text("");

    const NoteBlock& block = note->blocks[row.block];
    const int heading = std::clamp(block.heading, 0, 3);
    const bool is_heading = heading > 0;
    const ftxui::Color heading_fg = heading == 1
        ? theme.modal_selected_item_fg
        : heading == 2 ? theme.syntax_keyword : theme.syntax_type;
    const std::string value = BlockText(block);
    const bool has_selection = session_.HasSelection();
    const auto range = session_.Selection();
    const NotePosition cursor = session_.Cursor();

    Elements elements;
    elements.push_back(text(row.prefix) |
        color(is_heading ? heading_fg : theme.gutter));

    size_t byte = 0;
    for (const auto& run : block.runs) {
        const size_t run_start = byte;
        const size_t run_end = byte + run.text.size();
        const size_t from = std::max(row.start, run_start);
        const size_t to = std::min(row.end, run_end);
        if (from < to) {
            size_t local = from - run_start;
            const size_t local_end = to - run_start;
            while (local < local_end) {
                size_t next = textlt::utils::NextUtf8CodepointStart(run.text, local);
                if (next > local_end) next = local_end;
                const size_t absolute = run_start + local;
                Element glyph = text(run.text.substr(local, next - local)) |
                    color(is_heading ? heading_fg : theme.editor_text);
                if (is_heading) glyph = glyph | bold;
                if (has_selection &&
                    !NoteSession::Before({row.block, absolute}, range.first) &&
                    NoteSession::Before({row.block, absolute}, range.second)) {
                    glyph = glyph | bgcolor(theme.selection_bg) |
                        color(theme.selection_fg);
                }
                if (!has_selection && row.block == cursor.block && absolute == cursor.byte) {
                    glyph = glyph | inverted;
                }
                if (!note_search_matches_.empty()) {
                    for (size_t match_index = 0;
                         match_index < note_search_matches_.size(); ++match_index) {
                        const auto& match = note_search_matches_[match_index];
                        if (match.block != row.block || absolute < match.byte ||
                            absolute >= match.byte + match.length) {
                            continue;
                        }
                        glyph = glyph |
                            bgcolor(match_index == note_search_active_
                                ? theme.active_match_bg : theme.match_bg) |
                            color(theme.selection_fg);
                        break;
                    }
                }
                elements.push_back(std::move(glyph));
                local = next;
            }
        }
        byte = run_end;
    }

    if (!has_selection && row.last && row.block == cursor.block &&
        cursor.byte == value.size()) {
        elements.push_back(text(" ") | inverted);
    }
    if (heading == 1) {
        // H1 behaves like an action-title row: bold text on a filled accent
        // background spanning the whole column width.
        elements.push_back(filler());
        return hbox(std::move(elements)) | bold |
            bgcolor(theme.modal_selected_item_bg) | xflex;
    }
    return hbox(std::move(elements));
}

ftxui::Element NotesWorkspaceComponent::RenderEditorScrollbar(
    size_t viewport_rows, size_t page_rows, size_t total_rows, size_t scroll,
    const Theme& theme) {
    using namespace ftxui;
    const EditorScrollMetrics metrics = ComputeEditorScrollMetrics(
        viewport_rows, page_rows, total_rows);
    Elements cells;
    cells.reserve(metrics.viewport_rows);

    const size_t thumb_top = metrics.needs_scrollbar &&
        metrics.max_scroll > 0 && metrics.track_space > 0
        ? (scroll * metrics.track_space) / metrics.max_scroll : 0;

    for (size_t row = 0; row < metrics.viewport_rows; ++row) {
        if (!metrics.needs_scrollbar) {
            cells.push_back(text(std::string(kEditorScrollbarColumns, ' ')) |
                bgcolor(theme.background));
        } else if (row >= thumb_top &&
            row < thumb_top + metrics.thumb_height) {
            cells.push_back(text(std::string(kEditorScrollbarColumns, ' ')) |
                bgcolor(theme.modal_selected_item_bg) |
                color(theme.modal_selected_item_fg));
        } else {
            cells.push_back(text("│ ") | color(theme.gutter) | dim);
        }
    }
    return vbox(std::move(cells)) | reflect(editor_scrollbar_box_);
}

ftxui::Element NotesWorkspaceComponent::RenderEditor() {
    using namespace ftxui; const Theme& theme = theme_ ? *theme_ : FallbackTheme(); auto* note = session_.Note(); if (!note) return text("No note");
    if (editor_view_box_.x_max > editor_view_box_.x_min ||
        editor_view_box_.y_max > editor_view_box_.y_min) {
        editor_view_known_ = true;
    }
    size_t content_width = 78;
    if (editor_view_known_) {
        // The reflected text area excludes the scrollbar, so subtract only the
        // left padding and the part of the right padding before the scrollbar.
        const size_t text_right_gap = kEditorRightPadding > kEditorScrollbarColumns
            ? kEditorRightPadding - kEditorScrollbarColumns : 0;
        const size_t inner_width = static_cast<size_t>(std::max(
            0, editor_view_box_.x_max - editor_view_box_.x_min + 1));
        const size_t horizontal_padding = kEditorLeftPadding + text_right_gap;
        content_width = inner_width > horizontal_padding
            ? inner_width - horizontal_padding : 1;
    } else {
        // Estimate from the terminal before the editor reflects its first box,
        // so the header layout does not have to wait a frame to settle.
        const int sidebar_width = sidebar_visible_ ? kSidebarWidth + kSidebarGap : 0;
        const int editor_border = 2;
        const int estimate = ftxui::Terminal::Size().dimx - sidebar_width -
            editor_border - static_cast<int>(kEditorLeftPadding + kEditorRightPadding);
        if (estimate > 0) content_width = static_cast<size_t>(estimate);
    }
    const size_t column_count = static_cast<size_t>(
        std::clamp(editor_columns_, 1, 3));
    const size_t separator_width = column_count > 1 ? column_count - 1 : 0;
    // Keep one blank cell between each separator and the column to its right.
    const size_t column_gap = column_count > 1 ? column_count - 1 : 0;
    const size_t column_overhead = separator_width + column_gap;
    const size_t column_text_width = content_width > column_overhead
        ? std::max<size_t>(1,
            (content_width - column_overhead) / column_count)
        : 1;

    if (column_text_width != editor_content_width_) has_last_editor_cursor_ = false;
    BuildEditorRows(column_text_width);
    editor_content_width_ = column_text_width;

    size_t column_rows = 0;
    if (editor_view_known_) {
        const size_t view_height = static_cast<size_t>(std::max(
            0, editor_view_box_.y_max - editor_view_box_.y_min + 1));
        column_rows = view_height > kEditorTopPadding
            ? view_height - kEditorTopPadding : 0;
    }
    const size_t total_rows = editor_rows_.size();
    if (column_rows == 0) column_rows = std::max<size_t>(1, total_rows);
    const size_t page_rows = column_rows * column_count;
    const EditorScrollMetrics metrics = ComputeEditorScrollMetrics(
        column_rows, page_rows, total_rows);
    if (page_rows != editor_visible_rows_ && !note_search_matches_.empty()) {
        has_last_editor_cursor_ = false;
    }
    EnsureEditorCursorVisible(page_rows);
    editor_scroll_ = static_cast<int>(std::min<size_t>(
        static_cast<size_t>(std::max(0, editor_scroll_)), metrics.max_scroll));
    editor_visible_rows_ = page_rows;
    editor_column_rows_ = column_rows;

    Elements columns;
    editor_column_boxes_.resize(column_count);
    const size_t scroll = static_cast<size_t>(std::max(0, editor_scroll_));
    for (size_t column = 0; column < column_count; ++column) {
        Elements rows;
        const size_t column_start = scroll + column * column_rows;
        const size_t column_end = std::min(
            total_rows, column_start + column_rows);
        for (size_t index = column_start; index < column_end; ++index) {
            rows.push_back(RenderEditorRow(editor_rows_[index]));
        }
        while (rows.size() < column_rows) rows.push_back(text(""));
        columns.push_back(
            vbox(std::move(rows)) |
            size(WIDTH, EQUAL, static_cast<int>(column_text_width)) |
            reflect(editor_column_boxes_[column]));
        if (column + 1 < column_count) {
            columns.push_back(separatorCharacter("·") | color(theme.gutter));
            // One blank cell before the next column's text.
            columns.push_back(text(" "));
        }
    }
    columns.push_back(filler());

    const size_t text_right_gap = kEditorRightPadding > kEditorScrollbarColumns
        ? kEditorRightPadding - kEditorScrollbarColumns : 0;
    const Elements top_padding(kEditorTopPadding, text(""));
    Element text_area = hbox({
        text(std::string(kEditorLeftPadding, ' ')),
        vbox({
            vbox(top_padding),
            hbox(std::move(columns)),
        }) | xflex,
        text(std::string(text_right_gap, ' ')),
    }) | yflex | reflect(editor_view_box_);
    Element scrollbar = vbox({
        vbox(top_padding),
        RenderEditorScrollbar(
            column_rows, page_rows, total_rows,
            static_cast<size_t>(editor_scroll_), theme),
    });
    Element view = hbox({std::move(text_area) | xflex, std::move(scrollbar)}) | yflex;

    const std::string back_label = " < Notes ";
    const std::string pin_label = note->pinned ? " ◆ Pinned " : " ◇ Pin ";
    const std::string group_label = " Group: " + SectionName(*note) + " ▾ ";
    const std::string trash_label = note->deleted_at ? " Restore " : " Trash ";
    const size_t title_end = textlt::utils::Utf8ByteIndexAtDisplayColumn(
        note->title, 0, kNoteTitleMaxChars);
    std::string shown_title = note->title.substr(0, title_end);
    const size_t shown_title_width = textlt::utils::Utf8DisplayWidth(shown_title);
    if (shown_title_width < kNoteTitleMaxChars) {
        shown_title.append(kNoteTitleMaxChars - shown_title_width, ' ');
    }
    const std::string title_label = " " + shown_title + " ";
    Element back = text(back_label) | bold | reflect(back_box_); Element title = text(title_label) | bold | reflect(title_box_);
    Element pin = text(pin_label) | reflect(pin_box_);
    Element group = text(group_label) | reflect(section_box_); Element trash = text(trash_label) | color(note->deleted_at ? theme.button_success : theme.button_danger) | reflect(trash_box_);
    const int heading = session_.ActiveHeading();
    Element toolbar = hbox({
        ToolbarItem("H1", heading == 1, heading_1_box_),
        ToolbarItem("H2", heading == 2, heading_2_box_),
        ToolbarItem("H3", heading == 3, heading_3_box_),
        separator(),
        ToolbarItem("Copy", false, copy_box_),
        separator(),
        text(" Cols:") | color(theme.foreground),
        ToolbarItem("1", editor_columns_ == 1, col_1_box_),
        ToolbarItem("2", editor_columns_ == 2, col_2_box_),
        ToolbarItem("3", editor_columns_ == 3, col_3_box_),
    });
    // Keep the formatting buttons on the title/Group row when the content area
    // is wide enough for them; otherwise fall back to a dedicated toolbar row.
    const int toolbar_width =
        ToolbarItemWidth("H1") + ToolbarItemWidth("H2") +
        ToolbarItemWidth("H3") + 1 +
        ToolbarItemWidth("Copy") + 1 +
        string_width(" Cols:") + ToolbarItemWidth("1") +
        ToolbarItemWidth("2") + ToolbarItemWidth("3");
    const int header_width = string_width(back_label) + string_width(title_label) +
        string_width(pin_label) + string_width(group_label) + string_width(trash_label) + 4;
    const bool inline_toolbar = header_width + toolbar_width <= static_cast<int>(content_width) + 4;
    Element header = inline_toolbar
        ? hbox({back, separator(), title, separator(), group, separator(), toolbar, filler(), pin, separator(), trash})
        : hbox({back, separator(), title, separator(), group, filler(), pin, separator(), trash});
    Element status = hbox({text(" Created " + note->created_at + "  Updated " + note->updated_at) | dim, filler(), text(" " + save_status_ + " ") | color(theme.modal_accent)});
    Elements layout = {header};
    if (!inline_toolbar) layout.push_back(toolbar | borderStyled(LIGHT, theme.gutter));
    layout.push_back(std::move(view) | borderStyled(focus_ == FocusArea::Body ? HEAVY : LIGHT, focus_ == FocusArea::Body ? theme.modal_accent : theme.gutter) | yflex);
    layout.push_back(status);
    return vbox(std::move(layout)) | flex | bgcolor(theme.background);
}

ftxui::Element NotesWorkspaceComponent::OnRender() {
    using namespace ftxui;
    Element content = editing_ ? RenderEditor() : RenderOverview();
    Element body = sidebar_visible_
        ? hbox({
              RenderSidebar(),
              emptyElement() | size(WIDTH, EQUAL, kSidebarGap),
              std::move(content),
          }) | flex
        : hbox({std::move(content)}) | flex;
    if (confirm_action_ != ConfirmAction::None) {
        return dbox({body, vbox({filler(), hbox({filler(), RenderConfirmationPopup(), filler()}), filler()})});
    }
    if (group_picker_open_) {
        return dbox({body, vbox({filler(), hbox({filler(), RenderGroupPicker(), filler()}), filler()})});
    }
    if (title_editor_open_) {
        return dbox({body, vbox({filler(), hbox({filler(), RenderTitleEditor(), filler()}), filler()})});
    }
    if (context_menu_.IsOpen()) return context_menu_.Overlay(std::move(body)) | flex;
    return body;
}

std::string NotesWorkspaceComponent::StatusText() const { return save_status_; }

} // namespace textlt::notes
