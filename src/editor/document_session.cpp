#include "editor/document_session.hpp"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <unordered_map>
#include <utility>
#include <vector>

#include "editor/text_buffer.hpp"
#include "editor_utils.hpp"

namespace textlt {
namespace {

std::string ToLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool EndsWith(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
        value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

} // namespace


DocumentSession::DocumentSession()
    : lines(buffer.MutableLines()),
      is_dirty(buffer.DirtyFlag()) {}

DocumentSession::DocumentSession(std::filesystem::path p)
    : DocumentSession() {
    SetPath(std::move(p));
}

DocumentSession::DocumentSession(const DocumentSession& other)
    : buffer(other.buffer),
      history(other.history),
      lines(buffer.MutableLines()),
      is_dirty(buffer.DirtyFlag()),
      fallback_cursor_state_(other.CursorState()),
      active_cursor_state_(&fallback_cursor_state_),
      path(other.path),
      type(other.type),
      line_ending(other.line_ending),
      read_only(other.read_only),
      temporary(other.temporary) {}

DocumentSession& DocumentSession::operator=(const DocumentSession& other) {
    if (this != &other) {
        BindFrom(other);
    }
    return *this;
}

DocumentSession::DocumentSession(DocumentSession&& other) noexcept
    : buffer(std::move(other.buffer)),
      history(std::move(other.history)),
      lines(buffer.MutableLines()),
      is_dirty(buffer.DirtyFlag()),
      fallback_cursor_state_(other.CursorState()),
      active_cursor_state_(&fallback_cursor_state_),
      path(std::move(other.path)),
      type(other.type),
      line_ending(other.line_ending),
      read_only(other.read_only),
      temporary(other.temporary) {}

DocumentSession& DocumentSession::operator=(DocumentSession&& other) noexcept {
    if (this != &other) {
        buffer = std::move(other.buffer);
        history = std::move(other.history);
        fallback_cursor_state_ = other.CursorState();
        active_cursor_state_ = &fallback_cursor_state_;
        path = std::move(other.path);
        type = other.type;
        line_ending = other.line_ending;
        read_only = other.read_only;
        temporary = other.temporary;
    }
    return *this;
}

void DocumentSession::BindFrom(const DocumentSession& other) {
    buffer = other.buffer;
    history = other.history;
    fallback_cursor_state_ = other.CursorState();
    active_cursor_state_ = &fallback_cursor_state_;
    path = other.path;
    type = other.type;
    line_ending = other.line_ending;
    read_only = other.read_only;
    temporary = other.temporary;
}


void DocumentSession::SetActiveCursorState(EditorCursorState* cursor_state) {
    active_cursor_state_ = cursor_state ? cursor_state : &fallback_cursor_state_;
}

void DocumentSession::ClearActiveCursorState() {
    active_cursor_state_ = &fallback_cursor_state_;
}

EditorCursorState* DocumentSession::ActiveCursorStatePointer() const {
    return active_cursor_state_;
}

EditorCursorState& DocumentSession::CursorState() {
    if (!active_cursor_state_) {
        active_cursor_state_ = &fallback_cursor_state_;
    }
    return *active_cursor_state_;
}

const EditorCursorState& DocumentSession::CursorState() const {
    return active_cursor_state_ ? *active_cursor_state_ : fallback_cursor_state_;
}

size_t& DocumentSession::CursorRow() {
    return CursorState().cursor_row;
}

size_t DocumentSession::CursorRow() const {
    return CursorState().cursor_row;
}

size_t& DocumentSession::CursorCol() {
    return CursorState().cursor_col;
}

size_t DocumentSession::CursorCol() const {
    return CursorState().cursor_col;
}

Selection& DocumentSession::SelectionState() {
    return CursorState().selection;
}

const Selection& DocumentSession::SelectionState() const {
    return CursorState().selection;
}

DocumentType DocumentSession::DetermineDocumentType(const std::filesystem::path& path) {
    const std::string lower_ext = ToLowerCopy(path.extension().string());
    const std::string lower_filename = ToLowerCopy(path.filename().string());

    if (lower_filename.find(".blade.php") != std::string::npos) {
        return DocumentType::Blade;
    }
    if (lower_filename == ".bashrc" || lower_filename == ".profile") {
        return DocumentType::Bash;
    }
    if (lower_filename == "cmakelists.txt") {
        return DocumentType::CMake;
    }

    static const std::unordered_map<std::string, DocumentType> ext_map = {
        {".sh", DocumentType::Bash}, {".bash", DocumentType::Bash},
        {".cmake", DocumentType::CMake},
        {".zsh", DocumentType::Bash}, {".cpp", DocumentType::Cpp},
        {".hpp", DocumentType::Cpp}, {".cc", DocumentType::Cpp},
        {".h", DocumentType::Cpp}, {".json", DocumentType::Json},
        {".md", DocumentType::Markdown}, {".markdown", DocumentType::Markdown},
        {".html", DocumentType::Html},
        {".htm", DocumentType::Html}, {".css", DocumentType::Css},
        {".go", DocumentType::Go}, {".js", DocumentType::Js},
        {".jsx", DocumentType::Jsx}, {".ts", DocumentType::Ts},
        {".tsx", DocumentType::Tsx}, {".php", DocumentType::Php},
        {".rs", DocumentType::Rust}, {".java", DocumentType::Java},
        {".py", DocumentType::Python},
        {".xml", DocumentType::Xml}, {".xsd", DocumentType::Xml},
        {".xsl", DocumentType::Xml}, {".xslt", DocumentType::Xml},
    };

    if (const auto it = ext_map.find(lower_ext); it != ext_map.end()) {
        return it->second;
    }
    return DocumentType::PlainText;
}

void DocumentSession::Reset() {
    CursorRow() = 0;
    CursorCol() = 0;
    SelectionState() = {};
    path = "Untitled";
    type = DocumentType::PlainText;
    line_ending = LineEnding::LF;
    read_only = false;
    temporary = false;
    buffer.SetLines({""});
    buffer.SetDirty(false);
    history.Clear();
}

void DocumentSession::SetPath(std::filesystem::path new_path) {
    path = std::move(new_path);
    RefreshLexer();
}

void DocumentSession::RefreshLexer() {
    type = DetermineDocumentType(path);
}


bool DocumentSession::IsMemoryOnly() const {
    const std::string value = path.string();
    return path.empty() || value == "Untitled" || value == "untitled.txt";
}

std::string DocumentSession::CurrentFilePath() const {
    return path.string();
}

std::string DocumentSession::DisplayTitle() const {
    std::string title = path.filename().string();
    if (title.empty()) {
        title = path.string();
    }
    if (title.empty()) {
        title = "Untitled";
    }
    return title;
}

std::string DocumentSession::TypeLabel() const {
    switch (type) {
        case DocumentType::Bash:       return "Bash Script";
        case DocumentType::CMake:      return "CMake Script";
        case DocumentType::Cpp:        return "C++ Source";
        case DocumentType::Json:       return "JSON Document";
        case DocumentType::Markdown:   return "Markdown Document";
        case DocumentType::Html:       return "HTML Document";
        case DocumentType::Css:        return "CSS Stylesheet";
        case DocumentType::Go:         return "Go Source";
        case DocumentType::Js:         return "JavaScript Source";
        case DocumentType::Jsx:        return "React JSX Source";
        case DocumentType::Ts:         return "TypeScript Source";
        case DocumentType::Tsx:        return "React TSX Source";
        case DocumentType::Php:        return "PHP Script";
        case DocumentType::Blade:      return "Laravel Blade Template";
        case DocumentType::Rust:       return "Rust Source";
        case DocumentType::Java:       return "Java Source";
        case DocumentType::Python:     return "Python Script";
        case DocumentType::Xml:        return "XML Document";
        case DocumentType::PlainText:  return "Plain Text";
        default:                       return "Unknown";
    }
}

std::string DocumentSession::Label() const {
    return TypeLabel();
}

std::string DocumentSession::LexerId() const {
    switch (type) {
        case DocumentType::Bash:      return "bash";
        case DocumentType::CMake:     return "cmake";
        case DocumentType::Cpp:       return "cpp";
        case DocumentType::Json:      return "json";
        case DocumentType::Markdown:  return "markdown";
        case DocumentType::Html:      return "html";
        case DocumentType::Css:       return "css";
        case DocumentType::Go:        return "go";
        case DocumentType::Js:        return "javascript";
        case DocumentType::Jsx:       return "jsx";
        case DocumentType::Ts:        return "typescript";
        case DocumentType::Tsx:       return "tsx";
        case DocumentType::Php:       return "php";
        case DocumentType::Blade:     return "blade";
        case DocumentType::Rust:      return "rust";
        case DocumentType::Java:      return "java";
        case DocumentType::Python:    return "python";
        case DocumentType::Xml:       return "xml";
        case DocumentType::PlainText: return "plain";
        default:                      return "plain";
    }
}

std::string DocumentSession::CommentPrefix() const {
    const std::string filename = path.filename().string();
    const std::string extension = path.extension().string();

    if (extension == ".sql" || extension == ".graphql" || extension == ".gql") {
        return "--";
    }

    if (filename.rfind("Dockerfile", 0) == 0 ||
        filename == ".bashrc" || filename == ".profile" ||
        filename == ".env" || filename == ".env.local" ||
        filename == ".env.development" || filename == ".env.production" ||
        EndsWith(filename, ".env") ||
        extension == ".conf" || extension == ".ini" || extension == ".py" ||
        extension == ".rb" || extension == ".yaml" || extension == ".yml" ||
        extension == ".sh" || extension == ".bash" || extension == ".zsh" ||
        extension == ".bashrc" || extension == ".profile") {
        return "#";
    }

    return "//";
}

std::string DocumentSession::LineEndingLabel() const {
    return line_ending == LineEnding::CRLF ? "CRLF" : "LF";
}

std::string DocumentSession::LineEndingText() const {
    return line_ending == LineEnding::CRLF ? "\r\n" : "\n";
}

HistoryManager::State DocumentSession::CurrentTextProcessorState() const {
    return {
        buffer.Lines().empty() ? std::vector<std::string>{""} : buffer.Lines(),
        static_cast<int>(CursorCol()),
        static_cast<int>(CursorRow()),
    };
}

void DocumentSession::ClampTextProcessorCursor() {
    buffer.EnsureValid();
    const auto& lines = buffer.Lines();
    CursorRow() = std::min(CursorRow(), lines.size() - 1);
    CursorCol() = std::min(CursorCol(), lines[CursorRow()].size());
    if (SelectionState().active) {
        SelectionState().anchor_y = std::min(SelectionState().anchor_y, lines.size() - 1);
        SelectionState().anchor_x = std::min(SelectionState().anchor_x, lines[SelectionState().anchor_y].size());
    }
}

void DocumentSession::SelectWholeTextProcessorBuffer() {
    buffer.EnsureValid();
    const auto& lines = buffer.Lines();
    SelectionState().anchor_x = 0;
    SelectionState().anchor_y = 0;
    CursorRow() = lines.size() - 1;
    CursorCol() = lines[CursorRow()].size();
    SelectionState().active = true;
}

void DocumentSession::ClearTextProcessorSelection() {
    SelectionState().active = false;
    SelectionState().anchor_x = CursorCol();
    SelectionState().anchor_y = CursorRow();
}

std::string DocumentSession::SelectedTextFromBuffer() const {
    const auto& lines = buffer.Lines();
    if (!HasSelection() || lines.empty()) {
        return "";
    }

    auto [start, end] = utils::OrderedSelection(
        {SelectionState().anchor_x, SelectionState().anchor_y},
        {CursorCol(), CursorRow()},
        lines);

    if (start.y == end.y) {
        return lines[start.y].substr(start.x, end.x - start.x);
    }

    std::string selected = lines[start.y].substr(start.x);
    selected.push_back('\n');
    for (size_t y = start.y + 1; y < end.y; ++y) {
        selected += lines[y];
        selected.push_back('\n');
    }
    selected += lines[end.y].substr(0, end.x);
    return selected;
}

bool DocumentSession::DeleteTextProcessorSelection() {
    auto& lines = buffer.MutableLines();
    if (!HasSelection() || lines.empty()) {
        return false;
    }

    auto [start, end] = utils::OrderedSelection(
        {SelectionState().anchor_x, SelectionState().anchor_y},
        {CursorCol(), CursorRow()},
        lines);

    if (start.y == end.y) {
        lines[start.y].erase(start.x, end.x - start.x);
    } else {
        lines[start.y] = lines[start.y].substr(0, start.x) + lines[end.y].substr(end.x);
        lines.erase(
            lines.begin() + static_cast<std::ptrdiff_t>(start.y + 1),
            lines.begin() + static_cast<std::ptrdiff_t>(end.y + 1));
    }

    if (lines.empty()) {
        lines.push_back("");
    }

    CursorCol() = start.x;
    CursorRow() = start.y;
    ClearTextProcessorSelection();
    ClampTextProcessorCursor();
    return true;
}

bool DocumentSession::InsertTextProcessorText(const std::string& text) {
    if (text.empty()) {
        return false;
    }

    buffer.EnsureValid();
    auto& lines = buffer.MutableLines();

    std::vector<std::string> inserted_lines(1);
    for (char character : text) {
        if (character == '\r') {
            continue;
        }
        if (character == '\n') {
            inserted_lines.emplace_back();
        } else {
            inserted_lines.back().push_back(character);
        }
    }

    const size_t insertion_row = CursorRow();
    const std::string suffix = lines[insertion_row].substr(CursorCol());
    lines[insertion_row].erase(CursorCol());
    lines[insertion_row] += inserted_lines.front();

    if (inserted_lines.size() == 1) {
        CursorCol() = lines[insertion_row].size();
        lines[insertion_row] += suffix;
    } else {
        inserted_lines.back() += suffix;
        lines.insert(
            lines.begin() + static_cast<std::ptrdiff_t>(insertion_row + 1),
            std::make_move_iterator(inserted_lines.begin() + 1),
            std::make_move_iterator(inserted_lines.end()));
        CursorRow() = insertion_row + inserted_lines.size() - 1;
        CursorCol() = lines[CursorRow()].size() - suffix.size();
    }

    ClearTextProcessorSelection();
    ClampTextProcessorCursor();
    return true;
}

bool DocumentSession::GetTextProcessorTargetText(
    bool whole_document,
    std::string& text,
    std::string& error) const {
    if (whole_document) {
        text = buffer.ToText(LineEndingText());
        return true;
    }

    if (!HasSelection()) {
        error = "No selected text. Select text or enable Whole document.";
        return false;
    }

    text = SelectedTextFromBuffer();
    return true;
}

bool DocumentSession::ReplaceTextProcessorTargetText(
    bool whole_document,
    const std::string& text,
    std::string& error) {
    if (read_only) {
        error = "Document is read-only.";
        return false;
    }

    ClampTextProcessorCursor();
    if (whole_document) {
        SelectWholeTextProcessorBuffer();
    } else if (!HasSelection()) {
        error = "No selected text. Select text or enable Whole document.";
        return false;
    }

    history.EndTypingGroup();
    history.PushSnapshot(CurrentTextProcessorState());

    bool changed = false;
    if (text.empty()) {
        changed = DeleteTextProcessorSelection();
    } else {
        DeleteTextProcessorSelection();
        changed = InsertTextProcessorText(text);
    }

    if (changed) {
        buffer.MarkDirty();
    }
    return changed;
}

} // namespace textlt
