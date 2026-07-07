#include "editor/text_buffer.hpp"

#include <utility>

namespace textlt {

TextBuffer::TextBuffer() = default;

TextBuffer::TextBuffer(std::vector<std::string> lines)
    : lines_(std::move(lines)) {
    EnsureValid();
}

const std::vector<std::string>& TextBuffer::Lines() const {
    return lines_;
}

std::vector<std::string>& TextBuffer::MutableLines() {
    return lines_;
}

const std::string& TextBuffer::Line(size_t index) const {
    return lines_.at(index);
}

std::string& TextBuffer::MutableLine(size_t index) {
    return lines_.at(index);
}

void TextBuffer::SetLines(std::vector<std::string> lines) {
    lines_ = std::move(lines);
    EnsureValid();
    Touch();
}

void TextBuffer::SetText(const std::string& text) {
    lines_.clear();
    std::string line;
    for (char character : text) {
        if (character == '\n') {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            lines_.push_back(line);
            line.clear();
        } else {
            line.push_back(character);
        }
    }
    if (!text.empty() && text.back() != '\n') {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines_.push_back(line);
    }
    EnsureValid();
    Touch();
}

std::string TextBuffer::ToText(const std::string& line_ending) const {
    if (lines_.empty()) {
        return "";
    }

    std::string full_text;
    full_text.reserve(lines_.size() * 80);
    for (size_t index = 0; index < lines_.size(); ++index) {
        full_text += lines_[index];
        if (index + 1 < lines_.size()) {
            full_text += line_ending;
        }
    }
    return full_text;
}

size_t TextBuffer::LineCount() const {
    return lines_.size();
}

bool TextBuffer::Empty() const {
    return lines_.empty();
}

void TextBuffer::EnsureValid() {
    if (lines_.empty()) {
        lines_.push_back("");
    }
}

bool TextBuffer::Dirty() const {
    return dirty_;
}

bool& TextBuffer::DirtyFlag() {
    return dirty_;
}

void TextBuffer::SetDirty(bool dirty) {
    dirty_ = dirty;
}

void TextBuffer::MarkDirty() {
    dirty_ = true;
    Touch();
}

std::uint64_t TextBuffer::Version() const {
    return version_;
}

void TextBuffer::Touch() {
    ++version_;
}

} // namespace textlt
