#include "remote/remote_ssh_config.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <system_error>
#ifndef _WIN32
#include <glob.h>
#endif

namespace textlt {
namespace {

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string StripComment(const std::string& line) {
    bool quoted = false;
    bool escaped = false;
    for (size_t index = 0; index < line.size(); ++index) {
        const char ch = line[index];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (ch == '\\') {
            escaped = true;
            continue;
        }
        if (ch == '"') {
            quoted = !quoted;
            continue;
        }
        if (ch == '#' && !quoted) {
            return line.substr(0, index);
        }
    }
    return line;
}

std::vector<std::string> SplitWords(const std::string& line) {
    std::vector<std::string> words;
    std::string word;
    bool quoted = false;
    bool escaped = false;
    for (char ch : line) {
        if (escaped) {
            word += ch;
            escaped = false;
        } else if (ch == '\\') {
            escaped = true;
        } else if (ch == '"') {
            quoted = !quoted;
        } else if (std::isspace(static_cast<unsigned char>(ch)) && !quoted) {
            if (!word.empty()) {
                words.push_back(std::move(word));
                word.clear();
            }
        } else {
            word += ch;
        }
    }
    if (escaped) {
        word += '\\';
    }
    if (!word.empty()) {
        words.push_back(std::move(word));
    }
    return words;
}

std::filesystem::path ExpandHome(std::filesystem::path path) {
    const std::string text = path.string();
    if (text == "~" || text.rfind("~/", 0) == 0 || text.rfind("~\\", 0) == 0) {
#ifdef _WIN32
        const char* home = std::getenv("USERPROFILE");
#else
        const char* home = std::getenv("HOME");
#endif
        if (home && *home) {
            return text.size() == 1
                ? std::filesystem::path(home)
                : std::filesystem::path(home) / text.substr(2);
        }
    }
    return path;
}

std::vector<std::filesystem::path> ExpandIncludePattern(
    const std::filesystem::path& pattern,
    const std::filesystem::path& base_directory) {
    std::filesystem::path resolved = ExpandHome(pattern);
    if (resolved.is_relative()) {
        resolved = base_directory / resolved;
    }
    std::vector<std::filesystem::path> matches;
#ifdef _WIN32
    if (std::filesystem::is_regular_file(resolved)) {
        matches.push_back(resolved);
    }
#else
    glob_t expanded{};
    if (::glob(resolved.c_str(), GLOB_TILDE, nullptr, &expanded) == 0) {
        for (size_t index = 0; index < expanded.gl_pathc; ++index) {
            matches.emplace_back(expanded.gl_pathv[index]);
        }
    }
    ::globfree(&expanded);
#endif
    std::sort(matches.begin(), matches.end());
    return matches;
}

bool IsConcreteHost(const std::string& host) {
    return !host.empty() && host.front() != '!' &&
        host.find_first_of("*?") == std::string::npos;
}

void ParseConfigFile(
    const std::filesystem::path& path,
    std::set<std::filesystem::path>& visited,
    std::set<std::string>& hosts,
    std::string& error) {
    std::error_code canonical_error;
    std::filesystem::path identity = std::filesystem::weakly_canonical(path, canonical_error);
    if (canonical_error) {
        identity = std::filesystem::absolute(path, canonical_error);
    }
    if (!visited.insert(identity).second) {
        return;
    }

    std::ifstream file(path);
    if (!file) {
        if (error.empty()) {
            error = "Cannot read SSH config: " + path.string();
        }
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        line = StripComment(line);
        const size_t first_space = line.find_first_of(" \t");
        const size_t equals = line.find('=');
        if (equals != std::string::npos &&
            (first_space == std::string::npos || equals < first_space)) {
            line[equals] = ' ';
        }
        const std::vector<std::string> words = SplitWords(line);
        if (words.size() < 2) {
            continue;
        }
        const std::string keyword = Lower(words.front());
        if (keyword == "host") {
            for (size_t index = 1; index < words.size(); ++index) {
                if (IsConcreteHost(words[index])) {
                    hosts.insert(words[index]);
                }
            }
        } else if (keyword == "include") {
            for (size_t index = 1; index < words.size(); ++index) {
                for (const auto& include : ExpandIncludePattern(words[index], path.parent_path())) {
                    ParseConfigFile(include, visited, hosts, error);
                }
            }
        }
    }
}

} // namespace

std::filesystem::path DefaultSshConfigPath() {
#ifdef _WIN32
    const char* home = std::getenv("USERPROFILE");
#else
    const char* home = std::getenv("HOME");
#endif
    return home && *home ? std::filesystem::path(home) / ".ssh" / "config"
                         : std::filesystem::path(".ssh") / "config";
}

std::vector<std::string> DiscoverSshConfigHosts(
    const std::filesystem::path& config_path,
    std::string& error) {
    error.clear();
    std::set<std::filesystem::path> visited;
    std::set<std::string> hosts;
    ParseConfigFile(config_path, visited, hosts, error);
    return {hosts.begin(), hosts.end()};
}

} // namespace textlt
