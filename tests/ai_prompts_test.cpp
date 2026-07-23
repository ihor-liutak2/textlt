#include <cassert>
#include <string>

#include "ai/ai_prompts.hpp"

int main() {
    textlt::AiPromptRequest translate;
    translate.action = textlt::AiActionType::Translate;
    translate.source_language = "English";
    translate.target_language = "Polish";
    translate.text = "Hello";
    const std::string translation_prompt = textlt::BuildAiSystemPrompt(translate);
    assert(translation_prompt.find("from English into Polish") != std::string::npos);
    assert(translation_prompt.find("<TRANSLATION>") != std::string::npos);
    assert(translation_prompt.find("tokenizer control markers") != std::string::npos);
    assert(textlt::BuildAiUserPrompt(translate) ==
        "<SOURCE_TEXT>\nHello\n</SOURCE_TEXT>");
    assert(translation_prompt.find("Never copy SOURCE_TEXT unchanged") != std::string::npos);

    translate.corrective_retry = true;
    translate.corrective_reason = "the output was still English";
    assert(textlt::BuildAiSystemPrompt(translate).find(
        "previous answer was rejected") != std::string::npos);
    assert(textlt::BuildAiSystemPrompt(translate).find(
        "output was still English") != std::string::npos);

    textlt::AiPromptRequest russian_to_ukrainian;
    russian_to_ukrainian.action = textlt::AiActionType::Translate;
    russian_to_ukrainian.source_language = "Russian";
    russian_to_ukrainian.target_language = "Ukrainian";
    russian_to_ukrainian.text = "Надеюсь, вы простите меня.";
    std::string normalized;
    std::string error;
    assert(textlt::NormalizeAiTranslationResponse(
        russian_to_ukrainian,
        "<TRANSLATION>Сподіваюся, ви пробачите мене.</TRANSLATION>",
        normalized,
        error));
    assert(normalized == "Сподіваюся, ви пробачите мене.");
    assert(!textlt::NormalizeAiTranslationResponse(
        russian_to_ukrainian,
        "**Перевод:**\n\nНадеюсь, вы простите меня.\n\n**Объяснение:**\nТекст.",
        normalized,
        error));
    assert(error.find("source language") != std::string::npos);

    textlt::AiPromptRequest edit;
    edit.action = textlt::AiActionType::Edit;
    edit.edit_style = textlt::AiEditStyle::Business;
    const std::string business_prompt = textlt::BuildAiSystemPrompt(edit);
    assert(business_prompt.find("professional business style") != std::string::npos);
    assert(business_prompt.find("Do not invent") != std::string::npos);
    assert(business_prompt.find("tokenizer control markers") != std::string::npos);
    return 0;
}
