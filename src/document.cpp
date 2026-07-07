#include "document.hpp"

#include <utility>

namespace textlt {

Document::Document()
    : path(session.path),
      lines(buffer.MutableLines()),
      type(session.type),
      line_ending(session.line_ending),
      cursor_row(session.cursor_row),
      cursor_col(session.cursor_col),
      selection(session.selection),
      is_dirty(buffer.DirtyFlag()),
      read_only(session.read_only),
      temporary(session.temporary) {}

Document::Document(std::filesystem::path p)
    : Document() {
    SetPath(std::move(p));
}

Document::Document(const Document& other)
    : buffer(other.buffer),
      session(other.session),
      path(session.path),
      lines(buffer.MutableLines()),
      type(session.type),
      line_ending(session.line_ending),
      cursor_row(session.cursor_row),
      cursor_col(session.cursor_col),
      selection(session.selection),
      is_dirty(buffer.DirtyFlag()),
      read_only(session.read_only),
      temporary(session.temporary),
      history(other.history) {}

Document& Document::operator=(const Document& other) {
    if (this != &other) {
        BindFrom(other);
    }
    return *this;
}

Document::Document(Document&& other) noexcept
    : buffer(std::move(other.buffer)),
      session(std::move(other.session)),
      path(session.path),
      lines(buffer.MutableLines()),
      type(session.type),
      line_ending(session.line_ending),
      cursor_row(session.cursor_row),
      cursor_col(session.cursor_col),
      selection(session.selection),
      is_dirty(buffer.DirtyFlag()),
      read_only(session.read_only),
      temporary(session.temporary),
      history(std::move(other.history)) {}

Document& Document::operator=(Document&& other) noexcept {
    if (this != &other) {
        buffer = std::move(other.buffer);
        session = std::move(other.session);
        history = std::move(other.history);
    }
    return *this;
}

void Document::BindFrom(const Document& other) {
    buffer = other.buffer;
    session = other.session;
    history = other.history;
}


DocumentSession& Document::Session() {
    return session;
}

const DocumentSession& Document::Session() const {
    return session;
}

TextBuffer& Document::Buffer() {
    return buffer;
}

const TextBuffer& Document::Buffer() const {
    return buffer;
}

void Document::SetPath(std::filesystem::path p) {
    session.SetPath(std::move(p));
}

void Document::Reset() {
    session.Reset();
    buffer.SetLines({""});
    buffer.SetDirty(false);
    history.Clear();
}

std::string Document::Label() const {
    return session.TypeLabel();
}

std::string Document::LexerId() const {
    return session.LexerId();
}

std::string Document::DisplayTitle() const {
    return session.DisplayTitle();
}

bool Document::IsMemoryOnly() const {
    return session.IsMemoryOnly();
}

} // namespace textlt
