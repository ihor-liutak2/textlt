#include "app.hpp"
#include "terminal_flow_control_guard.hpp"

#include <string>
#include <vector>

int main(int argc, char* argv[]) {
    std::vector<std::string> files_to_open;
    for (int i = 1; i < argc; ++i) {
        files_to_open.emplace_back(argv[i]);
    }

    textlt::TerminalFlowControlGuard terminal_flow_control_guard;
    textlt::TextltApp app(files_to_open);
    app.Run();
    return 0;
}
