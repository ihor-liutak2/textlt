#include "terminal_flow_control_guard.hpp"

#include <string_view>
#include <unistd.h>

namespace textlt {
namespace {

void WriteTerminalSequence(std::string_view sequence) {
    if (sequence.empty()) {
        return;
    }
    (void)write(STDOUT_FILENO, sequence.data(), sequence.size());
}

} // namespace

TerminalFlowControlGuard::TerminalFlowControlGuard() {
    if (tcgetattr(STDIN_FILENO, &original_settings_) == 0) {
        configured_ = true;
        termios updated_settings = original_settings_;
        updated_settings.c_iflag &= ~IXON;
        updated_settings.c_lflag &= ~ISIG;
        tcsetattr(STDIN_FILENO, TCSANOW, &updated_settings);
    }

    // Ask compatible terminals to report modified keys distinctly. Without
    // this, Ctrl+Backspace is often indistinguishable from plain Backspace.
    WriteTerminalSequence("\x1B[>4;2m");
    keyboard_reporting_enabled_ = true;
}

TerminalFlowControlGuard::~TerminalFlowControlGuard() {
    if (keyboard_reporting_enabled_) {
        WriteTerminalSequence("\x1B[>4;0m");
    }
    if (configured_) {
        tcsetattr(STDIN_FILENO, TCSANOW, &original_settings_);
    }
}

} // namespace textlt
