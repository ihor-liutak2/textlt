#pragma once

#include <string>

namespace textlt {

enum class AiActionType {
    Translate,
    Edit,
};

enum class AiEditStyle {
    Conversational,
    Business,
};

struct AiPromptRequest {
    AiActionType action = AiActionType::Translate;
    std::string text;
    std::string source_language;
    std::string target_language;
    AiEditStyle edit_style = AiEditStyle::Conversational;
};

std::string BuildAiSystemPrompt(const AiPromptRequest& request);
std::string BuildAiUserPrompt(const AiPromptRequest& request);

} // namespace textlt
