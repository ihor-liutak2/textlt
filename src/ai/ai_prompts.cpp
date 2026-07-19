#include "ai/ai_prompts.hpp"

namespace textlt {

std::string BuildAiSystemPrompt(const AiPromptRequest& request) {
    if (request.action == AiActionType::Translate) {
        const std::string language = request.target_language.empty()
            ? std::string("Ukrainian")
            : request.target_language;
        return "Translate the supplied text into " + language +
            ". Preserve paragraph breaks, lists, punctuation, names, numbers, and meaning. "
            "Do not add explanations, commentary, headings, quotation marks, or markdown fences. "
            "Return only the translated text.";
    }

    if (request.edit_style == AiEditStyle::Business) {
        return "Edit the supplied text into a clear, formal, precise, professional business style. "
            "Preserve the original language, meaning, facts, names, numbers, paragraph breaks, and lists. "
            "Do not invent information or add explanations. Return only the edited text.";
    }

    return "Edit the supplied text into a natural, clear, conversational style that is easy to read. "
        "Preserve the original language, meaning, facts, names, numbers, paragraph breaks, and lists. "
        "Do not invent information or add explanations. Return only the edited text.";
}

std::string BuildAiUserPrompt(const AiPromptRequest& request) {
    return request.text;
}

} // namespace textlt
