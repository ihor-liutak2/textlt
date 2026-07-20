#pragma once

#include <string>

#include "ai/ai_prompts.hpp"

namespace textlt {

struct AiQuickStatusSnapshot {
    std::string model_label = "Not selected";
    std::string language_label = "Not selected";
    std::string status_label = "Not ready";
    std::string action_status_label;
    std::string paragraph_label = "Unavailable";
    bool paragraph_available = false;
    bool ready = false;
    bool checking = false;
    bool busy = false;
    bool stopping = false;
    AiActionType active_action = AiActionType::Translate;
};

} // namespace textlt
