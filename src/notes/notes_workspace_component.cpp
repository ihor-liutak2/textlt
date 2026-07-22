#include "notes/notes_workspace_component.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

#include "editor_utils.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"

namespace textlt::notes {
namespace {

bool IsCtrl(const ftxui::Event& event, char key) {
    const std::string lower(1, key);
    const std::string upper(1, static_cast<char>(std::toupper(static_cast<unsigned char>(key))));
    return event.input() == "Ctrl+" + upper || event.input() == std::string(1, static_cast<char>(key - 'a' + 1)) || event == ftxui::Event::Special("Ctrl+" + upper);
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

ftxui::Element ApplyRunStyle(ftxui::Element element, uint8_t marks) {
    if (marks & MarkBit(NoteMark::Bold)) element = element | ftxui::bold;
    if (marks & MarkBit(NoteMark::Italic)) element = element | ftxui::italic;
    if (marks & MarkBit(NoteMark::Underlined)) element = element | ftxui::underlined;
    if (marks & MarkBit(NoteMark::Strikethrough)) element = element | ftxui::strikethrough;
    return element;
}

} // namespace

NotesWorkspaceComponent::NotesWorkspaceComponent(
    const Theme* theme,
    StatusCallback on_status,
    DocumentsCallback on_documents,
    ClipboardReadCallback read_clipboard,
    ClipboardWriteCallback write_clipboard,
    std::filesystem::path root)
    : theme_(theme),
      on_status_(std::move(on_status)),
      on_documents_(std::move(on_documents)),
      read_clipboard_(std::move(read_clipboard)),
      write_clipboard_(std::move(write_clipboard)),
      repository_(std::move(root)),
      session_(nullptr) {
    repository_.Load(load_warning_); RebuildSidebar(); RebuildVisibleNotes();
}

void NotesWorkspaceComponent::Open() { TakeFocus(); if (!load_warning_.empty()) Notify("Notes warning: " + load_warning_); }

void NotesWorkspaceComponent::Notify(const std::string& message) { save_status_ = message; if (on_status_) on_status_(message); }

std::optional<size_t> NotesWorkspaceComponent::ActiveNoteIndex() const {
    if (!active_note_id_) return std::nullopt;
    const auto& notes = repository_.Notes();
    for (size_t index = 0; index < notes.size(); ++index) if (notes[index].id == *active_note_id_) return index;
    return std::nullopt;
}

std::string NotesWorkspaceComponent::SectionName(const NoteDocument& note) const {
    if (!note.section_id) return "Home";
    for (const auto& section : repository_.Sections()) if (section.id == *note.section_id) return section.name;
    return "Home";
}

void NotesWorkspaceComponent::RebuildSidebar() {
    sidebar_entries_.clear();
    size_t home_count = 0, trash_count = 0;
    for (const auto& note : repository_.Notes()) note.deleted_at ? ++trash_count : ++home_count;
    sidebar_entries_.push_back({SidebarEntryKind::Home, "", "Home", home_count});
    for (const auto& section : repository_.Sections()) {
        size_t count = 0; for (const auto& note : repository_.Notes()) if (!note.deleted_at && note.section_id && *note.section_id == section.id) ++count;
        sidebar_entries_.push_back({SidebarEntryKind::Section, section.id, section.name, count});
    }
    sidebar_entries_.push_back({SidebarEntryKind::NewSection, "", "+ New group", 0});
    sidebar_entries_.push_back({SidebarEntryKind::Trash, "", "Trash", trash_count});
    selected_sidebar_ = std::clamp(selected_sidebar_, 0, std::max(0, static_cast<int>(sidebar_entries_.size()) - 1));
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
        if (selected_section_id_.empty() && !trash_selected_ && notes[left].pinned != notes[right].pinned) return notes[left].pinned > notes[right].pinned;
        return notes[left].updated_at > notes[right].updated_at;
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
    editing_section_id_ = entry.id;
    group_name_input_ = entry.label;
    group_name_cursor_ = group_name_input_.size();
    focus_ = FocusArea::GroupName;
}

void NotesWorkspaceComponent::DeleteSelectedSection() {
    if (selected_sidebar_ < 0 || selected_sidebar_ >= static_cast<int>(sidebar_entries_.size())) return;
    const SidebarEntry entry = sidebar_entries_[selected_sidebar_];
    if (entry.kind != SidebarEntryKind::Section) return;
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
    editing_ = true; session_.SetNote(&repository_.Notes().front()); title_cursor_ = 0; focus_ = FocusArea::Title;
    std::string error; if (!repository_.Save(repository_.Notes().front(), error)) Notify("Note save failed: " + error); else Notify("New note");
    RebuildSidebar(); RebuildVisibleNotes();
}

void NotesWorkspaceComponent::OpenNote(size_t note_index) {
    if (note_index >= repository_.Notes().size()) return; active_note_id_ = repository_.Notes()[note_index].id; editing_ = true;
    session_.SetNote(&repository_.Notes()[note_index]); title_cursor_ = repository_.Notes()[note_index].title.size(); focus_ = FocusArea::Body;
}

void NotesWorkspaceComponent::OpenSelectedNote() { if (!visible_notes_.empty()) OpenNote(visible_notes_[selected_card_]); }

bool NotesWorkspaceComponent::Save(std::string& error) {
    if (!session_.Note() || !session_.Dirty()) return true;
    if (!repository_.Save(*session_.Note(), error)) { Notify("Save failed: " + error); return false; }
    session_.MarkSaved(); last_saved_ = std::chrono::steady_clock::now(); save_status_ = "Saved"; RebuildSidebar(); RebuildVisibleNotes(); return true;
}

void NotesWorkspaceComponent::TouchAndSave() { std::string error; Save(error); }

void NotesWorkspaceComponent::CloseEditor() { TouchAndSave(); editing_ = false; session_.SetNote(nullptr); active_note_id_.reset(); RebuildSidebar(); RebuildVisibleNotes(); focus_ = FocusArea::Cards; }

void NotesWorkspaceComponent::TogglePin() {
    if (!session_.Note() || session_.Note()->deleted_at) return; auto& note = *session_.Note(); note.pinned = !note.pinned;
    if (note.pinned) note.pinned_at = UtcNow(); else note.pinned_at.reset(); note.updated_at = UtcNow(); ++note.revision;
    std::string error; if (!repository_.Save(note, error)) Notify("Pin save failed: " + error); else Notify(note.pinned ? "Note pinned" : "Note unpinned");
}

void NotesWorkspaceComponent::MoveActiveNoteToNextSection() {
    if (!session_.Note() || session_.Note()->deleted_at) return; auto& note = *session_.Note(); const auto& sections = repository_.Sections();
    if (sections.empty()) note.section_id.reset();
    else if (!note.section_id) note.section_id = sections.front().id;
    else { auto found = std::find_if(sections.begin(), sections.end(), [&](const auto& value) { return value.id == *note.section_id; }); if (found == sections.end() || ++found == sections.end()) note.section_id.reset(); else note.section_id = found->id; }
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
    if (event.is_character() && !event.input().empty() && static_cast<unsigned char>(event.input()[0]) >= 0x20) { value.insert(cursor, event.input()); cursor += event.input().size(); on_change(); return true; }
    return false;
}

bool NotesWorkspaceComponent::HandleOverviewEvent(ftxui::Event event) {
    if (focus_ == FocusArea::GroupName) {
        if (event == ftxui::Event::Return) { FinishCreateSection(); return true; }
        if (event == ftxui::Event::Escape) { focus_ = FocusArea::Sidebar; group_name_input_.clear(); editing_section_id_.reset(); return true; }
        return HandleTextFieldEvent(group_name_input_, group_name_cursor_, event, [] {});
    }
    if (event == ftxui::Event::Tab) { focus_ = focus_ == FocusArea::Sidebar ? FocusArea::Search : focus_ == FocusArea::Search ? FocusArea::Cards : FocusArea::Sidebar; return true; }
    if (focus_ == FocusArea::Search) {
        if (event == ftxui::Event::Escape) { search_.clear(); search_cursor_ = 0; RebuildVisibleNotes(); focus_ = FocusArea::Cards; return true; }
        return HandleTextFieldEvent(search_, search_cursor_, event, [this] { RebuildVisibleNotes(); });
    }
    if (focus_ == FocusArea::Sidebar) {
        if (event == ftxui::Event::ArrowUp) { selected_sidebar_ = std::max(0, selected_sidebar_ - 1); return true; }
        if (event == ftxui::Event::ArrowDown) { selected_sidebar_ = std::min(static_cast<int>(sidebar_entries_.size()) - 1, selected_sidebar_ + 1); return true; }
        if (event == ftxui::Event::Return) { SelectSidebarEntry(selected_sidebar_); return true; }
        if (event.input() == "g" || event.input() == "G") { StartCreateSection(); return true; }
        if (event.input() == "r" || event.input() == "R") { StartRenameSection(); return true; }
        if (IsDelete(event)) { DeleteSelectedSection(); return true; }
    } else {
        if (event == ftxui::Event::ArrowLeft || event == ftxui::Event::ArrowUp) { selected_card_ = std::max(0, selected_card_ - 1); return true; }
        if (event == ftxui::Event::ArrowRight || event == ftxui::Event::ArrowDown) { selected_card_ = std::min(static_cast<int>(visible_notes_.size()) - 1, selected_card_ + 1); return true; }
        if (event == ftxui::Event::Return) { OpenSelectedNote(); return true; }
    }
    return false;
}

bool NotesWorkspaceComponent::HandleEditorEvent(ftxui::Event event) {
    if (event == ftxui::Event::Escape) { CloseEditor(); return true; }
    if (IsCtrl(event, 's')) { TouchAndSave(); return true; }
    if (IsCtrl(event, 'b')) { ToggleMark(NoteMark::Bold); return true; }
    if (IsCtrl(event, 'i')) { ToggleMark(NoteMark::Italic); return true; }
    if (IsCtrl(event, 'u')) { ToggleMark(NoteMark::Underlined); return true; }
    if (event.input() == "Ctrl+Shift+X" || event.input() == "\x1B[88;6u") {
        ToggleMark(NoteMark::Strikethrough);
        return true;
    }
    if (IsCtrl(event, 'a') && focus_ == FocusArea::Body) { session_.SelectAll(); return true; }
    if (IsCtrl(event, 'c') && focus_ == FocusArea::Body) {
        if (write_clipboard_ && session_.HasSelection()) write_clipboard_(session_.SelectedText());
        return true;
    }
    if (IsCtrl(event, 'x') && focus_ == FocusArea::Body) {
        if (session_.HasSelection()) {
            if (write_clipboard_) write_clipboard_(session_.SelectedText());
            session_.DeleteSelection();
            TouchAndSave();
        }
        return true;
    }
    if (IsCtrl(event, 'v') && focus_ == FocusArea::Body) {
        if (read_clipboard_) {
            const std::string value = read_clipboard_();
            if (!value.empty()) {
                session_.Insert(value);
                TouchAndSave();
            }
        }
        return true;
    }
    if (IsCtrl(event, 'z')) { session_.Undo(); TouchAndSave(); return true; }
    if (IsCtrl(event, 'y')) { session_.Redo(); TouchAndSave(); return true; }
    if (event == ftxui::Event::Tab) {
        if (focus_ == FocusArea::Body && session_.Note() &&
            session_.Note()->blocks[session_.Cursor().block].type != NoteBlockType::Paragraph) {
            session_.Indent(1);
            TouchAndSave();
        } else {
            focus_ = focus_ == FocusArea::Title ? FocusArea::Body : FocusArea::Title;
        }
        return true;
    }
    if ((event.input() == "Shift+Tab" || event.input() == "\x1B[Z") && focus_ == FocusArea::Body) {
        session_.Indent(-1);
        TouchAndSave();
        return true;
    }
    if ((event.input() == "Ctrl+Enter" || event.input() == "\x1B[13;5u") &&
        focus_ == FocusArea::Body) {
        if (session_.ToggleCheck()) TouchAndSave();
        return true;
    }
    if (focus_ == FocusArea::Title) {
        if (event == ftxui::Event::Return || event == ftxui::Event::ArrowDown) { focus_ = FocusArea::Body; return true; }
        auto* note = session_.Note(); if (!note) return false;
        return HandleTextFieldEvent(note->title, title_cursor_, event, [this, note] { note->updated_at = UtcNow(); ++note->revision; std::string error; if (!repository_.Save(*note, error)) Notify("Title save failed: " + error); });
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
    if (changed) TouchAndSave(); return changed;
}

bool NotesWorkspaceComponent::OnEvent(ftxui::Event event) {
    if (event.is_mouse() && event.mouse().button == ftxui::Mouse::Left && event.mouse().motion == ftxui::Mouse::Pressed) {
        const auto& mouse = event.mouse();
        if (documents_tab_box_.Contain(mouse.x, mouse.y)) { if (on_documents_) on_documents_(); return true; }
        for (size_t i = 0; i < sidebar_row_boxes_.size(); ++i) if (sidebar_row_boxes_[i].Contain(mouse.x, mouse.y)) { focus_ = FocusArea::Sidebar; SelectSidebarEntry(static_cast<int>(i)); return true; }
        if (new_note_box_.Contain(mouse.x, mouse.y)) { NewNote(); return true; }
        if (search_box_.Contain(mouse.x, mouse.y)) { focus_ = FocusArea::Search; return true; }
        if (!editing_) for (size_t i = 0; i < card_boxes_.size(); ++i) if (card_boxes_[i].Contain(mouse.x, mouse.y)) { selected_card_ = static_cast<int>(i); OpenSelectedNote(); return true; }
        if (editing_) {
            if (back_box_.Contain(mouse.x, mouse.y)) { CloseEditor(); return true; }
            if (pin_box_.Contain(mouse.x, mouse.y)) { TogglePin(); return true; }
            if (section_box_.Contain(mouse.x, mouse.y)) { MoveActiveNoteToNextSection(); return true; }
            if (trash_box_.Contain(mouse.x, mouse.y)) { DeleteOrRestoreActiveNote(); return true; }
            if (bold_box_.Contain(mouse.x, mouse.y)) { ToggleMark(NoteMark::Bold); return true; }
            if (italic_box_.Contain(mouse.x, mouse.y)) { ToggleMark(NoteMark::Italic); return true; }
            if (underline_box_.Contain(mouse.x, mouse.y)) { ToggleMark(NoteMark::Underlined); return true; }
            if (strike_box_.Contain(mouse.x, mouse.y)) { ToggleMark(NoteMark::Strikethrough); return true; }
            if (bullet_box_.Contain(mouse.x, mouse.y)) { SetBlockType(NoteBlockType::BulletItem); return true; }
            if (numbered_box_.Contain(mouse.x, mouse.y)) { SetBlockType(NoteBlockType::NumberedItem); return true; }
            if (check_box_.Contain(mouse.x, mouse.y)) { SetBlockType(NoteBlockType::CheckItem); return true; }
            if (paragraph_box_.Contain(mouse.x, mouse.y)) { SetBlockType(NoteBlockType::Paragraph); return true; }
        }
    }
    return editing_ ? HandleEditorEvent(std::move(event)) : HandleOverviewEvent(std::move(event));
}

void NotesWorkspaceComponent::ToggleMark(NoteMark mark) { if (session_.ToggleMark(mark) && session_.Dirty()) TouchAndSave(); }
void NotesWorkspaceComponent::SetBlockType(NoteBlockType type) {
    if (type == NoteBlockType::CheckItem && session_.Note() &&
        session_.Note()->blocks[session_.Cursor().block].type == NoteBlockType::CheckItem) {
        if (session_.ToggleCheck()) TouchAndSave();
        return;
    }
    if (session_.SetBlockType(type)) TouchAndSave();
}
void NotesWorkspaceComponent::ClearFormatting() { if (session_.ClearFormatting() && session_.Dirty()) TouchAndSave(); }

ftxui::Element NotesWorkspaceComponent::RenderSidebar() {
    using namespace ftxui; const Theme& theme = theme_ ? *theme_ : FallbackTheme(); sidebar_row_boxes_.resize(sidebar_entries_.size()); Elements rows;
    rows.push_back(hbox({
        text(" Documents ") | color(theme.foreground) | reflect(documents_tab_box_),
        text(" Notes ") | bold | bgcolor(theme.modal_selected_item_bg) | color(theme.modal_selected_item_fg),
    }));
    rows.push_back(separator() | color(theme.gutter));
    rows.push_back(text(" NOTES") | bold | color(theme.modal_accent));
    for (size_t i = 0; i < sidebar_entries_.size(); ++i) {
        const auto& entry = sidebar_entries_[i]; std::string label = "  " + entry.label;
        if (entry.kind != SidebarEntryKind::NewSection) label += std::string(std::max(1, 20 - static_cast<int>(entry.label.size())), ' ') + std::to_string(entry.count);
        Element row = text(label) | color(theme.foreground) | reflect(sidebar_row_boxes_[i]);
        if (static_cast<int>(i) == selected_sidebar_) row = row | bgcolor(theme.modal_selected_item_bg) | color(theme.modal_selected_item_fg);
        if (entry.kind == SidebarEntryKind::NewSection) row = row | color(theme.modal_accent);
        rows.push_back(row);
    }
    if (focus_ == FocusArea::GroupName) {
        rows.push_back(text(" " + std::string(editing_section_id_ ? "Rename: " : "New: ") + group_name_input_ + "▌") |
            borderStyled(LIGHT, theme.modal_accent));
    }
    rows.push_back(filler());
    rows.push_back(text(" G New  R Rename  Del Delete") | dim);
    rows.push_back(text(" Ctrl+N  Documents") | dim);
    return vbox(std::move(rows)) | borderStyled(LIGHT, theme.gutter) | size(WIDTH, EQUAL, 28) | reflect(sidebar_box_) | bgcolor(theme.menu_background);
}

ftxui::Element NotesWorkspaceComponent::RenderCard(NoteDocument& note, bool selected, size_t visible_index) {
    using namespace ftxui; const Theme& theme = theme_ ? *theme_ : FallbackTheme(); Elements lines;
    std::string title = note.title.empty() ? "Untitled note" : note.title; if (note.pinned) title = "◆ " + title;
    lines.push_back(text(title) | bold | color(theme.modal_accent));
    size_t shown = 0; for (const auto& block : note.blocks) { if (shown++ >= 4) break; std::string prefix; if (block.type == NoteBlockType::BulletItem) prefix = "• "; else if (block.type == NoteBlockType::NumberedItem) prefix = std::to_string(shown) + ". "; else if (block.type == NoteBlockType::CheckItem) prefix = block.checked ? "[x] " : "[ ] "; std::string value = BlockText(block); if (value.size() > 42) value = value.substr(0, 39) + "..."; lines.push_back(text(prefix + value) | color(theme.foreground)); }
    while (lines.size() < 5) lines.push_back(text(""));
    lines.push_back(hbox({text(" " + SectionName(note)) | dim, filler(), text(note.updated_at.substr(0, 10) + " ") | dim}));
    Element card = vbox(std::move(lines)) | borderStyled(selected ? HEAVY : LIGHT, selected ? theme.modal_accent : theme.gutter) | bgcolor(selected ? theme.menu_background : theme.background) | size(WIDTH, GREATER_THAN, 28);
    if (visible_index < card_boxes_.size()) card = card | reflect(card_boxes_[visible_index]); return card;
}

ftxui::Element NotesWorkspaceComponent::RenderOverview() {
    using namespace ftxui; const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    std::string heading = trash_selected_ ? "Trash" : selected_section_id_.empty() ? "Home" : sidebar_entries_[selected_sidebar_].label;
    Element search = text(" Search: " + search_ + (focus_ == FocusArea::Search ? "▌" : "")) | borderStyled(LIGHT, focus_ == FocusArea::Search ? theme.modal_accent : theme.gutter) | reflect(search_box_) | xflex;
    Element add = text(" + New Note ") | bold | color(theme.modal_selected_item_fg) | bgcolor(theme.modal_selected_item_bg) | reflect(new_note_box_);
    Elements rows;
    Elements current_row;
    card_boxes_.resize(visible_notes_.size());
    const int available_width = std::max(1, overview_box_.x_max - overview_box_.x_min + 1);
    const size_t columns = available_width >= 108 ? 3 : available_width >= 70 ? 2 : 1;
    auto flush_cards = [&] {
        if (current_row.empty()) return;
        while (current_row.size() < columns) current_row.push_back(emptyElement() | xflex);
        rows.push_back(hbox(std::move(current_row)));
        current_row.clear();
    };
    bool pinned_label = false, others_label = false;
    for (size_t i = 0; i < visible_notes_.size(); ++i) {
        auto& note = repository_.Notes()[visible_notes_[i]];
        if (selected_section_id_.empty() && !trash_selected_) {
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
                i) |
            xflex);
        if (current_row.size() == columns) flush_cards();
    }
    flush_cards();
    if (rows.empty()) rows.push_back(vbox({filler(), text(trash_selected_ ? "Trash is empty" : "No notes here. Press Ctrl+N to create one.") | center | dim, filler()}) | flex);
    return vbox({hbox({text(" " + heading + " ") | bold | color(theme.modal_accent), text(std::to_string(visible_notes_.size()) + " notes ") | dim, search, text(" "), add}), separator() | color(theme.gutter), vbox(std::move(rows)) | frame | yflex}) | flex | bgcolor(theme.background) | reflect(overview_box_);
}

ftxui::Element NotesWorkspaceComponent::ToolbarItem(const std::string& label, bool active, ftxui::Box& box) {
    const Theme& theme = theme_ ? *theme_ : FallbackTheme(); ftxui::Element item = ftxui::text(" " + label + " ") | ftxui::reflect(box);
    return active ? item | ftxui::bold | ftxui::bgcolor(theme.modal_selected_item_bg) | ftxui::color(theme.modal_selected_item_fg) : item | ftxui::color(theme.foreground);
}

ftxui::Element NotesWorkspaceComponent::RenderRichBlock(const NoteBlock& block, size_t block_index, size_t number) {
    using namespace ftxui; const Theme& theme = theme_ ? *theme_ : FallbackTheme(); std::string prefix(static_cast<size_t>(block.indent * 2), ' ');
    if (block.type == NoteBlockType::BulletItem) prefix += "• "; else if (block.type == NoteBlockType::NumberedItem) prefix += std::to_string(number) + ". "; else if (block.type == NoteBlockType::CheckItem) prefix += block.checked ? "[x] " : "[ ] ";
    Elements elements = {text(prefix) | color(theme.gutter)}; size_t byte = 0; auto range = session_.Selection();
    for (const auto& run : block.runs) {
        size_t local = 0; while (local < run.text.size()) {
            size_t next = textlt::utils::NextUtf8CodepointStart(run.text, local); const size_t absolute = byte + local;
            Element glyph = ApplyRunStyle(text(run.text.substr(local, next - local)), run.marks) | color(theme.editor_text);
            if (session_.HasSelection() && !NoteSession::Before({block_index, absolute}, range.first) && NoteSession::Before({block_index, absolute}, range.second)) glyph = glyph | bgcolor(theme.selection_bg) | color(theme.selection_fg);
            if (!session_.HasSelection() && block_index == session_.Cursor().block && absolute == session_.Cursor().byte) glyph = glyph | inverted;
            elements.push_back(glyph); local = next;
        }
        byte += run.text.size();
    }
    if (block_index == session_.Cursor().block && session_.Cursor().byte == byte) elements.push_back(text(" ") | inverted);
    return hbox(std::move(elements));
}

ftxui::Element NotesWorkspaceComponent::RenderEditor() {
    using namespace ftxui; const Theme& theme = theme_ ? *theme_ : FallbackTheme(); auto* note = session_.Note(); if (!note) return text("No note");
    Element back = text(" < Notes ") | bold | reflect(back_box_); Element pin = text(note->pinned ? " ◆ Pinned " : " ◇ Pin ") | reflect(pin_box_);
    Element group = text(" Group: " + SectionName(*note) + " ↻ ") | reflect(section_box_); Element trash = text(note->deleted_at ? " Restore " : " Trash ") | color(note->deleted_at ? theme.button_success : theme.button_danger) | reflect(trash_box_);
    Element title = text(" " + note->title + (focus_ == FocusArea::Title ? "▌" : "")) | bold | borderStyled(LIGHT, focus_ == FocusArea::Title ? theme.modal_accent : theme.gutter);
    const uint8_t marks = session_.ActiveMarks(); auto type = note->blocks[session_.Cursor().block].type;
    Element toolbar = hbox({ToolbarItem("B", marks & MarkBit(NoteMark::Bold), bold_box_), ToolbarItem("I", marks & MarkBit(NoteMark::Italic), italic_box_), ToolbarItem("U", marks & MarkBit(NoteMark::Underlined), underline_box_), ToolbarItem("S", marks & MarkBit(NoteMark::Strikethrough), strike_box_), separator(), ToolbarItem("• List", type == NoteBlockType::BulletItem, bullet_box_), ToolbarItem("1. List", type == NoteBlockType::NumberedItem, numbered_box_), ToolbarItem("[ ] Check", type == NoteBlockType::CheckItem, check_box_), ToolbarItem("¶", type == NoteBlockType::Paragraph, paragraph_box_)}) | borderStyled(LIGHT, theme.gutter);
    Elements blocks; size_t number = 1; for (size_t index = 0; index < note->blocks.size(); ++index) { blocks.push_back(RenderRichBlock(note->blocks[index], index, note->blocks[index].type == NoteBlockType::NumberedItem ? number++ : 0)); if (note->blocks[index].type != NoteBlockType::NumberedItem) number = 1; }
    Element status = hbox({text(" Created " + note->created_at + "  Updated " + note->updated_at) | dim, filler(), text(" " + save_status_ + " ") | color(theme.modal_accent)});
    return vbox({hbox({back, separator(), pin, separator(), group, filler(), trash}), title, toolbar, vbox(std::move(blocks)) | borderStyled(focus_ == FocusArea::Body ? HEAVY : LIGHT, focus_ == FocusArea::Body ? theme.modal_accent : theme.gutter) | frame | yflex, status}) | flex | bgcolor(theme.background);
}

ftxui::Element NotesWorkspaceComponent::OnRender() { using namespace ftxui; return hbox({RenderSidebar(), separator() | color((theme_ ? *theme_ : FallbackTheme()).gutter), editing_ ? RenderEditor() : RenderOverview()}) | flex; }

std::string NotesWorkspaceComponent::StatusText() const { return save_status_; }

} // namespace textlt::notes
