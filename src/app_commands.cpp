#include "app.hpp"

#include <functional>
#include <string>
#include <utility>

#include "ftxui/component/event.hpp"
#include "shortcut_key.hpp"

namespace textlt {
namespace {

AppCommand MakeCommand(
    const std::string& id,
    const std::string& title,
    const std::string& category,
    const std::string& shortcut,
    std::function<void()> run) {
    AppCommand command;
    command.id = id;
    command.title = title;
    command.category = category;
    command.shortcut = shortcut;
    command.run = std::move(run);
    return command;
}

} // namespace

void TextltApp::InitializeCommands() {
    auto add = [this](
        const std::string& id,
        const std::string& title,
        const std::string& category,
        const std::string& shortcut,
        std::function<void()> run) {
        command_registry_.Register(MakeCommand(id, title, category, shortcut, std::move(run)));
    };

    add("file.new", "New", "File", "", [this] { CommandFileNew(); });
    add("file.files", "Files", "File", "", [this] { CommandFileManageFiles(); });
    add("file.open", "Open", "File", "Ctrl+O", [this] { CommandFileOpen(); });
    add("file.save_as", "Save As", "File", "", [this] { CommandFileSaveAs(); });
    add("file.import", "Import", "File", "", [this] { CommandFileImport(); });
    add("file.recent", "Recent Files", "File", "", [this] { CommandFileRecent(); });
    add("file.close", "Close", "File", "Alt+W / Ctrl+W", [this] { CommandFileClose(); });
    add("file.close_all", "Close All", "File", "", [this] { CommandFileCloseAll(); });
    add("file.save", "Save", "File", "Ctrl+S", [this] { CommandFileSave(); });
    add("file.save_all", "Save All", "File", "", [this] { CommandFileSaveAll(); });
    add("file.toggle_favorite", "Toggle Favorite", "File", "", [this] { CommandFileToggleFavorite(); });

    add("edit.undo", "Undo", "Edit", "Ctrl+Z", [this] { CommandEditUndo(); });
    add("edit.redo", "Redo", "Edit", "Ctrl+Y", [this] { CommandEditRedo(); });
    add("edit.select_all", "Select All", "Edit", "Ctrl+A", [this] { CommandEditSelectAll(); });
    add("edit.cut", "Cut", "Edit", "Ctrl+X", [this] { CommandEditCut(); });
    add("edit.copy", "Copy", "Edit", "Ctrl+C", [this] { CommandEditCopy(); });
    add("edit.paste", "Paste", "Edit", "Ctrl+V", [this] { CommandEditPaste(); });
    add("edit.toggle_comment", "Toggle Comment", "Edit", "Ctrl+/", [this] { CommandEditToggleComment(); });
    add("edit.toggle_case", "Toggle Case", "Edit", "Ctrl+T", [this] { CommandEditToggleCase(); });
    add("edit.convert_indents_4_to_2", "Convert Indents 4 to 2", "Edit", "", [this] { CommandEditConvertIndents4To2(); });
    add("edit.convert_indents_2_to_4", "Convert Indents 2 to 4", "Edit", "", [this] { CommandEditConvertIndents2To4(); });
    add("edit.find", "Find", "Edit", "Ctrl+F", [this] { CommandEditFind(); });
    add("edit.replace", "Replace", "Edit", "Ctrl+R", [this] { CommandEditReplace(); });
    add("search.files", "Search in Files", "Edit", "", [this] { CommandSearchFiles(); });
    add("text_processors.open", "Text Processors", "Edit", "", [this] { CommandTextProcessors(); });
    add("custom_processor_builder.open", "Custom Processor Builder", "Text Processors", "", [this] { CommandCustomProcessorBuilder(); });
    add("editor.go_to_line", "Go to Line", "Edit", "Ctrl+G", [this] { OpenGoToLinePanel(); });

    add("view.toggle_line_numbers", "Toggle Line Numbers", "Options", "", [this] { CommandViewToggleLineNumbers(); });
    add("sidebar.toggle_file_explorer", "Toggle File Explorer", "Options", "", [this] { CommandSidebarToggleFileExplorer(); });
    add("sidebar.ctrl_b_file_explorer", "File Explorer Focus Toggle", "Options", "Ctrl+B", [this] { HandleCtrlBFileExplorer(); });
    add("sidebar.show_project", "Project Sidebar", "Options", "Alt+P", [this] { ShowProjectSidebar(); });
    add("sidebar.show_favorites", "Favorites Sidebar", "Options", "Alt+F", [this] { ShowFavoritesSidebar(); });
    add("sidebar.show_opened_files", "Opened Files Sidebar", "Options", "Alt+O", [this] { ShowOpenedFilesSidebar(); });
    add("sidebar.toggle_opened_project", "Toggle File Explorer Tab", "Options", "Alt+B", [this] { ToggleSidebarOpenedProject(); });
    add("editor.toggle_smart_word_wrap", "Toggle Smart Word Wrap", "Options", "", [this] { CommandEditorToggleSmartWordWrap(); });
    add("editor.toggle_syntax_highlighting", "Toggle Syntax Highlighting", "Options", "", [this] { CommandEditorToggleSyntaxHighlighting(); });
    add("editor.toggle_auto_pairing", "Toggle Auto Pairing", "Options", "", [this] { CommandEditorToggleAutoPairing(); });
    add("editor.toggle_auto_indent", "Toggle Auto Indent", "Options", "", [this] { CommandEditorToggleAutoIndent(); });
    add("editor.toggle_tab_size", "Toggle Tab Size", "Options", "", [this] { CommandEditorToggleTabSize(); });
    add("editor.convert_tabs_to_spaces", "Convert Tabs to Spaces", "Options", "", [this] { CommandEditorConvertTabsToSpaces(); });
    add("theme.open", "Theme", "Options", "", [this] { CommandThemeOpen(); });
    add("view.layout", "View Layout", "Options", "", [this] { CommandViewLayoutOpen(); });
    add("distraction.open_options", "Distraction Mode", "Options", "", [this] { CommandDistractionOpenOptions(); });
    add("distraction.enter", "Enter Distraction Mode", "Options", "", [this] { CommandDistractionEnter(); });
    add("distraction.exit", "Exit Distraction Mode", "Options", "", [this] { CommandDistractionExit(); });
    add("distraction.toggle", "Toggle Distraction Mode", "Options", "", [this] { CommandDistractionToggle(); });
    add("distraction.next_page", "Distraction Next Page", "Options", "", [this] { CommandDistractionNextPage(); });
    add("distraction.previous_page", "Distraction Previous Page", "Options", "", [this] { CommandDistractionPreviousPage(); });
    add("distraction.go_to_page", "Distraction Go To Page", "Options", "", [this] { CommandDistractionGoToPage(); });

    add("tts.open_modal", "TTS", "TTS", "Alt+H", [this] { OpenTtsModal(); });
    add("tts.play", "Play TTS", "TTS", "", [this] { CommandTtsPlay(); });
    add("tts.pause", "Pause TTS", "TTS", "", [this] { CommandTtsPause(); });
    add("tts.stop", "Stop TTS", "TTS", "", [this] { CommandTtsStop(); });
    add("tts.next", "Next TTS Chunk", "TTS", "", [this] { CommandTtsNext(); });
    add("ai.open_actions", "AI Actions", "AI", "Alt+J", [this] { OpenAiActionsModal(); });
    add("ai.open_settings", "AI Settings", "AI", "", [this] { OpenAiSettingsModal(); });
    add("assistant.open_settings", "TTS Settings", "TTS", "Alt+S", [this] { OpenAssistantSettingsModal(); });

    add("remote.files", "Remote Files", "Remote", "", [this] { OpenRemoteFilesModal(); });
    add("remote.connections", "Remote Connections", "Remote", "", [this] { OpenRemoteConnectionsModal(); });

    add("git.open", "Git", "Git", "", [this] { OpenGitModal(); });
    add("git.settings", "Git Settings", "Git", "", [this] { OpenGitSettingsModal(); });

    add("app.about", "About textlt", "Help", "", [this] { OpenAboutDialog(); });
    add("app.help", "Keyboard Shortcuts", "Help", "F1", [this] { OpenKeyboardShortcutsModal(); });
    add("app.exit", "Exit", "Application", "Ctrl+Q", [this] { RequestExit(); });
}

void TextltApp::InitializeMenuShortcuts() {
    for (const AppCommand& command : command_registry_.Commands()) {
        std::string first_shortcut;
        if (!command.shortcut.empty()) {
            first_shortcut = command.shortcut;
            const size_t slash_pos = first_shortcut.find(" / ");
            if (slash_pos != std::string::npos) {
                first_shortcut = first_shortcut.substr(0, slash_pos);
            }
            if (!ParseShortcutKey(first_shortcut)) {
                first_shortcut.clear();
            }
        }
        shortcut_registry_.RegisterDefault({
            ShortcutContext::Menu,
            command.id,
            command.title,
            command.category,
            first_shortcut,
        });
    }
}

void TextltApp::InitializeTextShortcuts() {
    for (const ShortcutBindingDefinition& definition : EditorKeymap::DefaultBindings()) {
        shortcut_registry_.RegisterDefault(definition);
    }
}

bool TextltApp::SaveShortcutOverrides(std::string& error) {
    return shortcut_store_.Save(shortcut_registry_.Overrides(), &error);
}

bool TextltApp::RunMenuShortcut(ftxui::Event event) {
    const std::string command_id = shortcut_registry_.MatchAction(ShortcutContext::Menu, event);
    if (command_id.empty()) {
        return false;
    }
    return RunCommand(command_id);
}

bool TextltApp::RunTextShortcut(ftxui::Event event) {
    const std::string action_id = shortcut_registry_.MatchAction(ShortcutContext::Text, event);
    if (action_id.empty()) {
        return false;
    }
    return editor_keymap_.RunAction(*this, action_id);
}

bool TextltApp::RunCommand(const std::string& command_id) {
    if (command_registry_.Run(command_id)) {
        return true;
    }

    active_action_ = "Unknown command: " + command_id;
    screen_.PostEvent(ftxui::Event::Custom);
    return false;
}

} // namespace textlt
