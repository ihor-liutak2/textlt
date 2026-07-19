#pragma once

#include <string>

#include "ai/ai_prompts.hpp"

namespace textlt {

struct AiQuickStatusSnapshot {
    std::string model_label = "Not selected";
    std::string status_label = "Not ready";
    bool ready = false;
    bool checking = false;
    bool busy = false;
    bool stopping = false;
    AiActionType active_action = AiActionType::Translate;
};

} // namespace textlt
