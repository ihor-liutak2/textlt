#include "ai/ai_prompts.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <vector>

namespace textlt {
namespace {

std::string Trim(std::string value) {
    const auto is_space = [](unsigned char character) {
        return std::isspace(character) != 0;
    };
    value.erase(
        value.begin(),
        std::find_if(value.begin(), value.end(), [&](char character) {
            return !is_space(static_cast<unsigned char>(character));
        }));
    value.erase(
        std::find_if(value.rbegin(), value.rend(), [&](char character) {
            return !is_space(static_cast<unsigned char>(character));
        }).base(),
        value.end());
    return value;
}

std::string LowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::string NormalizeHeading(std::string line) {
    line = Trim(std::move(line));
    while (!line.empty() &&
           (line.front() == '#' || line.front() == '*' || line.front() == '_')) {
        line.erase(line.begin());
        line = Trim(std::move(line));
    }
    while (!line.empty() &&
           (line.back() == ':' || line.back() == '*' || line.back() == '_')) {
        line.pop_back();
        line = Trim(std::move(line));
    }
    return LowerAscii(std::move(line));
}

bool IsTranslationHeading(const std::string& heading) {
    return heading == "translation" ||
        heading == "перевод" ||
        heading == "переклад";
}

bool IsExplanationHeading(const std::string& heading) {
    return heading == "explanation" ||
        heading == "объяснение" ||
        heading == "пояснення";
}

size_t CountAny(const std::string& value, const std::vector<std::string>& needles) {
    size_t count = 0;
    for (const std::string& needle : needles) {
        size_t position = 0;
        while ((position = value.find(needle, position)) != std::string::npos) {
            ++count;
            position += needle.size();
        }
    }
    return count;
}

bool HasWrongRussianUkrainianLanguage(
    const AiPromptRequest& request,
    const std::string& translation) {
    const std::string source = LowerAscii(request.source_language);
    const std::string target = LowerAscii(request.target_language);
    const std::vector<std::string> russian_only =
        {"ы", "Ы", "э", "Э", "ё", "Ё", "ъ", "Ъ"};
    const std::vector<std::string> ukrainian_only =
        {"і", "І", "ї", "Ї", "є", "Є", "ґ", "Ґ"};

    if (source == "russian" && target == "ukrainian") {
        return CountAny(translation, russian_only) > 0 &&
            CountAny(translation, ukrainian_only) == 0;
    }
    if (source == "ukrainian" && target == "russian") {
        return CountAny(translation, ukrainian_only) > 0 &&
            CountAny(translation, russian_only) == 0;
    }
    return false;
}

} // namespace

std::string BuildAiSystemPrompt(const AiPromptRequest& request) {
    if (request.action == AiActionType::Translate) {
        const std::string source_language = request.source_language.empty()
            ? std::string("English")
            : request.source_language;
        const std::string target_language = request.target_language.empty()
            ? std::string("Ukrainian")
            : request.target_language;
        std::string prompt =
            "You are a translation engine. Your only task is to translate the SOURCE_TEXT "
            "from " + source_language + " into " + target_language + ". "
            "The output must be written in " + target_language + ", not " + source_language + ". "
            "Never copy SOURCE_TEXT unchanged. Preserve meaning, paragraph breaks, lists, "
            "punctuation, names, and numbers. Do not answer the text, explain the translation, "
            "add headings, quotation marks, or markdown fences. Do not output special "
            "end-of-sequence or tokenizer control markers. Return exactly one block in this form: "
            "<TRANSLATION>translated text</TRANSLATION>. Put no text before or after that block.";
        if (request.corrective_retry) {
            prompt += " Your previous answer was rejected";
            if (!request.corrective_reason.empty()) {
                prompt += " because " + request.corrective_reason;
            }
            prompt += ". Correct that failure now and produce an actual translation into " +
                target_language + " without explanations.";
        }
        return prompt;
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
    if (request.action == AiActionType::Translate) {
        return "<SOURCE_TEXT>\n" + request.text + "\n</SOURCE_TEXT>";
    }
    return request.text;
}

bool NormalizeAiTranslationResponse(
    const AiPromptRequest& request,
    const std::string& response,
    std::string& translation,
    std::string& error) {
    translation.clear();
    error.clear();
    std::string candidate = Trim(response);
    if (candidate.empty()) {
        error = "the model returned an empty translation";
        return false;
    }

    constexpr const char* kOpenTag = "<TRANSLATION>";
    constexpr const char* kCloseTag = "</TRANSLATION>";
    const size_t open = candidate.find(kOpenTag);
    if (open != std::string::npos) {
        const size_t content_start = open + std::char_traits<char>::length(kOpenTag);
        const size_t close = candidate.find(kCloseTag, content_start);
        if (close == std::string::npos) {
            error = "the translation block was not closed";
            return false;
        }
        candidate = Trim(candidate.substr(content_start, close - content_start));
    } else {
        std::istringstream stream(candidate);
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(stream, line)) {
            lines.push_back(std::move(line));
        }

        size_t translation_heading = lines.size();
        size_t explanation_heading = lines.size();
        for (size_t index = 0; index < lines.size(); ++index) {
            const std::string heading = NormalizeHeading(lines[index]);
            if (translation_heading == lines.size() && IsTranslationHeading(heading)) {
                translation_heading = index;
            } else if (IsExplanationHeading(heading)) {
                explanation_heading = index;
                break;
            }
        }
        if (translation_heading != lines.size()) {
            const size_t end = explanation_heading == lines.size()
                ? lines.size()
                : explanation_heading;
            std::ostringstream extracted;
            for (size_t index = translation_heading + 1; index < end; ++index) {
                if (index > translation_heading + 1) {
                    extracted << '\n';
                }
                extracted << lines[index];
            }
            candidate = Trim(extracted.str());
        } else if (explanation_heading != lines.size()) {
            error = "the model returned an explanation instead of a translation";
            return false;
        }
    }

    if (candidate.empty()) {
        error = "the model returned no translated text";
        return false;
    }
    if (Trim(request.text) == candidate) {
        error = "the model copied the source text unchanged";
        return false;
    }
    if (HasWrongRussianUkrainianLanguage(request, candidate)) {
        error = "the output is still written in the source language";
        return false;
    }

    translation = std::move(candidate);
    return true;
}

} // namespace textlt
