#pragma once

#include <string>

#include "ftxui/component/event.hpp"

namespace textlt {

enum class ShortcutModifier {
    Ctrl,
    Alt,
    CtrlAlt,
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

inline std::string UkrainianUpperKeyForUsKey(char key) {
    switch (key) {
        case 'q': return "Й";
        case 'w': return "Ц";
        case 'e': return "У";
        case 'r': return "К";
        case 't': return "Е";
        case 'y': return "Н";
        case 'u': return "Г";
        case 'i': return "Ш";
        case 'o': return "Щ";
        case 'p': return "З";
        case 'a': return "Ф";
        case 's': return "І";
        case 'd': return "В";
        case 'f': return "А";
        case 'g': return "П";
        case 'h': return "Р";
        case 'j': return "О";
        case 'k': return "Л";
        case 'l': return "Д";
        case 'z': return "Я";
        case 'x': return "Ч";
        case 'c': return "С";
        case 'v': return "М";
        case 'b': return "И";
        case 'n': return "Т";
        case 'm': return "Ь";
        case '/': return ",";
        default: return {};
    }
}

inline bool MatchesPlainShortcutKey(const ftxui::Event& event, char us_key) {
    if (event.is_mouse() || event.is_cursor_position()) {
        return false;
    }

    const std::string& input = event.input();
    const char lower = us_key >= 'A' && us_key <= 'Z'
        ? static_cast<char>(us_key - 'A' + 'a')
        : us_key;
    const char upper = lower >= 'a' && lower <= 'z'
        ? static_cast<char>(lower - 'a' + 'A')
        : lower;

    if (input == std::string(1, lower) || input == std::string(1, upper)) {
        return true;
    }

    const std::string ukrainian = UkrainianKeyForUsKey(lower);
    if (!ukrainian.empty() && input == ukrainian) {
        return true;
    }

    const std::string ukrainian_upper = UkrainianUpperKeyForUsKey(lower);
    return !ukrainian_upper.empty() && input == ukrainian_upper;
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
    } else if (modifier == ShortcutModifier::Alt) {
        if (input == "Alt+" + std::string(1, upper) ||
            input == "\x1B" + std::string(1, lower) ||
            input == "\x1B" + std::string(1, upper)) {
            return true;
        }
    } else if (modifier == ShortcutModifier::CtrlAlt) {
        const std::string named = "Ctrl+Alt+" + std::string(1, upper);
        const std::string swapped = "Alt+Ctrl+" + std::string(1, upper);
        if (input == named || input == swapped ||
            event == ftxui::Event::Special(named) ||
            event == ftxui::Event::Special(swapped)) {
            return true;
        }
    }

    const int modifier_code = modifier == ShortcutModifier::Ctrl ? 5 :
        modifier == ShortcutModifier::Alt ? 3 : 7;
    const std::string modifiers = std::to_string(modifier_code);

    auto matches_modified_code_point = [&](int code_point) {
        const std::string code = std::to_string(code_point);
        return input == "\x1B[" + code + ";" + modifiers + "u" ||
            input == "\x1B[27;" + modifiers + ";" + code + "~" ||
            input == "\x1B[" + code + ";" + modifiers + "~";
    };

    if (matches_modified_code_point(static_cast<unsigned char>(lower)) ||
        matches_modified_code_point(static_cast<unsigned char>(upper))) {
        return true;
    }

    const std::string ukrainian = UkrainianKeyForUsKey(lower);
    const int code_point = Utf8CodePoint(ukrainian);
    if (code_point < 0) return false;
    const std::string ukrainian_upper = UkrainianUpperKeyForUsKey(lower);
    const int upper_code_point = Utf8CodePoint(ukrainian_upper);

    if (modifier == ShortcutModifier::Alt) {
        return input == "\x1B" + ukrainian ||
            input == "\x1B" + ukrainian_upper ||
            input == "Alt+" + ukrainian ||
            input == "Alt+" + ukrainian_upper ||
            matches_modified_code_point(code_point) ||
            (upper_code_point >= 0 && matches_modified_code_point(upper_code_point));
    }

    if (modifier == ShortcutModifier::CtrlAlt) {
        return input == "\x1B" + ukrainian ||
            input == "\x1B" + ukrainian_upper ||
            input == "Ctrl+Alt+" + ukrainian ||
            input == "Ctrl+Alt+" + ukrainian_upper ||
            input == "Alt+Ctrl+" + ukrainian ||
            input == "Alt+Ctrl+" + ukrainian_upper ||
            matches_modified_code_point(code_point) ||
            (upper_code_point >= 0 && matches_modified_code_point(upper_code_point));
    }

    return input == "Ctrl+" + ukrainian ||
        input == "Ctrl+" + ukrainian_upper ||
        matches_modified_code_point(code_point) ||
        (upper_code_point >= 0 && matches_modified_code_point(upper_code_point));
}

} // namespace textlt
