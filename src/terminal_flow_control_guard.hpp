#pragma once

#include <termios.h>

namespace textlt {

class TerminalFlowControlGuard {
public:
    TerminalFlowControlGuard();
    ~TerminalFlowControlGuard();

    TerminalFlowControlGuard(const TerminalFlowControlGuard&) = delete;
    TerminalFlowControlGuard& operator=(const TerminalFlowControlGuard&) = delete;

private:
    termios original_settings_ {};
    bool configured_ = false;
};

} // namespace textlt
