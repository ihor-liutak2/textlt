/* Included by ../text_importer.cpp. */

bool IsRtfDestinationControl(const std::string& word) {
    static const char* ignored_destinations[] = {
        "fonttbl", "colortbl", "stylesheet", "info", "pict", "object",
        "header", "footer", "headerl", "headerr", "headerf",
        "footerl", "footerr", "footerf", "annotation", "atnid",
        "generator", "datafield", "datastore", "themedata", "colorschememapping",
        "xmlnstbl", "listtable", "listoverridetable", "rsidtbl", "revtbl",
        "latentstyles", "pnseclvl", "fontemb", "fontfile", "filetbl",
        "fldinst", "shp", "nonshppict", "stylesheet", "userprops",
    };

    for (const char* value : ignored_destinations) {
        if (word == value) {
            return true;
        }
    }
    return false;
}

int HexValue(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

std::string NormalizeImportedPlainText(const std::string& text) {
    std::string output;
    bool previous_blank = false;

    for (const std::string& raw_line : SplitLines(text)) {
        const std::string line = NormalizeInlineWhitespace(raw_line);
        if (line.empty()) {
            if (!output.empty() && !previous_blank) {
                AppendBlankLine(output);
                previous_blank = true;
            }
            continue;
        }
        AppendLine(output, line);
        previous_blank = false;
    }

    return TrimTrailingNewlines(output);
}

std::string ParseRtfDocument(const std::string& rtf) {
    struct RtfState {
        bool skip_destination = false;
        int unicode_skip_count = 1;
        int codepage = 1252;
    };

    std::vector<RtfState> stack(1);
    std::string output;
    int pending_fallback_skip = 0;

    auto append_text_byte = [&](unsigned char value) {
        if (stack.back().skip_destination) {
            return;
        }
        AppendCodepageByte(output, value, stack.back().codepage);
    };

    auto append_code_point = [&](uint32_t value) {
        if (!stack.back().skip_destination) {
            AppendUtf8CodePoint(output, value);
        }
    };

    for (size_t i = 0; i < rtf.size(); ++i) {
        const unsigned char current = static_cast<unsigned char>(rtf[i]);

        if (current == '{') {
            stack.push_back(stack.back());
            pending_fallback_skip = 0;
            continue;
        }
        if (current == '}') {
            if (stack.size() > 1) {
                stack.pop_back();
            }
            pending_fallback_skip = 0;
            continue;
        }
        if (current == '\\') {
            if (i + 1 >= rtf.size()) {
                break;
            }
            const char next = rtf[++i];

            if (next == '\\' || next == '{' || next == '}') {
                if (pending_fallback_skip > 0) {
                    --pending_fallback_skip;
                } else {
                    append_text_byte(static_cast<unsigned char>(next));
                }
                continue;
            }
            if (next == '\'') {
                if (i + 2 < rtf.size()) {
                    const int high = HexValue(rtf[i + 1]);
                    const int low = HexValue(rtf[i + 2]);
                    if (high >= 0 && low >= 0) {
                        if (pending_fallback_skip > 0) {
                            --pending_fallback_skip;
                        } else {
                            append_text_byte(static_cast<unsigned char>((high << 4) | low));
                        }
                        i += 2;
                    }
                }
                continue;
            }
            if (next == '*') {
                stack.back().skip_destination = true;
                continue;
            }
            if (!std::isalpha(static_cast<unsigned char>(next))) {
                if (pending_fallback_skip > 0) {
                    --pending_fallback_skip;
                    continue;
                }
                if (stack.back().skip_destination) {
                    continue;
                }
                switch (next) {
                    case '~': output.push_back(' '); break;
                    case '-': output.push_back('-'); break;
                    case '_': output.push_back('-'); break;
                    default: break;
                }
                continue;
            }

            std::string word;
            word.push_back(next);
            while (i + 1 < rtf.size() && std::isalpha(static_cast<unsigned char>(rtf[i + 1]))) {
                word.push_back(rtf[++i]);
            }

            bool has_parameter = false;
            int parameter_sign = 1;
            int parameter = 0;
            if (i + 1 < rtf.size() && rtf[i + 1] == '-') {
                parameter_sign = -1;
                has_parameter = true;
                ++i;
            }
            while (i + 1 < rtf.size() && std::isdigit(static_cast<unsigned char>(rtf[i + 1]))) {
                has_parameter = true;
                parameter = parameter * 10 + (rtf[++i] - '0');
            }
            parameter *= parameter_sign;
            if (i + 1 < rtf.size() && rtf[i + 1] == ' ') {
                ++i;
            }

            if (IsRtfDestinationControl(word)) {
                stack.back().skip_destination = true;
            }
            if (word == "ansicpg" && has_parameter) {
                stack.back().codepage = parameter;
            } else if (word == "uc" && has_parameter) {
                stack.back().unicode_skip_count = std::max(0, parameter);
            }

            if (stack.back().skip_destination) {
                continue;
            }

            if (word == "par" || word == "line") {
                output.push_back('\n');
            } else if (word == "page") {
                AppendBlankLine(output);
            } else if (word == "tab") {
                output.push_back('\t');
            } else if (word == "emdash") {
                AppendUtf8CodePoint(output, 0x2014);
            } else if (word == "endash") {
                AppendUtf8CodePoint(output, 0x2013);
            } else if (word == "bullet") {
                AppendUtf8CodePoint(output, 0x2022);
            } else if (word == "lquote") {
                AppendUtf8CodePoint(output, 0x2018);
            } else if (word == "rquote") {
                AppendUtf8CodePoint(output, 0x2019);
            } else if (word == "ldblquote") {
                AppendUtf8CodePoint(output, 0x201C);
            } else if (word == "rdblquote") {
                AppendUtf8CodePoint(output, 0x201D);
            } else if (word == "u" && has_parameter) {
                int value = parameter;
                if (value < 0) {
                    value += 65536;
                }
                append_code_point(static_cast<uint32_t>(value));
                pending_fallback_skip = stack.back().unicode_skip_count;
            }
            continue;
        }

        if (pending_fallback_skip > 0) {
            --pending_fallback_skip;
            continue;
        }
        if (current == '\r' || current == '\n') {
            continue;
        }
        append_text_byte(current);
    }

    return NormalizeImportedPlainText(output);
}

int AttributeIntByLocalName(const pugi::xml_node& node, const std::string& local_name, int fallback) {
    for (pugi::xml_attribute attribute : node.attributes()) {
        std::string name = attribute.name();
        const size_t separator = name.find(':');
        if (separator != std::string::npos) {
            name = name.substr(separator + 1);
        }
        if (name == local_name) {
            return std::max(1, attribute.as_int(fallback));
        }
    }
    return fallback;
}
