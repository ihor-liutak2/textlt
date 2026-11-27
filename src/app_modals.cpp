#include "app.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "app_resources.hpp"
#include "ai/ai_active_settings.hpp"
#include "connect_workspace.hpp"
#include "editor/document_session.hpp"
#include "ftxui/component/event.hpp"
#include "theme.hpp"
#include "path_utils.hpp"

namespace textlt {
namespace {

size_t ParsePositiveNumberClamped(const std::string& value, size_t maximum_value, bool* has_digits = nullptr) {
    maximum_value = std::max<size_t>(maximum_value, 1);
    size_t parsed = 0;
    bool found_digit = false;
    for (unsigned char character : value) {
        if (character < '0' || character > '9') {
            continue;
        }
        found_digit = true;
        const size_t digit = static_cast<size_t>(character - '0');
        if (parsed > (maximum_value - digit) / 10) {
            parsed = maximum_value;
        } else {
            parsed = parsed * 10 + digit;
        }
    }
    if (has_digits) {
        *has_digits = found_digit;
    }
    if (!found_digit) {
        return 1;
    }
    return std::clamp(parsed, size_t{1}, maximum_value);
}

std::vector<std::string> BuiltInHelpLines() {
    return {
        "textlt Help",
        "",
        "File",
        "  Ctrl+O        Open a file",
        "  Ctrl+S        Save the current file",
        "  Ctrl+Q        Exit the editor",
        "",
        "Navigation",
        "  Arrow Keys    Move the cursor",
        "  Home          Move to the start of the current line",
        "  End           Move to the end of the current line",
        "  Ctrl+Home     Move to the start of the document",
        "  Ctrl+End      Move to the end of the document",
        "  Page Up       Move one page up",
        "  Page Down     Move one page down",
        "  Ctrl+Left     Jump to the previous word",
        "  Ctrl+Right    Jump to the next word",
        "  Alt+T         Go to line",
        "  Ctrl+Up       Jump to the previous paragraph",
        "  Ctrl+Down     Jump to the next paragraph",
        "  Shift+Ctrl+Up Select to the previous paragraph",
        "  Shift+Ctrl+Down Select to the next paragraph",
        "",
        "Editing",
        "  Ctrl + /      Toggle Comment",
        "  Ctrl + T      Toggle Case",
        "  Tab           Indent selected lines, or insert spaces",
        "  Shift+Tab     Remove one indent level",
        "  Alt+Backspace Delete the previous word",
        "  Ctrl+Delete   Delete the next word",
        "",
        "Selection & Clipboard",
        "  Shift+Arrows  Extend the active text selection",
        "  Shift+Home/End Select to the start/end of the line",
        "  Shift+PageUp/Down Select one page up/down",
        "  Ctrl+Shift+Home/End Select to document start/end",
        "  Ctrl+Shift+Up/Down Select by paragraph boundary",
        "  Ctrl+Click    Select from the cursor to the clicked position",
        "  Shift+Click   Same when forwarded by the terminal",
        "  Mouse Drag    Select text inside the editor viewport",
        "  Double Click  Select a word",
        "  Triple Click  Select a logical line",
        "  Escape        Clear the active selection",
        "  Ctrl+A        Select all text",
        "  Ctrl+C        Copy selection or current line",
        "  Ctrl+X        Cut selection or current line",
        "  Ctrl+V        Paste clipboard text",
        "  Ctrl+Shift+V  Paste clipboard text",
        "",
        "TTS / AI",
        "  Alt+H         Open Text-to-Speech",
        "  Alt+S         Open TTS Settings",
        "  Tools > AI Settings opens AI Settings",
        "",
        "Menus",
        "  Tab           Switch focus from File Explorer to Editor",
        "  Alt+Left      Switch to the previous editor pane",
        "  Alt+Right     Switch to the next editor pane",
        "  Ctrl+B        Open Quick Commands",
        "  Alt+B         Open Quick Text Formatting",
        "  Alt+M         Open Quick AI",
        "  B             Toggle sidebar",
        "  O             Open the Opened files tab in Text",
        "  F             Open the Favorites tab in Text",
        "  D             Open the Folder tab in Text",
        "  P             Open the Projects tab in Text",
        "  R             Open Recent Files from Quick Commands",
        "  Project > Copy Path copies the selected file or directory path",
        "  F1            Open this help window",
        "  F2            Open the File menu",
        "  F3            Open the Edit menu",
        "  F4            Open the Options menu",
        "  Escape        Close the active dialog or menu",
        "",
        "Options",
        "  Toggle Line Numbers changes the editor gutter immediately.",
        "  Toggle File Explorer shows or hides the sidebar.",
        "  Auto Pairing inserts matching brackets and quotes.",
        "  Smart Auto-Indent carries indentation onto new lines.",
        "  Theme opens the dynamic theme selector.",
        "  Help > Theme Color Settings customizes or resets the active theme.",
        "  View Layout opens the pane and column manager.",
        "",
        "Syntax Highlighting",
        "  Language        Supported Extensions / Filenames",
        "  C / C++         .c, .cpp, .hpp, .h",
        "  C#              .cs",
        "  Go              .go",
        "  Rust            .rs",
        "  Bash Scripting  .sh, .bash, .bashrc",
        "  JavaScript      .js, .mjs, .cjs",
        "  TypeScript      .ts, .mts",
        "  React JSX/TSX   .jsx, .tsx",
        "  GraphQL         .graphql, .gql",
        "  Laravel Blade   .blade.php",
        "  PHP             .php",
        "  Python          .py",
        "  Ruby            .rb, Gemfile",
        "  Java            .java",
        "  HTML            .html, .htm",
        "  XML             .xml, .xsd, .xsl, .xslt",
        "  CSS             .css",
        "  JSON            .json",
        "  Lua             .lua",
        "  INI             .ini, .conf",
        "  Environment     .env, .env.local, .env.*",
        "  YAML            .yaml, .yml",
        "  Docker          Dockerfile, docker-compose.yml",
        "  SQL             .sql",
    };
}

} // namespace


void TextltApp::OpenAboutDialog() {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }
    help_dialog_.OpenContent("About", {
        "textlt — Modern TUI Text Editor",
        "Version: " TEXTLT_VERSION " (Stable Release)",
        "",
        "Author: Ihor Liutak (ihorlt)",
        "Contact: ihorlt@gmail.com",
        "Origin: Ukraine 🇺🇦",
        "",
        "Built with: C++17 & FTXUI Framework",
        "License: MIT License (c) 2026",
    }, true);
    active_action_ = "Opened About";
    SetActiveLayer(UiLayer::Help);
}


