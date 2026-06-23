#include "search_in_files.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <sstream>
#include <system_error>

namespace textlt {
namespace {

std::string LowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string NormalizeSeparators(std::string value) {
    std::replace(value.begin(), value.end(), '\\', '/');
    return value;
}

bool HasDirectorySeparator(const std::string& value) {
    return value.find('/') != std::string::npos ||
           value.find('\\') != std::string::npos;
}

bool WildcardMatch(const std::string& pattern, const std::string& text) {
    size_t pattern_index = 0;
    size_t text_index = 0;
    size_t star_index = std::string::npos;
    size_t match_index = 0;

    while (text_index < text.size()) {
        if (pattern_index < pattern.size() &&
            (pattern[pattern_index] == '?' || pattern[pattern_index] == text[text_index])) {
            ++pattern_index;
            ++text_index;
            continue;
        }

        if (pattern_index < pattern.size() && pattern[pattern_index] == '*') {
            star_index = pattern_index++;
            match_index = text_index;
            continue;
        }

        if (star_index != std::string::npos) {
            pattern_index = star_index + 1;
            text_index = ++match_index;
            continue;
        }

        return false;
    }

    while (pattern_index < pattern.size() && pattern[pattern_index] == '*') {
        ++pattern_index;
    }

    return pattern_index == pattern.size();
}

bool ContainsIgnoredDirectory(
    const std::vector<std::string>& ignored_directories,
    const std::string& directory_name) {
    const std::string normalized_name = LowerCopy(directory_name);

    for (const std::string& ignored : ignored_directories) {
        if (LowerCopy(ignored) == normalized_name) {
            return true;
        }
    }

    return false;
}

bool IsLikelyBinaryFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return true;
    }

    std::array<char, 4096> buffer{};
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const std::streamsize bytes_read = input.gcount();

    for (std::streamsize index = 0; index < bytes_read; ++index) {
        if (buffer[static_cast<size_t>(index)] == '\0') {
            return true;
        }
    }

    return false;
}

std::string CleanLine(std::string line) {
    line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());
    std::replace(line.begin(), line.end(), '\t', ' ');
    return line;
}

std::string RootLabelForPath(const std::filesystem::path& path) {
    const std::string filename = path.filename().string();
    if (!filename.empty()) {
        return filename;
    }

    const std::string generic = path.generic_string();
    return generic.empty() ? "." : generic;
}

void AddError(FileSearchSummary* summary, const std::string& error) {
    if (summary && !error.empty()) {
        summary->errors.push_back(error);
    }
}

std::vector<FileSearchContextLine> BuildContextBefore(
    const std::vector<std::string>& lines,
    size_t match_index,
    size_t context_before) {
    std::vector<FileSearchContextLine> result;
    if (context_before == 0 || lines.empty() || match_index >= lines.size()) {
        return result;
    }

    const size_t start = match_index > context_before ? match_index - context_before : 0;
    for (size_t index = start; index < match_index; ++index) {
        result.push_back({index + 1, lines[index]});
    }

    return result;
}

std::vector<FileSearchContextLine> BuildContextAfter(
    const std::vector<std::string>& lines,
    size_t match_index,
    size_t context_after) {
    std::vector<FileSearchContextLine> result;
    if (context_after == 0 || lines.empty() || match_index >= lines.size()) {
        return result;
    }

    const size_t end = std::min(lines.size(), match_index + context_after + 1);
    for (size_t index = match_index + 1; index < end; ++index) {
        result.push_back({index + 1, lines[index]});
    }

    return result;
}

} // namespace

bool FileSearchSummary::HasErrors() const {
    return !errors.empty();
}

std::string FileSearchSummary::FirstError() const {
    return errors.empty() ? std::string() : errors.front();
}

FileSearchMaskSet FileSearchEngine::DefaultCodeMaskSet() {
    return {
        "C/C++ Project",
        "*.cpp *.hpp *.h *.cxx *.cc *.c *.hh *.ipp *.inl *.txt *.md *.cmake CMakeLists.txt"
    };
}

