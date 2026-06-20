#include "app_resources.hpp"
#include "app.hpp"
#include "terminal_flow_control_guard.hpp"

#include "ftxui/screen/terminal.hpp"

#include <string>
#include <vector>

int main(int argc, char* argv[]) {
    std::vector<std::string> files_to_open;
    for (int i = 1; i < argc; ++i) {
        files_to_open.emplace_back(argv[i]);
    }

    // Force 24-bit color before themes are loaded, because FTXUI resolves
    // Color::RGB values at construction time and otherwise down-samples them.
    ftxui::Terminal::SetColorSupport(ftxui::Terminal::Color::TrueColor);

    textlt::EnsureStartupResources();

    textlt::TerminalFlowControlGuard terminal_flow_control_guard;
    textlt::TextltApp app(files_to_open);
    app.Run();
    return 0;
}