void TextltApp::OpenHelpDialog() {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }
    help_dialog_.OpenContent("Help", BuiltInHelpLines(), false);
    active_action_ = "Opened Help";
    SetActiveLayer(UiLayer::Help);
}


void TextltApp::CloseHelpDialog() {
    help_dialog_.Close();
    SetActiveLayer(UiLayer::Main);
    FocusEditor();
}


void TextltApp::OpenKeyboardShortcutsModal() {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }
    keyboard_shortcuts_modal_.Open();
    active_action_ = "Opened Keyboard Shortcuts";
    SetActiveLayer(UiLayer::KeyboardShortcuts);
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::CloseKeyboardShortcutsModal() {
    keyboard_shortcuts_modal_.Close();
    SetActiveLayer(UiLayer::Main);
    FocusEditor();
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::OpenSidebarShortcutModal() {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }
    sidebar_shortcuts_modal_.Open();
    active_action_ = "Opened Quick Commands";
    SetActiveLayer(UiLayer::SidebarShortcuts);
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::CloseSidebarShortcutModal() {
    sidebar_shortcuts_modal_.Close();
    SetActiveLayer(UiLayer::Main);
    FocusEditor();
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::OpenCustomProcessorBuilderModal() {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }
    custom_processor_builder_modal_.Open();
    active_action_ = "Opened Custom Processor Builder";
    SetActiveLayer(UiLayer::CustomProcessorBuilder);
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::CloseCustomProcessorBuilderModal() {
    custom_processor_builder_modal_.Close();
    SetActiveLayer(UiLayer::Main);
    FocusEditor();
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::OpenRecentFilesModal() {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }
    recent_files_modal_.Open();
    active_action_ = "Opened Recent Files";
    SetActiveLayer(UiLayer::RecentFiles);
}


void TextltApp::CloseRecentFilesModal() {
    recent_files_modal_.Close();
    FocusEditor();
}


void TextltApp::OpenSearchFilesModal() {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }

    search_files_modal_.Open();
    active_action_ = "Opened Search in Files";
    SetActiveLayer(UiLayer::SearchFiles);
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::CloseSearchFilesModal() {
    search_files_modal_.Close();
    FocusEditor();
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::OpenFilesModal(FilesModalMode mode) {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }

    std::filesystem::path start_path = CurrentSidebarDirectory();
    std::string suggested_file_name;
    if (mode == FilesModalMode::SaveAs) {
        const auto document = pending_close_session_ ? pending_close_session_ : ActiveSessionPtr();
        const std::string current_path = document ? document->CurrentFilePath() : std::string{};
        if (!current_path.empty() &&
            current_path != "Untitled" &&
            current_path != "untitled.txt") {
            const auto current_file_path = PathFromUtf8(current_path);
            suggested_file_name = Utf8FromPath(current_file_path.filename());
            if (!current_file_path.parent_path().empty()) {
                start_path = current_file_path.parent_path();
            }
        }
    }

    files_modal_.Open(mode, start_path, suggested_file_name);
    active_action_ = "Opened " +
        std::string(mode == FilesModalMode::Open ? "Open" :
            mode == FilesModalMode::SaveAs ? "Save As" :
            mode == FilesModalMode::Import ? "Import" :
            mode == FilesModalMode::ImportActions ? "Import Actions" :
            mode == FilesModalMode::ImportNotesMarkdown ? "Import Markdown into Notes" :
            mode == FilesModalMode::SendConnectFile ? "Send File" :
            mode == FilesModalMode::OpenFolder ? "Open Folder" :
            mode == FilesModalMode::SelectDirectory ? "Add Project" :
            "Files") +
        " modal";
    sidebar_has_focus_ = false;
    SetActiveLayer(UiLayer::Files);
    if (auto view = files_modal_.View()) {
        view->TakeFocus();
    }
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::CloseFilesModal() {
    files_modal_.Close();
    if (sidebar_has_focus_) FocusSidebar();
    else FocusEditor();
    sidebar_has_focus_ = false;
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::OpenTextProcessorsModal(bool quick) {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }

    if (quick) {
        auto editor = std::static_pointer_cast<EditorComponent>(text_editor_);
        text_processors_modal_.OpenQuick(!editor || !editor->HasSelection());
    } else {
        text_processors_modal_.Open();
    }
    active_action_ = quick ? "Opened Quick Text Formatting" : "Opened Text Processors";
    SetActiveLayer(UiLayer::TextProcessors);
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::CloseTextProcessorsModal() {
    text_processors_modal_.Close();
    FocusEditor();
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::OpenRemoteConnectionsModal() {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }

    remote_connections_modal_.Open();
    active_action_ = "Opened Remote Connections";
    SetActiveLayer(UiLayer::RemoteConnections);
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::CloseRemoteConnectionsModal() {
    remote_connections_modal_.Close();
    FocusEditor();
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::OpenRemoteFilesModal() {
    std::string connection_id;
    if (const ProjectRecord* project = ActiveWorkspaceProject()) {
        std::string error;
        if (!project->remote_connection_id.empty() && remote_config_store_.Load(error) &&
            remote_config_store_.FindById(project->remote_connection_id)) {
            connection_id = project->remote_connection_id;
        }
    }

    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }

    if (connection_id.empty()) {
        remote_files_modal_.Open();
    } else {
        remote_files_modal_.OpenForConnection(connection_id);
    }
    active_action_ = "Opened Remote Files";
    SetActiveLayer(UiLayer::RemoteFiles);
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::CloseRemoteFilesModal() {
    remote_files_modal_.Close();
    FocusEditor();
    screen_.PostEvent(ftxui::Event::Custom);
}


bool TextltApp::GetTextProcessorTargetText(
    ProcessorTarget target,
    std::string& text,
    std::string& error) {
    auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
    const auto document = editor_ptr ? editor_ptr->GetSession() : document_workspace_.ActiveSessionPtr();
    if (!document) {
        error = "No active document.";
        active_action_ = error;
        screen_.PostEvent(ftxui::Event::Custom);
        return false;
    }

    auto& session = *document;
    if (target == ProcessorTarget::Paragraph) {
        return session.GetCurrentParagraphText(text, error);
    }
    if (!session.GetTextProcessorTargetText(target == ProcessorTarget::Document, text, error)) {
        active_action_ = error;
        screen_.PostEvent(ftxui::Event::Custom);
        return false;
    }
    return true;
}


bool TextltApp::ReplaceTextProcessorTargetText(
    ProcessorTarget target,
    const std::string& text,
    std::string& error) {
    auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
    const auto document = editor_ptr ? editor_ptr->GetSession() : document_workspace_.ActiveSessionPtr();
    if (!document) {
        error = "No active document.";
        active_action_ = error;
        screen_.PostEvent(ftxui::Event::Custom);
        return false;
    }

    auto& session = *document;
    bool applied;
    if (target == ProcessorTarget::Paragraph) {
        applied = session.ReplaceCurrentParagraphText(text, error);
    } else {
        applied = session.ReplaceTextProcessorTargetText(target == ProcessorTarget::Document, text, error);
    }
    if (!applied) {
        active_action_ = error;
        screen_.PostEvent(ftxui::Event::Custom);
        return false;
    }

    if (editor_ptr) {
        editor_ptr->SetSession(document);
    }

    switch (target) {
        case ProcessorTarget::Document: active_action_ = "Applied text processor to whole document"; break;
        case ProcessorTarget::Paragraph: active_action_ = "Applied text processor to current paragraph"; break;
        case ProcessorTarget::Selection: active_action_ = "Applied text processor to selected text"; break;
    }
    FocusEditor();
    screen_.PostEvent(ftxui::Event::Custom);
    return true;
}


bool TextltApp::InsertImportedText(
    const std::filesystem::path& path,
    const std::string& text,
    std::string& error) {
    if (text.empty()) {
        error = "Imported file does not contain plain text.";
        active_action_ = error;
        screen_.PostEvent(ftxui::Event::Custom);
        return false;
    }

    // DEBUG: dump imported text to file
    {
        std::ofstream dbg("/tmp/textlt_inserted.txt", std::ios::trunc);
        dbg << "Length: " << text.size() << " bytes, " << text.size() << " chars\n";
        dbg << "---BEGIN---\n";
        dbg << text;
        dbg << "\n---END---\n";
        // Show each line
        std::istringstream ss(text);
        std::string line;
        int i = 0;
        while (std::getline(ss, line)) {
            dbg << "LINE " << i++ << ": [" << line << "]\n";
        }
    }

    auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
    editor_ptr->InsertText(text, true);

    const std::string filename = path.filename().string();
    active_action_ = filename.empty()
        ? "Imported text"
        : "Imported text from " + filename;

    // The files modal closes after this callback returns. Do not switch the
    // active component tree from inside its mouse/key event; the queued custom
    // event from ConfirmFilesModalAction restores editor focus once the modal
    // is fully closed.
    return true;
}


void TextltApp::OpenGitModal() {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }

    git_manager_.RefreshNow();
    git_modal_.Open();
    active_action_ = "Opened Git";
    SetActiveLayer(UiLayer::Git);
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::OpenGitBranchesModal() {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }

    git_manager_.RefreshNow();
    git_modal_.OpenBranches();
    active_action_ = "Opened Git Branches";
    SetActiveLayer(UiLayer::Git);
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::CloseGitModal() {
    git_modal_.Close();
    FocusEditor();
    screen_.PostEvent(ftxui::Event::Custom);
}




bool TextltApp::OpenGitCompareDocuments(
    const std::string& left_title,
    const std::string& left_content,
    const std::string& right_title,
    const std::string& right_content,
    std::string& error) {
    if (left_title.empty()) {
        error = "Git compare title is empty.";
        return false;
    }

    auto make_git_doc = [](const std::string& title, const std::string& content) {
        const size_t revision_separator = title.find(" @ ");
        const std::string source_path = revision_separator == std::string::npos
            ? title : title.substr(0, revision_separator);
        auto doc = std::make_shared<DocumentSession>();
        doc->LoadContent(content, source_path.empty() ? "Git compare.txt" : source_path);
        doc->read_only = true;
        doc->temporary = true;
        doc->is_dirty = false;
        doc->git_compare = true;
        doc->lexer_path = source_path;
        doc->git_compare_label = revision_separator == std::string::npos
            ? std::string{} : title.substr(revision_separator);
        return doc;
    };

    auto left_doc = make_git_doc(left_title, left_content);
    auto right_doc = make_git_doc(right_title, right_content);
    const std::vector<std::string> source_left = left_doc->lines;
    const std::vector<std::string> source_right = right_doc->lines;
    const size_t left_count = left_doc->lines.size();
    const size_t right_count = right_doc->lines.size();
    std::vector<std::string> aligned_left;
    std::vector<std::string> aligned_right;
    std::vector<DiffLineKind> left_kinds;
    std::vector<DiffLineKind> right_kinds;
    auto append_row = [&](const std::string& left_line, const std::string& right_line,
                          DiffLineKind left_kind, DiffLineKind right_kind) {
        aligned_left.push_back(left_line);
        aligned_right.push_back(right_line);
        left_kinds.push_back(left_kind);
        right_kinds.push_back(right_kind);
    };
    if (left_count <= 2500 && right_count <= 2500 &&
        (left_count + 1) * (right_count + 1) <= 2500000) {
        const size_t width = right_count + 1;
        std::vector<uint32_t> lcs((left_count + 1) * width, 0);
        for (size_t i = left_count; i-- > 0;) {
            for (size_t j = right_count; j-- > 0;) {
                lcs[i * width + j] = left_doc->lines[i] == right_doc->lines[j]
                    ? lcs[(i + 1) * width + j + 1] + 1
                    : std::max(lcs[(i + 1) * width + j], lcs[i * width + j + 1]);
            }
        }
        size_t i = 0, j = 0;
        while (i < left_count || j < right_count) {
            if (i < left_count && j < right_count && source_left[i] == source_right[j]) {
                append_row(source_left[i], source_right[j], DiffLineKind::None, DiffLineKind::None);
                ++i; ++j;
                continue;
            }
            std::vector<std::string> deleted_lines;
            std::vector<std::string> added_lines;
            while (i < left_count || j < right_count) {
                if (i < left_count && j < right_count && source_left[i] == source_right[j]) break;
                if (j == right_count || (i < left_count && lcs[(i + 1) * width + j] >= lcs[i * width + j + 1])) {
                    deleted_lines.push_back(source_left[i++]);
                } else {
                    added_lines.push_back(source_right[j++]);
                }
            }
            const size_t paired = std::min(deleted_lines.size(), added_lines.size());
            for (size_t k = 0; k < paired; ++k) {
                append_row(deleted_lines[k], added_lines[k], DiffLineKind::Changed, DiffLineKind::Changed);
            }
            for (size_t k = paired; k < deleted_lines.size(); ++k)
                append_row(deleted_lines[k], "", DiffLineKind::Deleted, DiffLineKind::Deleted);
            for (size_t k = paired; k < added_lines.size(); ++k)
                append_row("", added_lines[k], DiffLineKind::Added, DiffLineKind::Added);
        }
    } else {
        const size_t rows = std::max(left_count, right_count);
        for (size_t i = 0; i < rows; ++i) {
            const bool has_left = i < left_count;
            const bool has_right = i < right_count;
            if (has_left && has_right) {
                const auto kind = source_left[i] == source_right[i]
                    ? DiffLineKind::None : DiffLineKind::Changed;
                append_row(source_left[i], source_right[i], kind, kind);
            } else if (has_left) {
                append_row(source_left[i], "", DiffLineKind::Deleted, DiffLineKind::Deleted);
            } else {
                append_row("", source_right[i], DiffLineKind::Added, DiffLineKind::Added);
            }
        }
    }
    left_doc->lines = std::move(aligned_left);
    right_doc->lines = std::move(aligned_right);
    left_doc->diff_line_kinds = std::move(left_kinds);
    right_doc->diff_line_kinds = std::move(right_kinds);
    left_doc->EnsureValidBuffer();
    right_doc->EnsureValidBuffer();
    left_doc->is_dirty = false;
    right_doc->is_dirty = false;

    CloseGitCompare();
    if (!right_title.empty()) {
        if (layout_controller_.IsDistractionModeActive()) SetDistractionEnabled(false);
        GitCompareState state{
            layout_controller_.Mode(), document_workspace_.ActiveEditorPaneIndex(),
            document_workspace_.ActiveSessionPtr(), {}, {left_doc, right_doc}
        };
        for (size_t pane = 0; pane < state.previous_panes.size(); ++pane) {
            state.previous_panes[pane] = {
                document_workspace_.SessionPtrAt(document_workspace_.PaneSessionIndex(pane)),
                document_workspace_.PaneViewport(pane)->CursorState()
            };
        }
        git_compare_state_ = std::move(state);
    }

    const size_t left_index = document_workspace_.AddSession(left_doc);
    size_t right_index = left_index;
    if (!right_title.empty()) {
        right_index = document_workspace_.AddSession(right_doc);
    }

    if (right_title.empty()) {
        SetEditorLayoutMode(EditorLayoutMode::Single);
        AssignSessionToEditorPane(0, left_index);
        SetActiveEditorPane(0);
    } else {
        SetEditorLayoutMode(EditorLayoutMode::TwoColumns);
        AssignSessionToEditorPane(0, left_index);
        AssignSessionToEditorPane(1, right_index);
        SetActiveEditorPane(0);
    }

    document_workspace_.SetActiveSessionIndex(left_index);
    active_action_ = right_title.empty()
        ? "Opened Git diff"
        : "Opened Git side-by-side compare";
    UpdateFileMenuLabels();
    FocusEditor();
    screen_.PostEvent(ftxui::Event::Custom);
    return true;
}


void TextltApp::OpenGitSettingsModal() {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }

    git_settings_modal_.Open();
    active_action_ = "Opened Git Settings";
    SetActiveLayer(UiLayer::GitSettings);
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::CloseGitSettingsModal() {
    git_settings_modal_.Close();
    FocusEditor();
    screen_.PostEvent(ftxui::Event::Custom);
}


std::vector<FileSearchRoot> TextltApp::CurrentSearchFileRoots() const {
    const std::filesystem::path root = CurrentSidebarDirectory();

    std::string label = root.filename().string();
    if (label.empty()) {
        label = root.string();
    }
    if (label.empty()) {
        label = "Current Folder";
    }

    return {
        FileSearchRoot{root, label}
    };
}


bool TextltApp::OpenSearchFileMatch(const FileSearchMatch& match, std::string& error) {
    if (!OpenFile(Utf8FromPath(match.path), error)) {
        return false;
    }

    active_action_ = "Opened search result " + match.relative_path.generic_string();
    if (match.line_number != 0) {
        auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
        editor_ptr->JumpToLine(match.line_number);
        active_action_ += ":" + std::to_string(match.line_number);
    }

    FocusEditor();
    screen_.PostEvent(ftxui::Event::Custom);
    return true;
}


void TextltApp::OpenTtsModal() {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }
    SetTtsHeaderActiveButton(TtsHeaderButton::Open);
    tts_modal_.Open();
    active_action_ = "Opened Text-to-Speech";
    SetActiveLayer(UiLayer::Tts);
}


void TextltApp::CloseTtsModal() {
    tts_modal_.Close();
    FocusEditor();
}


void TextltApp::OpenViewLayoutModal() {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }
    SetActiveLayer(UiLayer::ViewLayout);
    view_layout_modal_.Open();
    active_action_ = "Opened View Layout";
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::CloseViewLayoutModal() {
    view_layout_modal_.Close();
    FocusEditor();
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::OpenQuickAiModal() {
    std::string source;
    QuickAiModal::ApplyResult apply;
    if (workspace_mode_ == WorkspaceMode::Documents) {
        if (const auto editor = ActiveEditor()) {
            if (const auto session = editor->GetSession()) {
                const bool selected = editor->HasSelection();
                if (selected) {
                    source = editor->GetSelectedText();
                } else {
                    std::string error;
                    session->GetCurrentParagraphText(source, error);
                }
                if (!source.empty() && !session->read_only) {
                    const auto version = session->buffer.Version();
                    const auto selection = session->SelectionState();
                    const auto row = editor->GetCursorRow();
                    const auto column = editor->GetCursorCol();
                    apply = [this, editor, session, version, selection, row, column, selected]
                        (const std::string& result, std::string& error) {
                        if (workspace_mode_ != WorkspaceMode::Documents || ActiveEditor() != editor ||
                            editor->GetSession() != session || session->buffer.Version() != version) {
                            error = "The original document has changed. Reopen Quick AI to capture its text again.";
                            return false;
                        }
                        if (session->read_only) {
                            error = "The original document is read-only.";
                            return false;
                        }
                        session->SetCursorPosition(row, column);
                        if (selected) {
                            session->SetSelectionAnchor(selection.anchor_y, selection.anchor_x);
                            session->SetSelectionActive(true);
                            if (session->GetSelectedText() != result) {
                                editor->InsertText(result);
                            }
                        } else if (!session->ReplaceCurrentParagraphText(result, error)) {
                            return false;
                        } else {
                            editor->SetSession(session);
                        }
                        active_action_ = selected ? "Changed selected text" : "Changed current paragraph";
                        return true;
                    };
                }
            }
        }
    } else if (workspace_mode_ == WorkspaceMode::Notes && notes_workspace_component_) {
        const auto notes = std::static_pointer_cast<notes::NotesWorkspaceComponent>(notes_workspace_component_);
        if (auto target = notes->CaptureTextTarget()) {
            source = target->text;
            apply = [this, notes, target = std::move(*target)](const std::string& result, std::string& error) {
                if (workspace_mode_ != WorkspaceMode::Notes ||
                    !notes->ReplaceTextTarget(target, result, error)) {
                    return false;
                }
                active_action_ = error.empty() ? "Changed note text" : error;
                return true;
            };
        }
    }
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }
    SetActiveLayer(UiLayer::QuickAi);
    quick_ai_modal_.Open(ActiveAiSettings(editor_config_), std::move(source), std::move(apply));
    active_action_ = "Opened Quick AI";
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::CloseQuickAiModal() {
    quick_ai_modal_.Close();
    FocusEditor();
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::OpenAiSettingsModal() {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }
    ai_settings_modal_.Open();
    active_action_ = "Opened AI Settings";
    SetActiveLayer(UiLayer::AiSettings);
}


void TextltApp::CloseAiSettingsModal() {
    ai_settings_modal_.Close();
    FocusEditor();
}


void TextltApp::OpenAssistantSettingsModal() {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }
    EnsureAssistantResources();
    assistant_settings_modal_.Open();
    active_action_ = "Opened TTS Settings";
    SetActiveLayer(UiLayer::AssistantSettings);
}


void TextltApp::CloseAssistantSettingsModal() {
    assistant_settings_modal_.Close();
    FocusEditor();
}


void TextltApp::OpenThemeDialog() {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }
    SetActiveLayer(UiLayer::Theme);
    theme_dialog_.Open(themes_, editor_config_.active_theme_name);
}


void TextltApp::CloseThemeDialog() {
    theme_dialog_.Close();
    SetActiveLayer(UiLayer::Main);
    FocusEditor();
}


void TextltApp::OpenThemeColorsModal() {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }
    SetActiveLayer(UiLayer::ThemeColors);
    theme_colors_modal_.Open();
    active_action_ = "Opened Theme Color Settings";
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::CloseThemeColorsModal() {
    theme_colors_modal_.Close();
    SetActiveLayer(UiLayer::Main);
    FocusEditor();
}


void TextltApp::PreviewTheme(const std::string& theme_name) {
    current_theme_ = FindThemeByName(themes_, theme_name);
    const auto overrides = editor_config_.theme_color_overrides.find(current_theme_.name);
    if (overrides != editor_config_.theme_color_overrides.end()) {
        ApplyThemeColorOverrides(current_theme_, overrides->second);
    }
    connect_workspace_->UpdateBrowserTheme();
    editor_config_.SetActiveThemeName(current_theme_.name);
    active_action_ = "Previewing theme " + current_theme_.name;
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::SelectTheme(const std::string& theme_name) {
    current_theme_ = FindThemeByName(themes_, theme_name);
    const auto overrides = editor_config_.theme_color_overrides.find(current_theme_.name);
    if (overrides != editor_config_.theme_color_overrides.end()) {
        ApplyThemeColorOverrides(current_theme_, overrides->second);
    }
    connect_workspace_->UpdateBrowserTheme();
    editor_config_.SetActiveThemeName(current_theme_.name);
    active_action_ = "Theme changed to " + current_theme_.name;
    screen_.PostEvent(ftxui::Event::Custom);
    CloseThemeDialog();
}


void TextltApp::SaveConfig() {
    config_manager_.Save(editor_config_);
}


void TextltApp::ShowUnsavedChangesDialog(const std::string& title) {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }

    unsaved_changes_dialog_.Open(title);
    SetActiveLayer(UiLayer::UnsavedChanges);
    active_action_ = "Unsaved changes";
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::CancelUnsavedChanges() {
    unsaved_changes_dialog_.Close();
    pending_close_session_.reset();
    exit_after_save_as_ = false;
    FocusEditor();
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::RequestExit() {
    auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
    const bool has_only_empty_memory_document =
        document_workspace_.SessionCount() == 1 &&
        DocumentWorkspace::IsMemoryOnlySession(document_workspace_.SessionAt(0)) &&
        editor_ptr->GetAllText().empty();
    if (has_only_empty_memory_document) {
        PersistActiveFavoriteCursor();
        screen_.Exit();
        return;
    }

    if (editor_ptr->IsDirty()) {
        const auto document = ActiveSessionPtr();
        ShowUnsavedChangesDialog(document ? document->DisplayTitle() : "Untitled");
        return;
    }
    PersistActiveFavoriteCursor();
    screen_.Exit();
}


void TextltApp::SavePendingChanges() {
    const auto document = pending_close_session_ ? pending_close_session_ : ActiveSessionPtr();
    if (DocumentWorkspace::IsMemoryOnlySession(document)) {
        unsaved_changes_dialog_.Close();
        exit_after_save_as_ = !pending_close_session_;
        OpenFilesModal(FilesModalMode::SaveAs);
        return;
    }

    std::string error;
    if (document && SaveFile(document, document->path, error)) {
        FinishPendingClose();
        return;
    }

    active_action_ = error.empty() ? "Save failed" : error;
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::FinishPendingClose() {
    if (pending_close_session_) {
        unsaved_changes_dialog_.Close();
        CloseOpenSession(std::exchange(pending_close_session_, {}));
        return;
    }

    PersistActiveFavoriteCursor();
    screen_.Exit();
}


void TextltApp::OpenGoToLinePanel() {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }
    std::static_pointer_cast<EditorComponent>(text_editor_)->ClearSearchHighlights();
    navigation_popup_mode_ = NavigationPopupMode::GoToLine;
    show_goto_line_bar_ = true;
    goto_line_input_.clear();
    goto_line_input_cursor_position_ = 0;
    SetActiveLayer(UiLayer::GoToLine);
    if (goto_line_popup_container_) {
        goto_line_popup_container_->TakeFocus();
    }
    if (goto_line_input_component_) {
        goto_line_input_component_->TakeFocus();
    }
    active_action_ = "Go to line";
}


void TextltApp::OpenDistractionPagePanel() {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }
    navigation_popup_mode_ = NavigationPopupMode::GoToPage;
    show_goto_line_bar_ = true;
    const DistractionTopBarState state = CurrentDistractionTopBarState();
    goto_line_input_ = state.page_input.empty() ? std::to_string(std::max<size_t>(state.current_page, 1)) : state.page_input;
    goto_line_input_cursor_position_ = static_cast<int>(goto_line_input_.size());
    SetActiveLayer(UiLayer::GoToLine);
    if (goto_line_popup_container_) {
        goto_line_popup_container_->TakeFocus();
    }
    if (goto_line_input_component_) {
        goto_line_input_component_->TakeFocus();
    }
    active_action_ = "Go to page";
}


void TextltApp::CloseGoToLinePanel() {
    show_goto_line_bar_ = false;
    navigation_popup_mode_ = NavigationPopupMode::GoToLine;
    FocusEditor();
}


void TextltApp::SubmitGoToLine() {
    if (navigation_popup_mode_ == NavigationPopupMode::GoToPage) {
        const DistractionTopBarState state = CurrentDistractionTopBarState();
        const size_t total_pages = std::max<size_t>(state.total_pages, 1);
        bool has_digits = false;
        const size_t page_number = ParsePositiveNumberClamped(
            goto_line_input_,
            total_pages,
            &has_digits);
        if (!has_digits) {
            active_action_ = "Invalid page number";
            if (goto_line_input_component_) {
                goto_line_input_component_->TakeFocus();
            }
            screen_.PostEvent(ftxui::Event::Custom);
            return;
        }

        goto_line_input_ = std::to_string(page_number);
        goto_line_input_cursor_position_ = static_cast<int>(goto_line_input_.size());
        distraction_controller_.SetPageInput(goto_line_input_);
        if (distraction_controller_.GoToPage(
                &document_workspace_,
                layout_controller_.VisiblePaneCount(),
                &editor_config_)) {
            active_action_ = "Jumped to page " + goto_line_input_;
        } else {
            active_action_ = "Unable to jump to page";
        }
        show_goto_line_bar_ = false;
        navigation_popup_mode_ = NavigationPopupMode::GoToLine;
        FocusEditor();
        return;
    }

    const size_t active_pane = document_workspace_.ActiveEditorPaneIndex();
    DocumentSession* session = nullptr;
    EditorViewport* viewport = nullptr;
    if (document_workspace_.HasEditorPaneAt(active_pane)) {
        const size_t session_index = document_workspace_.PaneSessionIndex(active_pane);
        session = document_workspace_.SessionAt(session_index);
        viewport = document_workspace_.PaneViewport(active_pane);
    }
    if (!session) {
        session = document_workspace_.ActiveSession();
    }

    const size_t total_lines = std::max<size_t>(session ? session->LineCount() : 1, 1);

    bool has_digits = false;
    const size_t line_number = ParsePositiveNumberClamped(
        goto_line_input_,
        total_lines,
        &has_digits);
    if (!has_digits) {
        active_action_ = "Invalid line number";
        if (goto_line_input_component_) {
            goto_line_input_component_->TakeFocus();
        }
        screen_.PostEvent(ftxui::Event::Custom);
        return;
    }

    goto_line_input_ = std::to_string(line_number);
    goto_line_input_cursor_position_ = static_cast<int>(goto_line_input_.size());
    if (session) {
        const size_t line_index = line_number - 1;
        if (viewport) {
            session->SetActiveCursorState(&viewport->CursorState());
        }
        session->SetCursorPosition(line_index, 0);
        if (viewport) {
            viewport->CursorState().cursor_row = session->CursorRow();
            viewport->CursorState().cursor_col = session->CursorCol();
            viewport->CursorState().selection.active = false;
            viewport->scroll_y = session->CursorRow();
            viewport->scroll_x = 0;
        }
    }
    active_action_ = "Jumped to line " + std::to_string(line_number);
    screen_.PostEvent(ftxui::Event::Custom);

    show_goto_line_bar_ = false;
    navigation_popup_mode_ = NavigationPopupMode::GoToLine;
    FocusEditor();
}

} // namespace textlt
