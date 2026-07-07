    /**
     * @brief Sets the active document for the editor.
     * * Switches the editor context to a new document, synchronizes UI state,
     * and ensures cursor position is clamped to the new content boundaries.
     */
    void EditorComponent::SetDocumentSession(std::shared_ptr<DocumentSession> doc) {
        // Check if the provided document is valid to prevent null pointer dereference
        if (!doc) {
            return;
        }

        // Update the local document reference
        doc_ = doc;

        // Ensure cursor is within buffer limits to prevent out-of-bounds access
        ClampCursorToBuffer();

        // Refresh the viewport and scroll position to match the new document content
        UpdateScroll();

        // Clear previous search highlights and selections to avoid stale UI states
        ClearSearchHighlights();
        ClearSelection();
    }

    std::shared_ptr<DocumentSession> EditorComponent::GetDocumentSession() const { return doc_; }

    EditorComponent::EditorComponent(EditorConfig* config, const Theme* theme)
    : config_(config),
    theme_(theme),
    doc_(std::make_shared<DocumentSession>()) { // Initialize default document

        if (config_) {
            search_match_case_ = config_->search_match_case;
            search_whole_word_ = config_->search_whole_word;
        }
    }

    ftxui::Element EditorComponent::Render() {
        return RenderViewport();
    }

    void EditorComponent::SaveToFile(const std::string& path) {
        if (!doc_) return;
        if (doc_->read_only) {
            throw std::runtime_error("Document is read-only");
        }

        const std::string save_path = path.empty() ? doc_->path.string() : path;
        std::ofstream file(save_path, std::ios::binary);
        if (!file) {
            throw std::runtime_error("Unable to save file: " + save_path);
        }

        file << doc_->ToContent();

        doc_->SetPath(save_path);
        doc_->is_dirty = false;
    }

    void EditorComponent::LoadFromFile(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            throw std::runtime_error("Unable to open file: " + path);
        }

        const std::string content{
            std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>()};

        if (!doc_) doc_ = std::make_shared<DocumentSession>();
        doc_->LoadContent(content, path);
        scroll_x_ = 0;
        scroll_y_ = 0;
        ClearSearchHighlights();
        ClearSelection();
    }

    void EditorComponent::NewFile(const std::string& path) {
        if (!doc_) doc_ = std::make_shared<DocumentSession>();

        doc_->Reset();
        doc_->SetPath(path.empty() ? "Untitled" : path);

        scroll_x_ = 0;
        scroll_y_ = 0;
        ClearSearchHighlights();
        ClearSelection();
    }

    std::string EditorComponent::CurrentFilePath() const {
        return doc_ ? doc_->CurrentFilePath() : "Untitled";
    }

    bool EditorComponent::IsDirty() const {
        return doc_ ? doc_->is_dirty : false;
    }

    bool EditorComponent::IsReadOnly() const {
        return doc_ ? doc_->read_only : false;
    }

    LineEnding EditorComponent::ActiveLineEnding() const {
        return doc_ ? doc_->line_ending : LineEnding::LF;
    }

    std::string EditorComponent::ActiveLineEndingLabel() const {
        return doc_ ? doc_->LineEndingLabel() : "LF";
    }

    size_t EditorComponent::GetCursorRow() const {
        return doc_ ? doc_->cursor_row : 0;
    }

    size_t EditorComponent::GetCursorCol() const {
        return doc_ ? doc_->cursor_col : 0;
    }

    size_t EditorComponent::GetLineCount() const {
        return doc_ ? doc_->LineCount() : 0;
    }

    std::string EditorComponent::TextFromCursor() const {
        return doc_ ? doc_->TextFromCursor() : "";
    }

    std::string EditorComponent::GetAllText() const {
        return doc_ ? doc_->ToContent() : "";
    }

    void EditorComponent::JumpToLine(size_t line_number) {
        if (!doc_) return;

        EndTypingGroup();
        doc_->JumpToLine(line_number);
        ClearSelection();
        UpdateScroll();
    }

    void EditorComponent::SetCursorPosition(size_t row, size_t column) {
        if (!doc_) return;

        EndTypingGroup();
        doc_->SetCursorPosition(row, column);
        ClearSelection();
        UpdateScroll();
    }

    bool EditorComponent::HasSelection() const {
        return doc_ && doc_->HasSelection();
    }

    void EditorComponent::SelectAll() {
        if (!doc_) return;
        EndTypingGroup();
        doc_->SelectAll();
        UpdateScroll();
    }

    std::string EditorComponent::GetSelectedText() const {
        return doc_ ? doc_->GetSelectedText() : "";
    }

    void EditorComponent::DeleteSelection() {
        if (doc_ && doc_->DeleteSelection()) {
            UpdateScroll();
        }
    }

    void EditorComponent::DeleteSelectionWithoutSnapshot() {
        if (doc_ && doc_->DeleteSelectionWithoutSnapshot()) {
            UpdateScroll();
        }
    }

    size_t EditorComponent::FindMatchAtOrAfterCursor() const {
        if (search_matches_.empty() || !doc_) {
            return 0;
        }

        for (size_t i = 0; i < search_matches_.size(); ++i) {
            const SearchMatch& match = search_matches_[i];
            if (match.y > doc_->cursor_row ||
                (match.y == doc_->cursor_row && match.x + match.length > doc_->cursor_col)) {
                return i;
                }
        }

        return 0;
    }

    void EditorComponent::MoveCursorToSearchMatch(const SearchMatch& match) {
        if (!doc_) return;
        doc_->cursor_row = match.y;
        doc_->cursor_col = match.x;
        ClearSelection();
        UpdateScroll();
    }

    std::string EditorComponent::GetCommentPrefix() const {
        return doc_ ? doc_->CommentPrefix() : "//";
    }
