#include "modals/modal_text_processors.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include "app_resources.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/screen/screen.hpp"
#include "json_utils.hpp"

namespace {

void Require(bool condition, const char* message) {
    std::cerr << "Check: " << message << '\n';
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void Click(textlt::TextProcessorsModal& modal, const std::string& label) {
    ftxui::Screen frame(104, 34);
    ftxui::Render(frame, modal.View()->Render());
    for (int y = frame.dimy() - 1; y >= 0; --y) {
        for (int x = 0; x + static_cast<int>(label.size()) <= frame.dimx(); ++x) {
            size_t i = 0;
            while (i < label.size() && frame.PixelAt(x + static_cast<int>(i), y).character == label.substr(i, 1)) ++i;
            if (i != label.size()) continue;
            ftxui::Mouse mouse;
            mouse.x = x;
            mouse.y = y;
            mouse.button = ftxui::Mouse::Left;
            modal.View()->OnEvent(ftxui::Event::Mouse("", mouse));
            return;
        }
    }
    throw std::runtime_error("Button missing: " + label);
}

} // namespace

int main() try {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() /
        ("textlt-quick-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto defaults = root / "defaults";
    fs::create_directories(defaults / "text_parsers");
#ifdef _WIN32
    Require(_wputenv_s(L"APPDATA", root.c_str()) == 0, "Set test APPDATA");
    Require(_wputenv_s(L"TEXTLT_DATA_DIR", defaults.c_str()) == 0, "Set test resource path");
#else
    Require(setenv("XDG_CONFIG_HOME", root.c_str(), 1) == 0, "Set test XDG_CONFIG_HOME");
    Require(setenv("TEXTLT_DATA_DIR", defaults.c_str(), 1) == 0, "Set test resource path");
#endif
    std::ofstream(defaults / "default_text_parsers.json") << R"({
        "parsers": [{"id":"append","name":"Append","scope":"text","script":"text_parsers/append.lua",
        "pinned":true,"repeat_default":2,"params":[{"id":"suffix","label":"Suffix","type":"string","default":"!"}]}]
    })";
    std::ofstream(defaults / "text_parsers" / "append.lua")
        << "function transform(text, params) return text .. params.suffix end";

    const auto resources = textlt::FindTextProcessorsDirectory();
    std::cerr << "Processor resources: " << resources << '\n';
    Require(resources == defaults, "Expected the test processor resources");

    auto expected_target = textlt::ProcessorTarget::Selection;
    std::string result;
    textlt::TextProcessorsModal modal(nullptr,
        [&](textlt::ProcessorTarget target, std::string& text, std::string&) {
            Require(target == expected_target, "Read scope matches the target");
            text = target == textlt::ProcessorTarget::Paragraph ? "paragraph" : "selection";
            return true;
        },
        [&](textlt::ProcessorTarget target, const std::string& text, std::string&) {
            Require(target == expected_target, "Replacement scope matches the target");
            result = text;
            return true;
        });
    const auto path = textlt::TextParserManager::UserConfigDirectory() / "quick_text_commands.json";
    modal.OpenQuick(false);
    ftxui::Screen panel(80, 19);
    ftxui::Render(panel, modal.View()->Render());
    const auto row_text = [&](int y) {
        std::string row;
        for (int x = 1; x < panel.dimx() - 1; ++x) row += panel.PixelAt(x, y).character;
        return row;
    };
    for (int y = 13; y <= 17; ++y) {
        std::cerr << "Footer row " << y << " [" << row_text(y) << "]\n";
    }
    Require(row_text(13).find_first_not_of(' ') == std::string::npos, "Footer padding is blank");
    Require(row_text(14).find("────") != std::string::npos &&
            row_text(15).find("Press 1") != std::string::npos &&
            row_text(16).find("────") != std::string::npos, "Status must appear between dividers");
    Require(row_text(17).find("Remove") != std::string::npos &&
            row_text(17).find("Close") != std::string::npos, "Actions must occupy the bottom row");
    const auto initial = textlt::LoadJsonValue(path);
    Require(initial.is_array() && initial.size() == 10, "Expected ten starter commands");
    for (const auto& slot : initial) Require(slot.is_object(), "Starter slot contains a command");
    modal.View()->OnEvent(ftxui::Event::Character('6'));
    Require(!modal.IsOpen() && result.find("selection") != std::string::npos &&
        result != "selection", "Starter bullet list transforms the selection");

    // Removing the starter set must leave a saved empty panel available for customization.
    modal.OpenQuick(false);
    for (char digit : std::string("1234567890")) {
        Click(modal, "Remove");
        modal.View()->OnEvent(ftxui::Event::Character(digit));
        modal.View()->OnEvent(ftxui::Event::Return);
    }
    modal.Close();
    modal.Open();
    Click(modal, "Add to Quick Panel");
    modal.Close();
    // Saving the same processor again must keep each slot's own settings.
    const auto user_config = textlt::TextParserManager::UserConfigFilePath();
    auto config = textlt::LoadJsonValue(user_config);
    config["parsers"][0]["params"][0]["default"] = "?";
    config["parsers"][0]["repeat_default"] = 3;
    Require(textlt::WriteJsonAtomically(user_config, config), "Save updated test processor");
    modal.Open();
    for (int i = 0; i < 10; ++i) Click(modal, "Add to Quick Panel");
    auto slots = textlt::LoadJsonValue(path);
    Require(slots.size() == 10 && slots.at(9).at("id") == "append", "Ten saved slots expected");
    Require(slots.at(0).at("params").at("suffix") == "!", "First slot retains its suffix");
    Require(slots.at(0).at("repeat") == 2, "First slot retains its repeat count");
    modal.Close();

    modal.OpenQuick(false);
    modal.View()->OnEvent(ftxui::Event::Character('1'));
    Require(!modal.IsOpen() && result == "selection!!", "Selection command appends the saved suffix twice");

    expected_target = textlt::ProcessorTarget::Paragraph;
    modal.OpenQuick(true);
    Click(modal, "Remove");
    modal.View()->OnEvent(ftxui::Event::Character('2'));
    modal.View()->OnEvent(ftxui::Event::Return);
    slots = textlt::LoadJsonValue(path);
    Require(!slots.at(0).is_null() && slots.at(1).is_null() && !slots.at(2).is_null(), "Removal preserves neighboring slot numbers");
    modal.View()->OnEvent(ftxui::Event::Character('0'));
    Require(!modal.IsOpen() && result == "paragraph???", "Slot ten applies its saved settings to the paragraph");
    fs::remove_all(root);
} catch (const std::exception& error) {
    std::cerr << "Quick panel test failed: " << error.what() << '\n';
    return 1;
}
