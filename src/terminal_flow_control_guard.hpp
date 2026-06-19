#pragma once

#ifndef _WIN32
#include <termios.h>
#endif

namespace textlt {

class TerminalFlowControlGuard {
public:
    TerminalFlowControlGuard();
    ~TerminalFlowControlGuard();

    TerminalFlowControlGuard(const TerminalFlowControlGuard&) = delete;
    TerminalFlowControlGuard& operator=(const TerminalFlowControlGuard&) = delete;

private:
#ifndef _WIN32
    termios original_settings_ {};
    bool configured_ = false;
    bool keyboard_reporting_enabled_ = false;
#endif
};

} // namespace textlt
