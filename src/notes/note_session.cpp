#include "notes/note_session.hpp"

#include <algorithm>

#include "editor_utils.hpp"

namespace textlt::notes {

NoteSession::NoteSession(NoteDocument* note) { SetNote(note); }

void NoteSession::SetNote(NoteDocument* note) {
    note_ = note; cursor_ = {}; selection_anchor_.reset();
    dirty_ = false; undo_.clear(); redo_.clear(); ClampCursor();
}

bool NoteSession::Before(const NotePosition& left, const NotePosition& right) {
    return left.block < right.block || (left.block == right.block && left.byte < right.byte);
}

std::pair<NotePosition, NotePosition> NoteSession::Selection() const {
    if (!selection_anchor_) return {cursor_, cursor_};
    return Before(*selection_anchor_, cursor_) ? std::make_pair(*selection_anchor_, cursor_) : std::make_pair(cursor_, *selection_anchor_);
}

void NoteSession::ClampCursor() {
    if (!note_ || note_->blocks.empty()) { cursor_ = {}; return; }
    cursor_.block = std::min(cursor_.block, note_->blocks.size() - 1);
    cursor_.byte = std::min(cursor_.byte, BlockText(note_->blocks[cursor_.block]).size());
}

void NoteSession::BeginSelection(bool selecting) {
    if (selecting) { if (!selection_anchor_) selection_anchor_ = cursor_; }
    else selection_anchor_.reset();
}

void NoteSession::PushUndo() {
    if (!note_) return;
    undo_.push_back(*note_);
    if (undo_.size() > 100) undo_.erase(undo_.begin());
    redo_.clear();
}

void NoteSession::Changed() {
    if (!note_) return;
    note_->updated_at = UtcNow(); ++note_->revision; dirty_ = true;
}

void NoteSession::InsertTextAt(NoteBlock& block, size_t byte, const std::string& text) {
    std::string current = BlockText(block);
    byte = std::min(byte, current.size());
    current.insert(byte, text);
    block.runs = {{std::move(current)}};
}

bool NoteSession::DeleteRange(NotePosition start, NotePosition end) {
    if (!note_ || start == end || Before(end, start)) return false;
    if (start.block == end.block) {
        NoteBlock& block = note_->blocks[start.block];
        const std::string text = BlockText(block);
        const size_t from = std::min(start.byte, text.size());
        const size_t to = std::max(from, std::min(end.byte, text.size()));
        block.runs = {{text.substr(0, from) + text.substr(to)}};
    } else {
        NoteBlock& first = note_->blocks[start.block];
        NoteBlock& last = note_->blocks[end.block];
        const std::string first_text = BlockText(first);
        const std::string last_text = BlockText(last);
        const size_t from = std::min(start.byte, first_text.size());
        const size_t to = std::min(end.byte, last_text.size());
        first.runs = {{first_text.substr(0, from) + last_text.substr(to)}};
        note_->blocks.erase(note_->blocks.begin() + static_cast<std::ptrdiff_t>(start.block + 1),
            note_->blocks.begin() + static_cast<std::ptrdiff_t>(end.block + 1));
    }
    cursor_ = start; selection_anchor_.reset(); return true;
}

bool NoteSession::DeleteSelection() {
    if (!HasSelection()) return false;
    PushUndo(); const auto range = Selection(); DeleteRange(range.first, range.second); Changed(); return true;
}

void NoteSession::SetCursor(const NotePosition& position, bool selecting) {
    if (!note_) return;
    BeginSelection(selecting);
    cursor_ = position;
    ClampCursor();
}

bool NoteSession::Insert(const std::string& text) {
    if (!note_ || text.empty()) return false;
    if (HasSelection()) { PushUndo(); auto range = Selection(); DeleteRange(range.first, range.second); }
    else PushUndo();
    size_t start = 0;
    while (start <= text.size()) {
        const size_t newline = text.find('\n', start);
        const size_t end = newline == std::string::npos ? text.size() : newline;
        std::string part = text.substr(start, end - start);
        if (!part.empty() && part.back() == '\r') part.pop_back();
        if (!part.empty()) {
            NoteBlock& block = note_->blocks[cursor_.block];
            InsertTextAt(block, cursor_.byte, part);
            cursor_.byte += part.size();
        }
        if (newline == std::string::npos) break;
        SplitBlockAtCursor();
        start = newline + 1;
    }
    Changed();
    return true;
}

bool NoteSession::Backspace() {
    if (!note_) return false;
    if (HasSelection()) return DeleteSelection();
    if (cursor_.byte > 0) {
        PushUndo(); const std::string text = BlockText(note_->blocks[cursor_.block]);
        const size_t previous = textlt::utils::PreviousUtf8CodepointStart(text, cursor_.byte);
        DeleteRange({cursor_.block, previous}, cursor_); Changed(); return true;
    }
    if (cursor_.block == 0) return false;
    PushUndo(); const size_t previous_length = BlockText(note_->blocks[cursor_.block - 1]).size();
    DeleteRange({cursor_.block - 1, previous_length}, {cursor_.block, 0}); Changed(); return true;
}

bool NoteSession::DeleteForward() {
    if (!note_) return false;
    if (HasSelection()) return DeleteSelection();
    const std::string text = BlockText(note_->blocks[cursor_.block]);
    if (cursor_.byte < text.size()) {
        PushUndo(); const size_t next = textlt::utils::NextUtf8CodepointStart(text, cursor_.byte);
        DeleteRange(cursor_, {cursor_.block, next}); Changed(); return true;
    }
    if (cursor_.block + 1 >= note_->blocks.size()) return false;
    PushUndo(); DeleteRange(cursor_, {cursor_.block + 1, 0}); Changed(); return true;
}

void NoteSession::SplitBlockAtCursor() {
    NoteBlock& block = note_->blocks[cursor_.block];
    const std::string text = BlockText(block);
    const size_t offset = std::min(cursor_.byte, text.size());
    // Enter at the end of a heading continues as a normal paragraph.
    const NoteBlockType next_type = block.heading > 0
        ? NoteBlockType::Paragraph : block.type;
    NoteBlock next{GenerateUuid(), next_type, block.indent, false,
        {{text.substr(offset)}}};
    next.heading = 0;
    block.runs = {{text.substr(0, offset)}};
    note_->blocks.insert(note_->blocks.begin() + static_cast<std::ptrdiff_t>(cursor_.block + 1), std::move(next));
    ++cursor_.block;
    cursor_.byte = 0;
}

bool NoteSession::Enter() {
    if (!note_) return false;
    if (HasSelection()) { PushUndo(); auto range = Selection(); DeleteRange(range.first, range.second); } else PushUndo();
    NoteBlock& block = note_->blocks[cursor_.block];
    if (BlockText(block).empty() &&
        (block.type != NoteBlockType::Paragraph || block.heading > 0)) {
        block.type = NoteBlockType::Paragraph;
        block.heading = 0;
        block.indent = 0;
        Changed();
        return true;
    }
    SplitBlockAtCursor();
    Changed();
    return true;
}

bool NoteSession::MoveLeft(bool selecting) {
    if (!note_) return false;
    BeginSelection(selecting);
    if (cursor_.byte > 0) {
        cursor_.byte = textlt::utils::PreviousUtf8CodepointStart(BlockText(note_->blocks[cursor_.block]), cursor_.byte);
        return true;
    }
    if (cursor_.block == 0) return false;
    --cursor_.block;
    cursor_.byte = BlockText(note_->blocks[cursor_.block]).size();
    return true;
}
bool NoteSession::MoveRight(bool selecting) {
    if (!note_) return false;
    BeginSelection(selecting);
    const std::string text = BlockText(note_->blocks[cursor_.block]);
    if (cursor_.byte < text.size()) {
        cursor_.byte = textlt::utils::NextUtf8CodepointStart(text, cursor_.byte);
        return true;
    }
    if (cursor_.block + 1 >= note_->blocks.size()) return false;
    ++cursor_.block;
    cursor_.byte = 0;
    return true;
}
bool NoteSession::MoveUp(bool selecting) { if (!note_ || cursor_.block == 0) return false; BeginSelection(selecting); --cursor_.block; cursor_.byte = std::min(cursor_.byte, BlockText(note_->blocks[cursor_.block]).size()); return true; }
bool NoteSession::MoveDown(bool selecting) { if (!note_ || cursor_.block + 1 >= note_->blocks.size()) return false; BeginSelection(selecting); ++cursor_.block; cursor_.byte = std::min(cursor_.byte, BlockText(note_->blocks[cursor_.block]).size()); return true; }
void NoteSession::MoveHome(bool selecting) { BeginSelection(selecting); cursor_.byte = 0; }
void NoteSession::MoveEnd(bool selecting) { BeginSelection(selecting); if (note_) cursor_.byte = BlockText(note_->blocks[cursor_.block]).size(); }
void NoteSession::SelectAll() { if (!note_ || note_->blocks.empty()) return; selection_anchor_ = NotePosition{0, 0}; cursor_ = {note_->blocks.size() - 1, BlockText(note_->blocks.back()).size()}; }

std::string NoteSession::SelectedText() const {
    if (!note_ || !HasSelection()) return {};
    const auto range = Selection(); std::string result;
    for (size_t block = range.first.block; block <= range.second.block; ++block) {
        if (!result.empty()) result += '\n';
        const std::string text = BlockText(note_->blocks[block]);
        const size_t start = block == range.first.block ? range.first.byte : 0;
        const size_t end = block == range.second.block ? range.second.byte : text.size();
        result += text.substr(start, end - start);
    }
    return result;
}

int NoteSession::ActiveHeading() const {
    if (!note_ || note_->blocks.empty()) return 0;
    return note_->blocks[std::min(cursor_.block, note_->blocks.size() - 1)].heading;
}

bool NoteSession::ToggleHeading(int level) {
    if (!note_) return false;
    level = std::clamp(level, 1, 3);
    PushUndo();
    const auto range = Selection();
    bool all_same = true;
    for (size_t block = range.first.block; block <= range.second.block; ++block) {
        if (note_->blocks[block].heading != level) { all_same = false; break; }
    }
    const int target = all_same ? 0 : level;
    for (size_t block = range.first.block; block <= range.second.block; ++block) {
        NoteBlock& item = note_->blocks[block];
        item.heading = target;
        if (target > 0) {
            item.type = NoteBlockType::Paragraph;
            item.checked = false;
        }
    }
    Changed();
    return true;
}

bool NoteSession::SetBlockType(NoteBlockType type) { if (!note_) return false; PushUndo(); auto range = Selection(); for (size_t block = range.first.block; block <= range.second.block; ++block) { note_->blocks[block].type = type; note_->blocks[block].heading = 0; if (type != NoteBlockType::CheckItem) note_->blocks[block].checked = false; } Changed(); return true; }
bool NoteSession::ToggleCheck() { if (!note_) return false; auto& block = note_->blocks[cursor_.block]; if (block.type != NoteBlockType::CheckItem || block.heading > 0) return SetBlockType(NoteBlockType::CheckItem); PushUndo(); block.checked = !block.checked; Changed(); return true; }
bool NoteSession::Indent(int delta) { if (!note_) return false; PushUndo(); auto range = Selection(); for (size_t block = range.first.block; block <= range.second.block; ++block) note_->blocks[block].indent = std::clamp(note_->blocks[block].indent + delta, 0, 6); Changed(); return true; }

bool NoteSession::Undo() {
    if (!note_ || undo_.empty()) return false;
    const uint64_t next_revision = note_->revision + 1;
    redo_.push_back(*note_);
    *note_ = std::move(undo_.back());
    undo_.pop_back();
    note_->revision = next_revision;
    note_->updated_at = UtcNow();
    ClampCursor();
    selection_anchor_.reset();
    dirty_ = true;
    return true;
}

bool NoteSession::Redo() {
    if (!note_ || redo_.empty()) return false;
    const uint64_t next_revision = note_->revision + 1;
    undo_.push_back(*note_);
    *note_ = std::move(redo_.back());
    redo_.pop_back();
    note_->revision = next_revision;
    note_->updated_at = UtcNow();
    ClampCursor();
    selection_anchor_.reset();
    dirty_ = true;
    return true;
}

} // namespace textlt::notes
