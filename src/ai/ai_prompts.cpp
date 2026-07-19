#include "ai/ai_prompts.hpp"

namespace textlt {

std::string BuildAiSystemPrompt(const AiPromptRequest& request) {
    if (request.action == AiActionType::Translate) {
        const std::string source_language = request.source_language.empty()
            ? std::string("English")
            : request.source_language;
        const std::string target_language = request.target_language.empty()
            ? std::string("Ukrainian")
            : request.target_language;
        return "Translate the supplied text explicitly from " + source_language +
            " into " + target_language +
            ". Do not translate it into any other language. Preserve paragraph breaks, lists, "
            "punctuation, names, numbers, and meaning. "
            "Do not add explanations, commentary, headings, quotation marks, or markdown fences. "
            "Do not output special end-of-sequence or tokenizer control markers. "
            "Return only the translated text.";
    }

    if (request.edit_style == AiEditStyle::Business) {
        return "Edit the supplied text into a clear, formal, precise, professional business style. "
            "Preserve the original language, meaning, facts, names, numbers, paragraph breaks, and lists. "
            "Do not invent information or add explanations. Do not output special end-of-sequence "
            "or tokenizer control markers. Return only the edited text.";
    }

    return "Edit the supplied text into a natural, clear, conversational style that is easy to read. "
        "Preserve the original language, meaning, facts, names, numbers, paragraph breaks, and lists. "
        "Do not invent information or add explanations. Do not output special end-of-sequence "
        "or tokenizer control markers. Return only the edited text.";
}

std::string BuildAiUserPrompt(const AiPromptRequest& request) {
    return request.text;
}

} // namespace textlt
