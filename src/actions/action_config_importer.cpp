#include "actions/action_config_importer.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <utility>

namespace textlt::actions {
namespace {

std::string Trim(std::string value) {
    const auto first = std::find_if_not(value.begin(), value.end(),
        [](unsigned char c) { return std::isspace(c); });
    const auto last = std::find_if_not(value.rbegin(), value.rend(),
        [](unsigned char c) { return std::isspace(c); }).base();
    return first < last ? std::string(first, last) : std::string{};
}

std::string Lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool EndsWith(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
        value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string JoinPath(const std::string& parent, const std::string& child) {
    return parent.empty() ? child : parent + "." + child;
}

size_t FindUnquoted(const std::string& text, char wanted) {
    char quote = 0;
    bool escaped = false;
    for (size_t index = 0; index < text.size(); ++index) {
        const char c = text[index];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (c == '\\' && quote != '\'') {
            escaped = true;
            continue;
        }
        if ((c == '\'' || c == '"')) {
            if (quote == c) quote = 0;
            else if (quote == 0) quote = c;
            continue;
        }
        if (quote == 0 && c == wanted) return index;
    }
    return std::string::npos;
}

std::string StripInlineComment(std::string value) {
    char quote = 0;
    bool escaped = false;
    for (size_t index = 0; index < value.size(); ++index) {
        const char c = value[index];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (c == '\\' && quote != '\'') {
            escaped = true;
            continue;
        }
        if (c == '\'' || c == '"') {
            if (quote == c) quote = 0;
            else if (quote == 0) quote = c;
            continue;
        }
        if (c == '#' && quote == 0 &&
            (index == 0 || std::isspace(static_cast<unsigned char>(value[index - 1])))) {
            value.resize(index);
            break;
        }
    }
    return Trim(std::move(value));
}

std::string DecodeQuoted(std::string value) {
    value = Trim(std::move(value));
    if (value.size() < 2 ||
        !((value.front() == '"' && value.back() == '"') ||
          (value.front() == '\'' && value.back() == '\''))) {
        return value;
    }
    const char quote = value.front();
    std::string decoded;
    decoded.reserve(value.size() - 2);
    for (size_t index = 1; index + 1 < value.size(); ++index) {
        char c = value[index];
        if (quote == '\'' && c == '\'' && index + 2 < value.size() &&
            value[index + 1] == '\'') {
            decoded.push_back('\'');
            ++index;
            continue;
        }
        if (quote == '"' && c == '\\' && index + 2 < value.size()) {
            const char escaped = value[++index];
            switch (escaped) {
                case 'n': decoded.push_back('\n'); break;
                case 'r': decoded.push_back('\r'); break;
                case 't': decoded.push_back('\t'); break;
                default: decoded.push_back(escaped); break;
            }
            continue;
        }
        decoded.push_back(c);
    }
    return decoded;
}

ActionConfigImportResult Failure(std::string error) {
    ActionConfigImportResult result;
    result.error = std::move(error);
    return result;
}

ActionConfigImportResult Finish(std::vector<ImportedActionValue> values) {
    if (values.empty()) return Failure("The selected file does not contain importable values.");
    ActionConfigImportResult result;
    result.success = true;
    result.values = std::move(values);
    return result;
}

ActionConfigImportResult ParseEnv(const std::string& content) {
    std::vector<ImportedActionValue> values;
    std::istringstream input(content);
    std::string line;
    size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        line = Trim(std::move(line));
        if (line.empty() || line.front() == '#') continue;
        if (line.compare(0, 7, "export ") == 0) line = Trim(line.substr(7));
        const size_t separator = FindUnquoted(line, '=');
        if (separator == std::string::npos) {
            return Failure("Invalid environment entry on line " + std::to_string(line_number) + ".");
        }
        std::string name = Trim(line.substr(0, separator));
        if (name.empty()) {
            return Failure("Environment name is empty on line " + std::to_string(line_number) + ".");
        }
        std::string value = StripInlineComment(line.substr(separator + 1));
        values.push_back({std::move(name), DecodeQuoted(std::move(value))});
    }
    return Finish(std::move(values));
}

std::string DecodeProperties(std::string value) {
    std::string decoded;
    decoded.reserve(value.size());
    for (size_t index = 0; index < value.size(); ++index) {
        if (value[index] != '\\' || index + 1 >= value.size()) {
            decoded.push_back(value[index]);
            continue;
        }
        const char escaped = value[++index];
        switch (escaped) {
            case 'n': decoded.push_back('\n'); break;
            case 'r': decoded.push_back('\r'); break;
            case 't': decoded.push_back('\t'); break;
            case 'f': decoded.push_back('\f'); break;
            default: decoded.push_back(escaped); break;
        }
    }
    return decoded;
}

ActionConfigImportResult ParseProperties(const std::string& content) {
    std::vector<ImportedActionValue> values;
    std::istringstream input(content);
    std::string physical;
    std::string line;
    size_t line_number = 0;
    while (std::getline(input, physical)) {
        ++line_number;
        if (!physical.empty() && physical.back() == '\r') physical.pop_back();
        line += physical;
        size_t slashes = 0;
        for (auto it = line.rbegin(); it != line.rend() && *it == '\\'; ++it) ++slashes;
        if (slashes % 2 == 1) {
            line.pop_back();
            continue;
        }
        std::string entry = Trim(std::move(line));
        line.clear();
        if (entry.empty() || entry.front() == '#' || entry.front() == '!') continue;

        size_t separator = std::string::npos;
        bool escaped = false;
        for (size_t index = 0; index < entry.size(); ++index) {
            if (escaped) { escaped = false; continue; }
            if (entry[index] == '\\') { escaped = true; continue; }
            if (entry[index] == '=' || entry[index] == ':' ||
                std::isspace(static_cast<unsigned char>(entry[index]))) {
                separator = index;
                break;
            }
        }
        std::string name = separator == std::string::npos ? entry : entry.substr(0, separator);
        size_t value_start = separator == std::string::npos ? entry.size() : separator;
        while (value_start < entry.size() &&
            std::isspace(static_cast<unsigned char>(entry[value_start]))) ++value_start;
        if (value_start < entry.size() && (entry[value_start] == '=' || entry[value_start] == ':')) ++value_start;
        while (value_start < entry.size() &&
            std::isspace(static_cast<unsigned char>(entry[value_start]))) ++value_start;
        name = DecodeProperties(name);
        if (name.empty()) return Failure("Property name is empty on line " + std::to_string(line_number) + ".");
        values.push_back({std::move(name), DecodeProperties(entry.substr(value_start))});
    }
    if (!line.empty()) return Failure("The properties file ends with an unfinished continuation.");
    return Finish(std::move(values));
}

struct YamlContext {
    int indent = -1;
    std::string path;
};

ActionConfigImportResult ParseYaml(const std::string& content) {
    std::vector<ImportedActionValue> values;
    std::vector<YamlContext> contexts{{-1, {}}};
    std::map<std::string, size_t> sequence_indices;
    std::istringstream input(content);
    std::string raw;
    size_t line_number = 0;
    while (std::getline(input, raw)) {
        ++line_number;
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        if (raw.find('\t') != std::string::npos) {
            return Failure("YAML indentation cannot contain tabs (line " +
                std::to_string(line_number) + ").");
        }
        const size_t first = raw.find_first_not_of(' ');
        if (first == std::string::npos) continue;
        std::string text = raw.substr(first);
        if (text.empty() || text.front() == '#' || text == "---" || text == "...") continue;
        const int indent = static_cast<int>(first);
        while (contexts.size() > 1 && indent <= contexts.back().indent) contexts.pop_back();
        const std::string parent = contexts.back().path;

        bool sequence = text == "-" || text.compare(0, 2, "- ") == 0;
        std::string item_path;
        if (sequence) {
            const size_t index = sequence_indices[parent]++;
            item_path = JoinPath(parent, std::to_string(index));
            text = text.size() == 1 ? std::string{} : Trim(text.substr(2));
            contexts.push_back({indent, item_path});
            if (text.empty()) continue;
        }

        const size_t colon = FindUnquoted(text, ':');
        if (colon == std::string::npos) {
            if (!sequence) {
                return Failure("Expected a YAML key and value on line " +
                    std::to_string(line_number) + ".");
            }
            values.push_back({item_path, DecodeQuoted(StripInlineComment(text))});
            contexts.pop_back();
            continue;
        }

        std::string key = DecodeQuoted(Trim(text.substr(0, colon)));
        if (key.empty()) return Failure("YAML key is empty on line " + std::to_string(line_number) + ".");
        const std::string base = sequence ? item_path : parent;
        const std::string path = JoinPath(base, key);
        std::string value = StripInlineComment(text.substr(colon + 1));
        if (value.empty()) {
            // A sequence mapping's key begins after the '- ' marker. Giving it
            // that logical indentation lets sibling keys pop this context.
            contexts.push_back({indent + (sequence ? 2 : 0), path});
            continue;
        }
        if (value == "|" || value == ">" || value == "|-" || value == ">-") {
            return Failure("YAML block scalars are not supported (line " +
                std::to_string(line_number) + ").");
        }
        values.push_back({path, DecodeQuoted(std::move(value))});
    }
    return Finish(std::move(values));
}

} // namespace

ActionConfigImportResult ActionConfigImporter::ImportFile(
    const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return Failure("Could not open the selected action file.");
    const std::string content{
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    return Parse(path, content);
}

ActionConfigImportResult ActionConfigImporter::Parse(
    const std::filesystem::path& path,
    const std::string& content) {
    const std::string filename = Lowercase(path.filename().string());
    const std::string extension = Lowercase(path.extension().string());
    if (extension == ".yaml" || extension == ".yml") return ParseYaml(content);
    if (extension == ".properties") return ParseProperties(content);
    if (filename == ".env" || filename.compare(0, 5, ".env.") == 0 ||
        extension == ".env" || EndsWith(filename, ".env")) {
        return ParseEnv(content);
    }
    return Failure("Choose an .env, .properties, .yaml, or .yml file.");
}

} // namespace textlt::actions
