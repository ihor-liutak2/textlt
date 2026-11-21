#include "remote/modal_remote_files.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <iterator>
#include <locale>
#include <stdexcept>
#include <regex>
#include <sstream>
#include <system_error>
#include <utility>

#include "ftxui/component/component_options.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"
#include "editor_utils.hpp"
#include "ui_button.hpp"
#include "remote/remote_dialog_theme.hpp"

namespace textlt {
namespace {

constexpr int kVisibleRows = 14;
constexpr int kDoubleClickMinMs = 80;
constexpr int kDoubleClickMaxMs = 500;

bool IsBackspaceEvent(const ftxui::Event& event) {
    return event == ftxui::Event::Backspace ||
        event.input() == "\x7F" ||
        event.input() == "\x08";
}

bool IsPlainRemoteName(const std::string& name) {
    return !name.empty() &&
        name != "." &&
        name != ".." &&
        name.find('/') == std::string::npos &&
        name.find('\\') == std::string::npos;
}

std::string CurrentConnectionLabel(const std::vector<RemoteConnectionConfig>& connections, int index) {
    if (connections.empty()) {
        return "No connection";
    }
    index = std::clamp(index, 0, static_cast<int>(connections.size()) - 1);
    const RemoteConnectionConfig& config = connections[static_cast<size_t>(index)];
    if (!config.name.empty()) {
        return config.name;
    }
    if (!config.ssh_config_host.empty()) {
        return config.ssh_config_host;
    }
    if (!config.host.empty()) {
        return config.host;
    }
    return RemoteConnectionTypeToString(config.type);
}

std::string OverwriteCalendarDate(std::tm date, bool has_year) {
    static const auto locale = []() -> std::optional<std::locale> {
        try {
            return std::locale(std::locale::classic(), "", std::locale::time);
        } catch (const std::runtime_error&) {
            return std::nullopt;
        }
    }();
    const auto format = [&](const std::tm& day) {
        std::ostringstream out;
        out.imbue(locale.value_or(std::locale::classic()));
        out << std::put_time(&day, locale ? "%x" : "%d.%m.%Y");
        return out.str();
    };
    if (has_year) {
        return format(date);
    }

    // Locate the year in the regional short date without assuming its position or width.
    date.tm_year = 88;
    const std::string first = format(date);
    date.tm_year = 104;
    const std::string second = format(date);
    if (first == second) {
        return first;
    }
    const auto prefix = std::mismatch(first.begin(), first.end(), second.begin(), second.end());
    const auto suffix = std::mismatch(first.rbegin(), std::make_reverse_iterator(prefix.first),
                                      second.rbegin(), std::make_reverse_iterator(prefix.second));
    auto start = prefix.first;
    auto end = suffix.first.base();
    const auto continuation_byte = [](char byte) {
        return (static_cast<unsigned char>(byte) & 0xC0) == 0x80;
    };
    while (start != first.begin() && start != first.end() && continuation_byte(*start)) {
        --start;
    }
    while (end != first.end() && continuation_byte(*end)) {
        ++end;
    }
    return std::string(first.begin(), start) + "----" + std::string(end, first.end());
}

std::string FormatOverwriteDate(const std::string& value) {
    static const std::regex iso(
        R"((\d{4})-(\d{2})-(\d{2})T(\d{2}:\d{2})(?::\d{2}(?:\.\d+)?)?(Z|[+-]\d{2}:\d{2})?)");
    std::tm date{};
    std::smatch match;
    if (std::regex_match(value, match, iso)) {
        const std::string zone = match[5];
        const std::string suffix = zone == "Z" ? " UTC" : zone.empty() ? "" : " " + zone;
        date.tm_year = std::stoi(match[1]) - 1900;
        date.tm_mon = std::stoi(match[2]) - 1;
        date.tm_mday = std::stoi(match[3]);
        return OverwriteCalendarDate(date, true) + " " + match[4].str() + suffix;
    }

    static const std::regex dos(
        R"((\d{2})-(\d{2})-(\d{4}|\d{2})\s+(\d{2}):(\d{2})(AM|PM))", std::regex::icase);
    if (std::regex_match(value, match, dos)) {
        int year = std::stoi(match[3]);
        if (match.length(3) == 2) {
            year += year < 69 ? 2000 : 1900;
        }
        const char period = match[6].str().front();
        const int hour = std::stoi(match[4]) % 12 + (period == 'P' || period == 'p' ? 12 : 0);
        std::ostringstream formatted;
        date.tm_year = year - 1900;
        date.tm_mon = std::stoi(match[1]) - 1;
        date.tm_mday = std::stoi(match[2]);
        formatted << OverwriteCalendarDate(date, true) << ' '
                  << std::setfill('0') << std::setw(2) << hour << ':' << match[5];
        return formatted.str();
    }

    // Unix long listings supply either a year or a time of day.
    std::istringstream listing(value);
    listing.imbue(std::locale::classic());
    std::string year_or_time;
    if (listing >> std::get_time(&date, "%b %d") >> year_or_time) {
        static const std::regex year(R"(\d{4})");
        static const std::regex time(R"(\d{2}:\d{2})");
        if (std::regex_match(year_or_time, year)) {
            date.tm_year = std::stoi(year_or_time) - 1900;
            return OverwriteCalendarDate(date, true) + " --:--";
        }
        if (std::regex_match(year_or_time, time)) {
            return OverwriteCalendarDate(date, false) + " " + year_or_time;
        }
    }
    return "unknown";
}

std::string OverwriteDetails(const RemoteEntry& entry) {
    const std::string size = entry.type == RemoteEntryType::Directory ? "Directory"
        : entry.size ? std::to_string(*entry.size) + " bytes" : "Size unknown";
    return size + "  Modified " + FormatOverwriteDate(entry.modified);
}

RemoteEntry LocalOverwriteMetadata(const std::filesystem::path& path) {
    RemoteEntry entry;
    std::error_code error;
    if (std::filesystem::is_directory(path, error)) {
        entry.type = RemoteEntryType::Directory;
    } else if (std::filesystem::is_regular_file(path, error)) {
        const auto size = std::filesystem::file_size(path, error);
        if (!error) {
            entry.size = size;
        }
    }

    const auto modified = std::filesystem::last_write_time(path, error);
    if (!error) {
        using namespace std::chrono;
        const auto system_time = system_clock::now() + (modified - decltype(modified)::clock::now());
        const auto seconds_since_epoch = system_clock::to_time_t(round<seconds>(system_time));
        std::tm utc{};
#ifdef _WIN32
        const bool converted = gmtime_s(&utc, &seconds_since_epoch) == 0;
#else
        const bool converted = gmtime_r(&seconds_since_epoch, &utc) != nullptr;
#endif
        if (converted) {
            std::ostringstream formatted;
            formatted << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
            entry.modified = formatted.str();
        }
    }
    return entry;
}

} // namespace

#include "remote/modal_remote_files/setup.cpp"
#include "remote/modal_remote_files/render.cpp"
#include "remote/modal_remote_files/events.cpp"
#include "remote/modal_remote_files/loading_navigation.cpp"
#include "remote/modal_remote_files/transfers.cpp"
#include "remote/modal_remote_files/operations.cpp"
#include "remote/modal_remote_files/state_utils.cpp"
#include "remote/modal_remote_files/wrapper.cpp"

} // namespace textlt
