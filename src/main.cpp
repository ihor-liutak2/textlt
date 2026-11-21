#include "app_resources.hpp"
#include "app.hpp"
#include "path_utils.hpp"
#include "terminal_flow_control_guard.hpp"
#include "startup_check.hpp"

#include "ftxui/screen/terminal.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
int wmain(int argc, wchar_t* argv[]) {
#else
int main(int argc, char* argv[]) {
#endif
#ifdef _WIN32
    // TextLT renders UTF-8. Do not inherit PowerShell 5.1's legacy console
    // code page, which cannot represent all interface characters.
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
#endif

    bool startup_check = false;
    if (argc == 2) {
        const std::string option = textlt::Utf8FromPath(argv[1]);
        if (option == "--version" || option == "-v") {
            std::cout << "textlt " TEXTLT_VERSION "\n";
            return 0;
        }
        if (option == "--help" || option == "-h") {
            std::cout
                << "textlt " TEXTLT_VERSION " - Native Non-Modal Text Editor\n"
                << "Usage: textlt [file ...]\n"
                << "       textlt --version\n"
                << "       textlt --startup-check  (check runtime dependencies and user storage)\n";
            return 0;
        }
        startup_check = option == "--startup-check";
    }

    std::vector<std::string> files_to_open;
    if (!startup_check) {
        for (int i = 1; i < argc; ++i) {
            files_to_open.push_back(textlt::Utf8FromPath(argv[i]));
        }
    }

    // Force 24-bit color before themes are loaded, because FTXUI resolves
    // Color::RGB values at construction time and otherwise down-samples them.
    ftxui::Terminal::SetColorSupport(ftxui::Terminal::Color::TrueColor);

    try {
        if (startup_check && !textlt::CheckStartupEnvironment()) {
            std::cerr << "TextLT startup check failed. See [FAIL] entries above.\n";
            return 1;
        }
        if (startup_check) std::cout << "[INFO] Initializing editor and workspace" << std::endl;
        textlt::EnsureStartupResources();

        // TextltApp owns the complete UI and all modal/workspace state. Keep it
        // off the main thread's comparatively small Windows stack.
        auto app = std::make_unique<textlt::TextltApp>(files_to_open);
        if (startup_check) {
            std::cout << "TextLT startup check passed.\n";
            return 0;
        }
        textlt::TerminalFlowControlGuard terminal_flow_control_guard;
        app->Run();
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "TextLT could not start: " << exception.what() << '\n';
    } catch (...) {
        std::cerr << "TextLT could not start: unknown startup error.\n";
    }
    return 1;
}
