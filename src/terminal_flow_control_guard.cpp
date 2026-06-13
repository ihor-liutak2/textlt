#include "terminal_flow_control_guard.hpp"

#include <unistd.h>

namespace textlt {

TerminalFlowControlGuard::TerminalFlowControlGuard() {
    if (tcgetattr(STDIN_FILENO, &original_settings_) == 0) {
        configured_ = true;
        termios updated_settings = original_settings_;
        updated_settings.c_iflag &= ~IXON;
        updated_settings.c_lflag &= ~ISIG;
        tcsetattr(STDIN_FILENO, TCSANOW, &updated_settings);
    }
}

TerminalFlowControlGuard::~TerminalFlowControlGuard() {
    if (configured_) {
        tcsetattr(STDIN_FILENO, TCSANOW, &original_settings_);
    }
}

} // namespace textlt
