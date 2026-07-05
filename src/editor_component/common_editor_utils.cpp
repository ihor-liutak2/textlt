    namespace {

        // Checks if the character is an opening bracket or quote
        bool IsPairOpeningCharacter(char character) {
            return character == '{' || character == '[' || character == '(' ||
            character == '"' || character == '\'';
        }

        // Checks if the character is a closing bracket or quote
        bool IsPairClosingCharacter(char character) {
            return character == '}' || character == ']' || character == ')' ||
            character == '"' || character == '\'';
        }

        // Returns true if the string contains only whitespace characters
        bool IsBlankLine(const std::string& line) {
            return std::all_of(line.begin(), line.end(), [](unsigned char character) {
                return std::isspace(character);
            });
        }

        // Returns the corresponding closing character for a given opening character
        char ClosingPairFor(char character) {
            switch (character) {
                case '{': return '}';
                case '[': return ']';
                case '(': return ')';
                case '"': return '"';
                case '\'': return '\'';
                default: return '\0';
            }
        }

        // Returns the leading whitespace (indentation) of a line
        std::string LeadingIndent(const std::string& line) {
            size_t end = 0;
            while (end < line.size() && (line[end] == ' ' || line[end] == '\t')) {
                ++end;
            }
            return line.substr(0, end);
        }

        // Returns a copy of the string with trailing whitespace removed
        std::string TrimRightWhitespace(std::string value) {
            while (!value.empty() &&
                std::isspace(static_cast<unsigned char>(value.back()))) {
                value.pop_back();
                }
                return value;
        }

        // Checks if a trimmed line ends with the Ruby 'do' block keyword
        bool EndsWithRubyDo(const std::string& trimmed_line) {
            if (trimmed_line.size() < 2 ||
                trimmed_line.compare(trimmed_line.size() - 2, 2, "do") != 0) {
                return false;
                }
                if (trimmed_line.size() == 2) {
                    return true;
                }
                const unsigned char previous =
                static_cast<unsigned char>(trimmed_line[trimmed_line.size() - 3]);
            return !std::isalnum(previous) && previous != '_';
        }

        // Returns a lowercase copy of the provided string
        std::string ToLowerCopy(std::string value) {
            for (char& character : value) {
                character = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(character)));
            }
            return value;
        }

        bool IsBracketIgnoredStyle(SyntaxHighlighter::Style style) {
            return style == SyntaxHighlighter::Style::String ||
                style == SyntaxHighlighter::Style::Comment;
        }

        std::vector<std::vector<bool>> BuildIgnoredBracketMask(
            const std::vector<std::string>& lines,
            const std::string& file_path) {
            std::vector<std::vector<bool>> ignored(lines.size());
            SyntaxHighlighter::TokenizationContext context;

            for (size_t y = 0; y < lines.size(); ++y) {
                ignored[y].assign(lines[y].size(), false);
                const auto tokens = SyntaxHighlighter::TokenizeLine(lines[y], file_path, &context);
                for (const SyntaxHighlighter::Token& token : tokens) {
                    if (!IsBracketIgnoredStyle(token.style)) {
                        continue;
                    }
                    const size_t end = std::min(lines[y].size(), token.start + token.length);
                    for (size_t x = token.start; x < end; ++x) {
                        ignored[y][x] = true;
                    }
                }
            }

            return ignored;
        }

    } // namespace