std::vector<FileSearchMaskSet> FileSearchEngine::DefaultMaskSets() {
    return {
        {
            "C/C++ Project",
            "*.cpp *.hpp *.h *.cxx *.cc *.c *.hh *.ipp *.inl *.cmake CMakeLists.txt"
        },
        {
            "Text Documents",
            "*.txt *.md *.rst *.log"
        },
        {
            "Config Files",
            "*.json *.yaml *.yml *.toml *.ini *.conf *.env .env .env.*"
        },
        {
            "Web Project",
            "*.html *.htm *.css *.scss *.js *.jsx *.ts *.tsx *.json"
        },
        {
            "All Text Files",
            "*"
        }
    };
}

std::vector<std::string> FileSearchEngine::ParseMasks(const std::string& masks) {
    std::string normalized = masks;
    for (char& ch : normalized) {
        if (ch == ',' || ch == ';' || ch == '\n' || ch == '\r' || ch == '\t') {
            ch = ' ';
        }
    }

    std::vector<std::string> result;
    std::istringstream stream(normalized);
    std::string mask;

    while (stream >> mask) {
        result.push_back(mask);
    }

    return result;
}

bool FileSearchEngine::FileNameMatchesAnyMask(
    const std::filesystem::path& relative_path,
    const std::vector<std::string>& masks) {
    if (masks.empty()) {
        return true;
    }

    const std::string filename =
        LowerCopy(NormalizeSeparators(relative_path.filename().string()));
    const std::string relative =
        LowerCopy(NormalizeSeparators(relative_path.generic_string()));

    for (const std::string& raw_mask : masks) {
        if (raw_mask.empty() || raw_mask == "*") {
            return true;
        }

        const std::string mask = LowerCopy(NormalizeSeparators(raw_mask));
        const std::string& value = HasDirectorySeparator(mask) ? relative : filename;

        if (WildcardMatch(mask, value)) {
            return true;
        }
    }

    return false;
}

FileSearchSummary FileSearchEngine::SearchDirectory(
    const std::filesystem::path& root,
    const std::string& query) const {
    return SearchDirectory(root, query, DefaultCodeMaskSet(), 0, 0);
}

FileSearchSummary FileSearchEngine::SearchDirectory(
    const std::filesystem::path& root,
    const std::string& query,
    const FileSearchMaskSet& mask_set,
    size_t context_before,
    size_t context_after) const {
    FileSearchOptions options;
    options.roots.push_back({root, RootLabelForPath(root)});
    options.query = query;
    options.mask_set = mask_set;
    options.context_before = context_before;
    options.context_after = context_after;
    return Search(options);
}

FileSearchSummary FileSearchEngine::SearchDirectories(
    const std::vector<std::filesystem::path>& roots,
    const std::string& query,
    const FileSearchMaskSet& mask_set,
    size_t context_before,
    size_t context_after) const {
    FileSearchOptions options;
    options.query = query;
    options.mask_set = mask_set;
    options.context_before = context_before;
    options.context_after = context_after;

    for (const std::filesystem::path& root : roots) {
        options.roots.push_back({root, RootLabelForPath(root)});
    }

    return Search(options);
}

FileSearchSummary FileSearchEngine::Search(const FileSearchOptions& options) const {
    FileSearchSummary summary;

    if (options.query.empty()) {
        summary.errors.push_back("Search query is empty.");
        return summary;
    }

    if (options.roots.empty()) {
        summary.errors.push_back("No search directories selected.");
        return summary;
    }

    if (options.max_results == 0) {
        summary.truncated = true;
        return summary;
    }

    const std::vector<std::string> masks = ParseMasks(options.mask_set.value);

    for (const FileSearchRoot& root : options.roots) {
        if (summary.truncated) {
            break;
        }
        SearchRoot(root, options, masks, &summary);
    }

    return summary;
}

