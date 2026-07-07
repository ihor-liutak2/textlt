#include "document.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace textlt {

namespace {

std::string ToLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

DocumentType DetermineDocumentType(const std::filesystem::path& path) {
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

} // namespace

Document::Document()
    : lines(buffer.MutableLines()),
      cursor_row(session.cursor_row),
      cursor_col(session.cursor_col),
      selection(session.selection),
      is_dirty(buffer.DirtyFlag()) {}

Document::Document(std::filesystem::path p)
    : Document() {
    SetPath(std::move(p));
}

Document::Document(const Document& other)
    : buffer(other.buffer),
      session(other.session),
      path(other.path),
      lines(buffer.MutableLines()),
      type(other.type),
      line_ending(other.line_ending),
      cursor_row(session.cursor_row),
      cursor_col(session.cursor_col),
      selection(session.selection),
      is_dirty(buffer.DirtyFlag()),
      read_only(other.read_only),
      temporary(other.temporary),
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
      path(std::move(other.path)),
      lines(buffer.MutableLines()),
      type(other.type),
      line_ending(other.line_ending),
      cursor_row(session.cursor_row),
      cursor_col(session.cursor_col),
      selection(session.selection),
      is_dirty(buffer.DirtyFlag()),
      read_only(other.read_only),
      temporary(other.temporary),
      history(std::move(other.history)) {}

Document& Document::operator=(Document&& other) noexcept {
    if (this != &other) {
        buffer = std::move(other.buffer);
        session = std::move(other.session);
        path = std::move(other.path);
        type = other.type;
        line_ending = other.line_ending;
        read_only = other.read_only;
        temporary = other.temporary;
        history = std::move(other.history);
    }
    return *this;
}

void Document::BindFrom(const Document& other) {
    buffer = other.buffer;
    session = other.session;
    path = other.path;
    type = other.type;
    line_ending = other.line_ending;
    read_only = other.read_only;
    temporary = other.temporary;
    history = other.history;
}

void Document::SetPath(std::filesystem::path p) {
    path = std::move(p);
    type = DetermineDocumentType(path);
}

void Document::Reset() {
    path = "Untitled";
    type = DocumentType::PlainText;
    line_ending = LineEnding::LF;
    cursor_row = 0;
    cursor_col = 0;
    is_dirty = false;
    read_only = false;
    temporary = false;
    history.Clear();
    buffer.SetLines({""});
}

std::string Document::Label() const {
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

} // namespace textlt
