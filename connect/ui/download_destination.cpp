#include "download_destination.hpp"

#include <algorithm>
#include <fstream>
#include <random>

namespace textlt::connect_ui {

std::string SafeDownloadBasename(std::string name) {
    const auto separator = name.find_last_of("/\\");
    if (separator != std::string::npos) name.erase(0, separator + 1);
    for (char& byte : name) {
        if (static_cast<unsigned char>(byte) < 32 || byte == 127 || std::string_view("<>:\"|?*").find(byte) !=
            std::string_view::npos) byte = '_';
    }
    while (!name.empty() && (name.back() == '.' || name.back() == ' ')) name.pop_back();
    if (name.empty()) return "download";
    auto stem = name.substr(0, name.find('.'));
    while (!stem.empty() && stem.back() == ' ') stem.pop_back();
    for (char& byte : stem) {
        if (byte >= 'a' && byte <= 'z') byte -= 'a' - 'A';
    }
    const bool device_port = (stem.compare(0, 3, "COM") == 0 || stem.compare(0, 3, "LPT") == 0) &&
        (stem.substr(3) == "¹" || stem.substr(3) == "²" || stem.substr(3) == "³" ||
         (stem.size() == 4 && stem[3] >= '0' && stem[3] <= '9'));
    if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL" ||
        device_port) name.insert(0, "_");
    return name;
}

namespace {

namespace fs = std::filesystem;

std::string FitName(std::string name, std::size_t capacity) {
    if (name.size() <= capacity) return name;
    while ((static_cast<unsigned char>(name[capacity]) & 0xc0) == 0x80) --capacity;
    name.resize(capacity);
    return name;
}

class DownloadDestination final : public connect::FileDestination {
public:
    DownloadDestination(fs::path downloads, fs::path staging, std::string basename)
        : downloads_(std::move(downloads)), staging_(std::move(staging)),
          basename_(std::move(basename)), output_(staging_ / "data", std::ios::binary) {}

    ~DownloadDestination() override {
        if (output_.is_open()) output_.close();
        std::error_code ignored;
        fs::remove(staging_ / "data", ignored);
        fs::remove(staging_, ignored);
    }

    std::ostream& Stream() override { return output_; }

    connect::ProviderResult Commit() override {
        output_.close();
        if (!output_) return {false, "could not close the Downloads staging file"};
        const auto base = fs::u8path(basename_);
        const auto extension = FitName(base.extension().u8string(), 64);
        for (unsigned number = 0; number < 10000; ++number) {
            const auto suffix = number ? " (" + std::to_string(number) + ")" : "";
            const auto name = !number ? FitName(basename_, 255)
                : FitName(base.stem().u8string(), 255 - suffix.size() - extension.size()) +
                    suffix + extension;
            std::error_code error;
            fs::create_hard_link(staging_ / "data", downloads_ / fs::u8path(name), error);
            if (!error) return {true, "Saved to Downloads as " + name};
            if (error != std::errc::file_exists) {
                return {false, "could not publish the file in Downloads: " + error.message()};
            }
        }
        return {false, "could not choose a unique name in Downloads"};
    }

private:
    fs::path downloads_;
    fs::path staging_;
    std::string basename_;
    std::ofstream output_;
};

} // namespace

std::unique_ptr<connect::FileDestination> MakeDownloadDestination(
    const std::filesystem::path& downloads, std::string name) {
    if (downloads.empty()) return {};
    std::error_code error;
    fs::create_directories(downloads, error);
    if (error) return {};
    name = SafeDownloadBasename(std::move(name));
    std::random_device random;
    for (int attempt = 0; attempt < 16; ++attempt) {
        const auto staging = downloads / (".textlt-connect-" + std::to_string(random()) +
                                           "-" + std::to_string(random()));
        if (fs::create_directory(staging, error)) {
#ifndef _WIN32
            fs::permissions(staging, fs::perms::owner_all, error);
            if (error) {
                fs::remove(staging, error);
                return {};
            }
#endif
            auto destination = std::make_unique<DownloadDestination>(downloads, staging, name);
            if (destination->Stream()) return destination;
            return {};
        }
        if (error && error != std::errc::file_exists) return {};
    }
    return {};
}

} // namespace textlt::connect_ui
