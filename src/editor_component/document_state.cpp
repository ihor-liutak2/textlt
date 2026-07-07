    /**
     * @brief Sets the active session for the editor.
     * * Switches the editor context to a new document, synchronizes UI state,
     * and ensures cursor position is clamped to the new content boundaries.
     */
    void EditorComponent::SetSession(std::shared_ptr<DocumentSession> doc) {
        // Check if the provided document is valid to prevent null pointer dereference
        if (!doc) {
            return;
        }

        // Update the local session reference
        session_ = doc;

        // Ensure cursor is within buffer limits to prevent out-of-bounds access
        ClampCursorToBuffer();

        // Refresh the viewport and scroll position to match the new document content
        UpdateScroll();

        // Clear previous search highlights and selections to avoid stale UI states
        ClearSearchHighlights();
        ClearSelection();
    }

    std::shared_ptr<DocumentSession> EditorComponent::GetSession() const { return session_; }

    EditorComponent::EditorComponent(EditorConfig* config, const Theme* theme)
    : config_(config),
    theme_(theme),
    session_(std::make_shared<DocumentSession>()) { // Initialize default session

        if (config_) {
            search_match_case_ = config_->search_match_case;
            search_whole_word_ = config_->search_whole_word;
        }
    }

    ftxui::Element EditorComponent::Render() {
        return RenderViewport();
    }

    void EditorComponent::SaveToFile(const std::string& path) {
        if (!session_) return;
        if (session_->read_only) {
            throw std::runtime_error("Document is read-only");
        }

        const std::string save_path = path.empty() ? session_->path.string() : path;
        std::ofstream file(save_path, std::ios::binary);
        if (!file) {
            throw std::runtime_error("Unable to save file: " + save_path);
        }

        file << session_->ToContent();

        session_->SetPath(save_path);
        session_->is_dirty = false;
    }

    void EditorComponent::LoadFromFile(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            throw std::runtime_error("Unable to open file: " + path);
        }

        const std::string content{
            std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>()};

        if (!session_) session_ = std::make_shared<DocumentSession>();
        session_->LoadContent(content, path);
        scroll_x_ = 0;
        scroll_y_ = 0;
        ClearSearchHighlights();
        ClearSelection();
    }

    void EditorComponent::NewFile(const std::string& path) {
        if (!session_) session_ = std::make_shared<DocumentSession>();

        session_->Reset();
        session_->SetPath(path.empty() ? "Untitled" : path);

        scroll_x_ = 0;
        scroll_y_ = 0;
        ClearSearchHighlights();
        ClearSelection();
    }

    std::string EditorComponent::CurrentFilePath() const {
        return session_ ? session_->CurrentFilePath() : "Untitled";
    }

    bool EditorComponent::IsDirty() const {
        return session_ ? session_->is_dirty : false;
    }

    bool EditorComponent::IsReadOnly() const {
        return session_ ? session_->read_only : false;
    }

    LineEnding EditorComponent::ActiveLineEnding() const {
        return session_ ? session_->line_ending : LineEnding::LF;
    }

    std::string EditorComponent::ActiveLineEndingLabel() const {
        return session_ ? session_->LineEndingLabel() : "LF";
    }

    size_t EditorComponent::GetCursorRow() const {
        return session_ ? session_->cursor_row : 0;
    }

    size_t EditorComponent::GetCursorCol() const {
        return session_ ? session_->cursor_col : 0;
    }

    size_t EditorComponent::GetLineCount() const {
        return session_ ? session_->LineCount() : 0;
    }

    std::string EditorComponent::TextFromCursor() const {
        return session_ ? session_->TextFromCursor() : "";
    }

    std::string EditorComponent::GetAllText() const {
        return session_ ? session_->ToContent() : "";
    }

    void EditorComponent::JumpToLine(size_t line_number) {
        if (!session_) return;

        EndTypingGroup();
        session_->JumpToLine(line_number);
        ClearSelection();
        UpdateScroll();
    }

    void EditorComponent::SetCursorPosition(size_t row, size_t column) {
        if (!session_) return;

        EndTypingGroup();
        session_->SetCursorPosition(row, column);
        ClearSelection();
        UpdateScroll();
    }

    bool EditorComponent::HasSelection() const {
        return session_ && session_->HasSelection();
    }

    void EditorComponent::SelectAll() {
        if (!session_) return;
        EndTypingGroup();
        session_->SelectAll();
        UpdateScroll();
    }

    std::string EditorComponent::GetSelectedText() const {
        return session_ ? session_->GetSelectedText() : "";
    }

    void EditorComponent::DeleteSelection() {
        if (session_ && session_->DeleteSelection()) {
            UpdateScroll();
        }
    }

    void EditorComponent::DeleteSelectionWithoutSnapshot() {
        if (session_ && session_->DeleteSelectionWithoutSnapshot()) {
            UpdateScroll();
        }
    }

    size_t EditorComponent::FindMatchAtOrAfterCursor() const {
        if (search_matches_.empty() || !session_) {
            return 0;
        }

        for (size_t i = 0; i < search_matches_.size(); ++i) {
            const SearchMatch& match = search_matches_[i];
            if (match.y > session_->cursor_row ||
                (match.y == session_->cursor_row && match.x + match.length > session_->cursor_col)) {
                return i;
                }
        }

        return 0;
    }

    void EditorComponent::MoveCursorToSearchMatch(const SearchMatch& match) {
        if (!session_) return;
        session_->cursor_row = match.y;
        session_->cursor_col = match.x;
        ClearSelection();
        UpdateScroll();
    }

    std::string EditorComponent::GetCommentPrefix() const {
        return session_ ? session_->CommentPrefix() : "//";
    }
