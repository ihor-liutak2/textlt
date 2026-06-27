#pragma once

#include <string>

#include "ftxui/component/event.hpp"

namespace textlt {

enum class ShortcutModifier {
    Ctrl,
    Alt,
};

inline std::string UkrainianKeyForUsKey(char key) {
    switch (key) {
        case 'q': return "й";
        case 'w': return "ц";
        case 'e': return "у";
        case 'r': return "к";
        case 't': return "е";
        case 'y': return "н";
        case 'u': return "г";
        case 'i': return "ш";
        case 'o': return "щ";
        case 'p': return "з";
        case 'a': return "ф";
        case 's': return "і";
        case 'd': return "в";
        case 'f': return "а";
        case 'g': return "п";
        case 'h': return "р";
        case 'j': return "о";
        case 'k': return "л";
        case 'l': return "д";
        case 'z': return "я";
        case 'x': return "ч";
        case 'c': return "с";
        case 'v': return "м";
        case 'b': return "и";
        case 'n': return "т";
        case 'm': return "ь";
        case '/': return ".";
        default: return {};
    }
}

inline int Utf8CodePoint(const std::string& value) {
    if (value.empty()) return -1;
    const auto first = static_cast<unsigned char>(value[0]);
    if (first < 0x80) return first;
    if ((first & 0xE0) == 0xC0 && value.size() >= 2) {
        return ((first & 0x1F) << 6) |
            (static_cast<unsigned char>(value[1]) & 0x3F);
    }
    return -1;
}

inline bool MatchesShortcut(
    const ftxui::Event& event,
    ShortcutModifier modifier,
    char us_key) {
    const std::string& input = event.input();
    const char lower = us_key >= 'A' && us_key <= 'Z'
        ? static_cast<char>(us_key - 'A' + 'a')
        : us_key;
    const char upper = lower >= 'a' && lower <= 'z'
        ? static_cast<char>(lower - 'a' + 'A')
        : lower;

    if (modifier == ShortcutModifier::Ctrl) {
        if (lower >= 'a' && lower <= 'z' &&
            input == std::string(1, static_cast<char>(lower - 'a' + 1))) {
            return true;
        }
        if (input == "Ctrl+" + std::string(1, upper)) {
            return true;
        }
    } else {
        if (input == "Alt+" + std::string(1, upper) ||
            input == "\x1B" + std::string(1, lower) ||
            input == "\x1B" + std::string(1, upper)) {
            return true;
        }
    }

    const std::string ukrainian = UkrainianKeyForUsKey(lower);
    const int code_point = Utf8CodePoint(ukrainian);
    if (code_point < 0) return false;

    if (modifier == ShortcutModifier::Alt && input == "\x1B" + ukrainian) {
        return true;
    }

    const int modifier_code = modifier == ShortcutModifier::Ctrl ? 5 : 3;
    const std::string code = std::to_string(code_point);
    const std::string modifiers = std::to_string(modifier_code);
    return input == "\x1B[" + code + ";" + modifiers + "u" ||
        input == "\x1B[27;" + modifiers + ";" + code + "~" ||
        input == "\x1B[" + code + ";" + modifiers + "~";
}

} // namespace textlt
