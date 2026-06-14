#include "database_lexer.hpp"

#include <unordered_set>

#include "lexer_common.hpp"

namespace textlt::lexers {

const std::unordered_set<std::string>& GraphqlKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "enum",
        "fragment",
        "input",
        "interface",
        "mutation",
        "query",
        "scalar",
        "schema",
        "subscription",
        "type",
    };
    return keywords;
}

const std::unordered_set<std::string>& SqlKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "BY",
        "CREATE",
        "DELETE",
        "DROP",
        "FROM",
        "GROUP",
        "INDEX",
        "INNER",
        "INSERT",
        "INTO",
        "JOIN",
        "LEFT",
        "ON",
        "ORDER",
        "RIGHT",
        "SELECT",
        "TABLE",
        "UPDATE",
        "VALUES",
        "WHERE",
    };
    return keywords;
}

std::vector<SyntaxHighlighter::Token> TokenizeGraphql(const std::string& line) {
    std::vector<SyntaxHighlighter::Token> tokens;
    tokens.reserve(line.size() / 4 + 1);

    size_t index = 0;
    while (index < line.size()) {
        if (line[index] == '#') {
            PushToken(tokens, index, line.size(), SyntaxHighlighter::Style::Comment);
            break;
        }

        if (line[index] == '"' || line[index] == '\'') {
            const size_t end = ParseQuotedStringEnd(line, index);
            PushToken(tokens, index, end, SyntaxHighlighter::Style::String);
            index = end;
            continue;
        }

        if (line[index] == '$' && index + 1 < line.size() && IsIdentifierStart(line[index + 1])) {
            const size_t start = index;
            index += 2;
            while (index < line.size() && IsIdentifierBody(line[index])) {
                ++index;
            }
            PushToken(tokens, start, index, SyntaxHighlighter::Style::Type);
            continue;
        }

        if (IsNumberStart(line, index)) {
            const size_t start = index;
            index = ParseNumberEnd(line, index);
            PushToken(tokens, start, index, SyntaxHighlighter::Style::Number);
            continue;
        }

        if (IsIdentifierStart(line[index])) {
            const size_t start = index;
            while (index < line.size() && IsIdentifierBody(line[index])) {
                ++index;
            }
            const std::string word = line.substr(start, index - start);
            SyntaxHighlighter::Style style = SyntaxHighlighter::Style::Normal;
            if (GraphqlKeywords().find(word) != GraphqlKeywords().end()) {
                style = SyntaxHighlighter::Style::Keyword;
            } else {
                const size_t lookahead = SkipWhitespace(line, index);
                if (lookahead < line.size() && line[lookahead] == ':') {
                    style = SyntaxHighlighter::Style::Type;
                }
            }
            PushToken(tokens, start, index, style);
            continue;
        }

        PushToken(tokens, index, index + 1, SyntaxHighlighter::Style::Normal);
        ++index;
    }

    return tokens;
}

std::vector<SyntaxHighlighter::Token> TokenizeSql(const std::string& line) {
    std::vector<SyntaxHighlighter::Token> tokens;
    tokens.reserve(line.size() / 4 + 1);

    size_t index = 0;
    while (index < line.size()) {
        if (StartsWith(line, index, "--")) {
            PushToken(tokens, index, line.size(), SyntaxHighlighter::Style::Comment);
            break;
        }

        if (StartsWith(line, index, "/*")) {
            const size_t end = FindCommentEnd(line, index + 2, "*/");
            PushToken(tokens, index, end, SyntaxHighlighter::Style::Comment);
            index = end;
            continue;
        }

        if (line[index] == '"' || line[index] == '\'') {
            const size_t end = ParseQuotedStringEnd(line, index);
            PushToken(tokens, index, end, SyntaxHighlighter::Style::String);
            index = end;
            continue;
        }

        if (IsNumberStart(line, index)) {
            const size_t start = index;
            index = ParseNumberEnd(line, index);
            PushToken(tokens, start, index, SyntaxHighlighter::Style::Number);
            continue;
        }

        if (IsIdentifierStart(line[index])) {
            const size_t start = index;
            while (index < line.size() && IsIdentifierBody(line[index])) {
                ++index;
            }
            const std::string word = ToUpper(line.substr(start, index - start));
            const SyntaxHighlighter::Style style =
                SqlKeywords().find(word) != SqlKeywords().end()
                    ? SyntaxHighlighter::Style::Keyword
                    : SyntaxHighlighter::Style::Normal;
            PushToken(tokens, start, index, style);
            continue;
        }

        PushToken(tokens, index, index + 1, SyntaxHighlighter::Style::Normal);
        ++index;
    }

    return tokens;
}

} // namespace textlt::lexers
