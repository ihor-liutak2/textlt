#include "editor/document_session.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <utility>

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
    cursor_row = 0;
    cursor_col = 0;
    selection = {};
    scroll_x = 0;
    scroll_y = 0;
    path = "Untitled";
    type = DocumentType::PlainText;
    line_ending = LineEnding::LF;
    read_only = false;
    temporary = false;
}

void DocumentSession::SetPath(std::filesystem::path new_path) {
    path = std::move(new_path);
    RefreshLexer();
}

void DocumentSession::RefreshLexer() {
    type = DetermineDocumentType(path);
}

bool DocumentSession::HasSelection() const {
    return selection.active &&
        (cursor_col != selection.anchor_x || cursor_row != selection.anchor_y);
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

} // namespace textlt
