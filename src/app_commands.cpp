#include "app.hpp"

#include <functional>
#include <string>
#include <utility>

#include "ftxui/component/event.hpp"

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

    add("file.new", "New", "File", "", [this] { HandleFileMenu(0); });
    add("file.files", "Files", "File", "", [this] { HandleFileMenu(1); });
    add("file.open", "Open", "File", "Ctrl+O", [this] { HandleFileMenu(2); });
    add("file.save_as", "Save As", "File", "", [this] { HandleFileMenu(3); });
    add("file.import", "Import", "File", "", [this] { HandleFileMenu(4); });
    add("file.recent", "Recent Files", "File", "", [this] { HandleFileMenu(5); });
    add("file.close", "Close", "File", "Alt+W / Ctrl+W", [this] { HandleFileMenu(6); });
    add("file.close_all", "Close All", "File", "", [this] { HandleFileMenu(7); });
    add("file.save", "Save", "File", "Ctrl+S", [this] { HandleFileMenu(8); });
    add("file.save_all", "Save All", "File", "", [this] { HandleFileMenu(9); });
    add("file.toggle_favorite", "Toggle Favorite", "File", "", [this] { HandleFileMenu(10); });

    add("edit.undo", "Undo", "Edit", "Ctrl+Z", [this] { HandleEditMenu(0); });
    add("edit.redo", "Redo", "Edit", "Ctrl+Y", [this] { HandleEditMenu(1); });
    add("edit.select_all", "Select All", "Edit", "Ctrl+A", [this] { HandleEditMenu(2); });
    add("edit.cut", "Cut", "Edit", "Ctrl+X", [this] { HandleEditMenu(3); });
    add("edit.copy", "Copy", "Edit", "Ctrl+C", [this] { HandleEditMenu(4); });
    add("edit.paste", "Paste", "Edit", "Ctrl+V", [this] { HandleEditMenu(5); });
    add("edit.toggle_comment", "Toggle Comment", "Edit", "Ctrl+/", [this] { HandleEditMenu(6); });
    add("edit.toggle_case", "Toggle Case", "Edit", "Ctrl+T", [this] { HandleEditMenu(7); });
    add("edit.convert_indents_4_to_2", "Convert Indents 4 to 2", "Edit", "", [this] { HandleEditMenu(8); });
    add("edit.convert_indents_2_to_4", "Convert Indents 2 to 4", "Edit", "", [this] { HandleEditMenu(9); });
    add("edit.find", "Find", "Edit", "Ctrl+F", [this] { HandleEditMenu(10); });
    add("edit.replace", "Replace", "Edit", "Ctrl+R", [this] { HandleEditMenu(11); });
    add("search.files", "Search in Files", "Edit", "", [this] { HandleEditMenu(12); });
    add("text_processors.open", "Text Processors", "Edit", "", [this] { HandleEditMenu(13); });

    add("view.toggle_line_numbers", "Toggle Line Numbers", "Options", "", [this] { HandleOptionsMenu(0); });
    add("sidebar.toggle_file_explorer", "Toggle File Explorer", "Options", "Ctrl+B", [this] { HandleOptionsMenu(1); });
    add("editor.toggle_smart_word_wrap", "Toggle Smart Word Wrap", "Options", "", [this] { HandleOptionsMenu(2); });
    add("editor.toggle_syntax_highlighting", "Toggle Syntax Highlighting", "Options", "", [this] { HandleOptionsMenu(3); });
    add("editor.toggle_auto_pairing", "Toggle Auto Pairing", "Options", "", [this] { HandleOptionsMenu(4); });
    add("editor.toggle_auto_indent", "Toggle Auto Indent", "Options", "", [this] { HandleOptionsMenu(5); });
    add("editor.toggle_tab_size", "Toggle Tab Size", "Options", "", [this] { HandleOptionsMenu(6); });
    add("editor.convert_tabs_to_spaces", "Convert Tabs to Spaces", "Options", "", [this] { HandleOptionsMenu(7); });
    add("theme.open", "Theme", "Options", "", [this] { HandleOptionsMenu(8); });
    add("view.layout", "View Layout", "Options", "", [this] { HandleOptionsMenu(9); });

    add("tts.open_modal", "TTS", "AI", "Alt+H", [this] { OpenTtsModal(); });
    add("tts.play", "Play TTS", "TTS", "", [this] {
        tts_modal_.Play();
        screen_.PostEvent(ftxui::Event::Custom);
    });
    add("tts.pause", "Pause TTS", "TTS", "", [this] {
        tts_modal_.Pause();
        screen_.PostEvent(ftxui::Event::Custom);
    });
    add("tts.stop", "Stop TTS", "TTS", "", [this] {
        tts_modal_.Stop();
        screen_.PostEvent(ftxui::Event::Custom);
    });
    add("tts.next", "Next TTS Chunk", "TTS", "", [this] {
        tts_modal_.Next();
        screen_.PostEvent(ftxui::Event::Custom);
    });
    add("ai.open_actions", "AI Actions", "AI", "Alt+J", [this] { OpenAiActionsModal(); });
    add("assistant.open_settings", "Assistant Settings", "AI", "Alt+S", [this] { OpenAssistantSettingsModal(); });

    add("remote.files", "Remote Files", "Remote", "", [this] { OpenRemoteFilesModal(); });
    add("remote.connections", "Remote Connections", "Remote", "", [this] { OpenRemoteConnectionsModal(); });

    add("git.open", "Git", "Git", "", [this] { OpenGitModal(); });
    add("git.settings", "Git Settings", "Git", "", [this] { OpenGitSettingsModal(); });

    add("app.about", "About textlt", "Help", "", [this] { OpenAboutDialog(); });
    add("app.help", "Keyboard Shortcuts", "Help", "F1", [this] { OpenHelpDialog(); });
    add("app.exit", "Exit", "Application", "Ctrl+Q", [this] { RequestExit(); });
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
