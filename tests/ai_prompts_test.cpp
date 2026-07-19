#include <cassert>
#include <string>

#include "ai/ai_prompts.hpp"

int main() {
    textlt::AiPromptRequest translate;
    translate.action = textlt::AiActionType::Translate;
    translate.target_language = "Polish";
    translate.text = "Hello";
    const std::string translation_prompt = textlt::BuildAiSystemPrompt(translate);
    assert(translation_prompt.find("Polish") != std::string::npos);
    assert(translation_prompt.find("Return only") != std::string::npos);
    assert(textlt::BuildAiUserPrompt(translate) == "Hello");

    textlt::AiPromptRequest edit;
    edit.action = textlt::AiActionType::Edit;
    edit.edit_style = textlt::AiEditStyle::Business;
    const std::string business_prompt = textlt::BuildAiSystemPrompt(edit);
    assert(business_prompt.find("professional business style") != std::string::npos);
    assert(business_prompt.find("Do not invent") != std::string::npos);
    return 0;
}
