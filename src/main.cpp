#include "app.hpp"
#include "terminal_flow_control_guard.hpp"

int main() {
    textlt::TerminalFlowControlGuard terminal_flow_control_guard;
    textlt::TextltApp app;
    app.Run();
    return 0;
}