void FileSearchEngine::SearchRoot(
    const FileSearchRoot& root,
    const FileSearchOptions& options,
    const std::vector<std::string>& masks,
    FileSearchSummary* summary) const {
    if (!summary) {
        return;
    }

    std::error_code error;
    const std::filesystem::path absolute_root =
        std::filesystem::absolute(root.path, error).lexically_normal();

    if (error) {
        AddError(summary, "Cannot resolve search root: " + root.path.string());
        return;
    }

    const std::string root_label =
        root.label.empty() ? RootLabelForPath(absolute_root) : root.label;

    std::error_code status_error;
    if (std::filesystem::is_regular_file(absolute_root, status_error)) {
        ++summary->roots_scanned;

        const std::filesystem::path parent =
            absolute_root.has_parent_path() ? absolute_root.parent_path() : std::filesystem::current_path();

        SearchFile(
            parent,
            root_label,
            absolute_root,
            absolute_root.filename(),
            options,
            masks,
            summary);
        return;
    }

    if (!std::filesystem::is_directory(absolute_root, status_error)) {
        AddError(summary, "Search root is not a directory: " + absolute_root.string());
        return;
    }

    ++summary->roots_scanned;

    std::filesystem::recursive_directory_iterator iterator(
        absolute_root,
        std::filesystem::directory_options::skip_permission_denied,
        error);

    if (error) {
        AddError(summary, "Cannot open search root: " + absolute_root.string());
        return;
    }

    const std::filesystem::recursive_directory_iterator end;

    while (iterator != end) {
        const std::filesystem::directory_entry entry = *iterator;

        std::error_code entry_error;
        if (entry.is_directory(entry_error)) {
            ++summary->directories_scanned;

            const std::string directory_name = entry.path().filename().string();
            if (ContainsIgnoredDirectory(options.ignored_directories, directory_name)) {
                iterator.disable_recursion_pending();
            }

            iterator.increment(error);
            if (error) {
                AddError(summary, error.message());
                error.clear();
            }
            continue;
        }

        if (entry.is_regular_file(entry_error)) {
            const std::filesystem::path relative_path =
                entry.path().lexically_relative(absolute_root);

            SearchFile(
                absolute_root,
                root_label,
                entry.path(),
                relative_path.empty() ? entry.path().filename() : relative_path,
                options,
                masks,
                summary);

            if (summary->truncated) {
                return;
            }
        } else {
            ++summary->files_skipped;
        }

        iterator.increment(error);
        if (error) {
            AddError(summary, error.message());
            error.clear();
        }
    }
}

void FileSearchEngine::SearchFile(
    const std::filesystem::path& root,
    const std::string& root_label,
    const std::filesystem::path& file_path,
    const std::filesystem::path& relative_path,
    const FileSearchOptions& options,
    const std::vector<std::string>& masks,
    FileSearchSummary* summary) const {
    if (!summary || summary->truncated) {
        return;
    }

    if (!FileNameMatchesAnyMask(relative_path, masks)) {
        ++summary->files_skipped;
        return;
    }

    std::error_code size_error;
    const auto file_size = std::filesystem::file_size(file_path, size_error);
    if (size_error || file_size > options.max_file_size_bytes || IsLikelyBinaryFile(file_path)) {
        ++summary->files_skipped;
        return;
    }

    std::ifstream input(file_path);
    if (!input) {
        ++summary->files_skipped;
        return;
    }

    std::vector<std::string> lines;
    std::string line;

    while (std::getline(input, line)) {
        lines.push_back(CleanLine(std::move(line)));
    }

    ++summary->files_scanned;

    const std::string needle =
        options.match_case ? options.query : LowerCopy(options.query);

    const size_t matches_before_file = summary->matches.size();

    for (size_t line_index = 0; line_index < lines.size(); ++line_index) {
        const std::string haystack =
            options.match_case ? lines[line_index] : LowerCopy(lines[line_index]);

        size_t match_position = haystack.find(needle);
        while (match_position != std::string::npos) {
            FileSearchMatch match;
            match.path = file_path;
            match.root = root;
            match.relative_path = relative_path;
            match.root_label = root_label;
            match.line_number = line_index + 1;
            match.column = match_position + 1;
            match.line_text = lines[line_index];
            match.before = BuildContextBefore(lines, line_index, options.context_before);
            match.after = BuildContextAfter(lines, line_index, options.context_after);

            summary->matches.push_back(std::move(match));

            if (summary->matches.size() >= options.max_results) {
                summary->truncated = true;
                break;
            }

            const size_t step = std::max<size_t>(needle.size(), 1);
            match_position = haystack.find(needle, match_position + step);
        }

        if (summary->truncated) {
            break;
        }
    }

    if (summary->matches.size() > matches_before_file) {
        ++summary->files_with_matches;
    }
}

} // namespace textlt
