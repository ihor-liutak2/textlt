#include "notes/note_session.hpp"

#include <algorithm>

#include "editor_utils.hpp"

namespace textlt::notes {

NoteSession::NoteSession(NoteDocument* note) { SetNote(note); }

void NoteSession::SetNote(NoteDocument* note) {
    note_ = note; cursor_ = {}; selection_anchor_.reset(); active_marks_ = 0;
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

void NoteSession::NormalizeRuns(NoteBlock& block) {
    std::vector<NoteRun> result;
    for (auto& run : block.runs) {
        if (run.text.empty()) continue;
        if (!result.empty() && result.back().marks == run.marks) result.back().text += run.text;
        else result.push_back(std::move(run));
    }
    if (result.empty()) result.push_back({"", active_marks_});
    block.runs = std::move(result);
}

uint8_t NoteSession::MarksAt(const NoteBlock& block, size_t byte) const {
    size_t offset = 0;
    for (const auto& run : block.runs) {
        if (byte <= offset + run.text.size()) return run.marks;
        offset += run.text.size();
    }
    return block.runs.empty() ? active_marks_ : block.runs.back().marks;
}

void NoteSession::UpdateActiveMarks() {
    if (!note_ || note_->blocks.empty()) return;
    active_marks_ = MarksAt(note_->blocks[cursor_.block], cursor_.byte);
}

void NoteSession::InsertRunText(NoteBlock& block, size_t byte, const std::string& text, uint8_t marks) {
    std::vector<NoteRun> result;
    size_t offset = 0; bool inserted = false;
    for (const auto& run : block.runs) {
        const size_t end = offset + run.text.size();
        if (!inserted && byte >= offset && byte <= end) {
            const size_t local = byte - offset;
            if (local > 0) result.push_back({run.text.substr(0, local), run.marks});
            result.push_back({text, marks});
            if (local < run.text.size()) result.push_back({run.text.substr(local), run.marks});
            inserted = true;
        } else result.push_back(run);
        offset = end;
    }
    if (!inserted) result.push_back({text, marks});
    block.runs = std::move(result); NormalizeRuns(block);
}

void NoteSession::ApplyMarks(NoteBlock& block, size_t start, size_t end, uint8_t mask, bool add) {
    if (start >= end) return;
    std::vector<NoteRun> result; size_t offset = 0;
    for (const auto& run : block.runs) {
        const size_t run_end = offset + run.text.size();
        const size_t local_start = start > offset ? std::min(start - offset, run.text.size()) : 0;
        const size_t local_end = end > offset ? std::min(end - offset, run.text.size()) : 0;
        if (local_start > 0) result.push_back({run.text.substr(0, local_start), run.marks});
        if (local_end > local_start) {
            uint8_t marks = add ? static_cast<uint8_t>(run.marks | mask) : static_cast<uint8_t>(run.marks & ~mask);
            result.push_back({run.text.substr(local_start, local_end - local_start), marks});
        }
        if (local_end < run.text.size()) result.push_back({run.text.substr(local_end), run.marks});
        offset = run_end;
    }
    block.runs = std::move(result); NormalizeRuns(block);
}

bool NoteSession::DeleteRange(NotePosition start, NotePosition end) {
    if (!note_ || start == end || Before(end, start)) return false;
    if (start.block == end.block) {
        NoteBlock& block = note_->blocks[start.block];
        std::vector<NoteRun> result; size_t offset = 0;
        for (const auto& run : block.runs) {
            const size_t run_end = offset + run.text.size();
            const size_t left = std::min(run.text.size(), start.byte > offset ? start.byte - offset : 0);
            const size_t right = std::min(run.text.size(), end.byte > offset ? end.byte - offset : 0);
            if (left > 0) result.push_back({run.text.substr(0, left), run.marks});
            if (right < run.text.size()) result.push_back({run.text.substr(right), run.marks});
            offset = run_end;
        }
        block.runs = std::move(result); NormalizeRuns(block);
    } else {
        NoteBlock& first = note_->blocks[start.block];
        NoteBlock& last = note_->blocks[end.block];
        std::vector<NoteRun> merged;
        size_t offset = 0;
        for (const auto& run : first.runs) {
            const size_t keep = std::min(run.text.size(), start.byte > offset ? start.byte - offset : 0);
            if (keep) merged.push_back({run.text.substr(0, keep), run.marks});
            offset += run.text.size();
        }
        offset = 0;
        for (const auto& run : last.runs) {
            const size_t skip = std::min(run.text.size(), end.byte > offset ? end.byte - offset : 0);
            if (skip < run.text.size()) merged.push_back({run.text.substr(skip), run.marks});
            offset += run.text.size();
        }
        first.runs = std::move(merged); NormalizeRuns(first);
        note_->blocks.erase(note_->blocks.begin() + static_cast<std::ptrdiff_t>(start.block + 1), note_->blocks.begin() + static_cast<std::ptrdiff_t>(end.block + 1));
    }
    cursor_ = start; selection_anchor_.reset(); return true;
}

bool NoteSession::DeleteSelection() {
    if (!HasSelection()) return false;
    PushUndo(); const auto range = Selection(); DeleteRange(range.first, range.second); Changed(); return true;
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
            InsertRunText(block, cursor_.byte, part, active_marks_);
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
    NoteBlock& block = note_->blocks[cursor_.block]; const std::string text = BlockText(block);
    NoteBlock next{GenerateUuid(), block.type, block.indent, false, {}};
    size_t offset = 0;
    for (const auto& run : block.runs) {
        const size_t local = std::min(run.text.size(), cursor_.byte > offset ? cursor_.byte - offset : 0);
        if (local < run.text.size()) next.runs.push_back({run.text.substr(local), run.marks});
        offset += run.text.size();
    }
    DeleteRange(cursor_, {cursor_.block, text.size()});
    if (next.runs.empty()) next.runs.push_back({"", active_marks_});
    note_->blocks.insert(note_->blocks.begin() + static_cast<std::ptrdiff_t>(cursor_.block + 1), std::move(next));
    ++cursor_.block;
    cursor_.byte = 0;
}

bool NoteSession::Enter() {
    if (!note_) return false;
    if (HasSelection()) { PushUndo(); auto range = Selection(); DeleteRange(range.first, range.second); } else PushUndo();
    NoteBlock& block = note_->blocks[cursor_.block];
    if (BlockText(block).empty() && block.type != NoteBlockType::Paragraph) {
        block.type = NoteBlockType::Paragraph;
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
        UpdateActiveMarks();
        return true;
    }
    if (cursor_.block == 0) return false;
    --cursor_.block;
    cursor_.byte = BlockText(note_->blocks[cursor_.block]).size();
    UpdateActiveMarks();
    return true;
}
bool NoteSession::MoveRight(bool selecting) {
    if (!note_) return false;
    BeginSelection(selecting);
    const std::string text = BlockText(note_->blocks[cursor_.block]);
    if (cursor_.byte < text.size()) {
        cursor_.byte = textlt::utils::NextUtf8CodepointStart(text, cursor_.byte);
        UpdateActiveMarks();
        return true;
    }
    if (cursor_.block + 1 >= note_->blocks.size()) return false;
    ++cursor_.block;
    cursor_.byte = 0;
    UpdateActiveMarks();
    return true;
}
bool NoteSession::MoveUp(bool selecting) { if (!note_ || cursor_.block == 0) return false; BeginSelection(selecting); --cursor_.block; cursor_.byte = std::min(cursor_.byte, BlockText(note_->blocks[cursor_.block]).size()); UpdateActiveMarks(); return true; }
bool NoteSession::MoveDown(bool selecting) { if (!note_ || cursor_.block + 1 >= note_->blocks.size()) return false; BeginSelection(selecting); ++cursor_.block; cursor_.byte = std::min(cursor_.byte, BlockText(note_->blocks[cursor_.block]).size()); UpdateActiveMarks(); return true; }
void NoteSession::MoveHome(bool selecting) { BeginSelection(selecting); cursor_.byte = 0; UpdateActiveMarks(); }
void NoteSession::MoveEnd(bool selecting) { BeginSelection(selecting); if (note_) cursor_.byte = BlockText(note_->blocks[cursor_.block]).size(); UpdateActiveMarks(); }
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

bool NoteSession::ToggleMark(NoteMark mark) {
    const uint8_t mask = MarkBit(mark);
    if (!note_) return false;
    if (!HasSelection()) { active_marks_ ^= mask; return true; }
    PushUndo(); const auto range = Selection(); bool all_set = true;
    for (size_t block = range.first.block; block <= range.second.block; ++block) {
        const auto& item = note_->blocks[block]; const size_t start = block == range.first.block ? range.first.byte : 0; const size_t end = block == range.second.block ? range.second.byte : BlockText(item).size();
        size_t offset = 0; for (const auto& run : item.runs) { if (offset < end && offset + run.text.size() > start && (run.marks & mask) == 0) all_set = false; offset += run.text.size(); }
    }
    for (size_t block = range.first.block; block <= range.second.block; ++block) { auto& item = note_->blocks[block]; const size_t start = block == range.first.block ? range.first.byte : 0; const size_t end = block == range.second.block ? range.second.byte : BlockText(item).size(); ApplyMarks(item, start, end, mask, !all_set); }
    Changed(); return true;
}

bool NoteSession::ClearFormatting() {
    if (!note_) return false;
    if (!HasSelection()) { active_marks_ = 0; return true; }
    PushUndo(); const auto range = Selection();
    for (size_t block = range.first.block; block <= range.second.block; ++block) { auto& item = note_->blocks[block]; size_t start = block == range.first.block ? range.first.byte : 0; size_t end = block == range.second.block ? range.second.byte : BlockText(item).size(); ApplyMarks(item, start, end, 0xff, false); }
    Changed(); return true;
}

bool NoteSession::SetBlockType(NoteBlockType type) { if (!note_) return false; PushUndo(); auto range = Selection(); for (size_t block = range.first.block; block <= range.second.block; ++block) { note_->blocks[block].type = type; if (type != NoteBlockType::CheckItem) note_->blocks[block].checked = false; } Changed(); return true; }
bool NoteSession::ToggleCheck() { if (!note_) return false; auto& block = note_->blocks[cursor_.block]; if (block.type != NoteBlockType::CheckItem) return SetBlockType(NoteBlockType::CheckItem); PushUndo(); block.checked = !block.checked; Changed(); return true; }
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
