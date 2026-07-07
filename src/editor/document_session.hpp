#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

namespace textlt {

enum class DocumentType {
    PlainText,
    Bash,
    CMake,
    Cpp,
    Json,
    Markdown,
    Html,
    Css,
    Go,
    Js,
    Jsx,
    Ts,
    Tsx,
    Php,
    Blade,
    Rust,
    Java,
    Python,
    Xml
};

enum class LineEnding {
    LF,
    CRLF,
};

struct Selection {
    bool active = false;
    size_t anchor_x = 0;
    size_t anchor_y = 0;
};

class DocumentSession {
public:
    size_t cursor_row = 0;
    size_t cursor_col = 0;
    Selection selection;
    size_t scroll_x = 0;
    size_t scroll_y = 0;

    std::filesystem::path path = "Untitled";
    DocumentType type = DocumentType::PlainText;
    LineEnding line_ending = LineEnding::LF;
    bool read_only = false;
    bool temporary = false;

    void Reset();
    void SetPath(std::filesystem::path new_path);
    void RefreshLexer();

    bool HasSelection() const;
    bool IsMemoryOnly() const;

    std::string CurrentFilePath() const;
    std::string DisplayTitle() const;
    std::string TypeLabel() const;
    std::string LexerId() const;
    std::string CommentPrefix() const;
    std::string LineEndingLabel() const;

    static DocumentType DetermineDocumentType(const std::filesystem::path& path);
};

using EditorSession = DocumentSession;

} // namespace textlt
