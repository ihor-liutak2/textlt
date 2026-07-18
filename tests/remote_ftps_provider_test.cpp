#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "remote/remote_ftps_provider.hpp"

namespace {

void Expect(bool value, const std::string& message) {
    if (!value) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

} // namespace

int main() {
    using namespace textlt;

    std::vector<RemoteEntry> entries;
    const std::string unix_listing =
        "drwxr-xr-x 2 owner group 4096 Jan 10 12:30 Folder One\r\n"
        "-rw-r--r-- 1 owner group 123 Jan 10 2025 notes.txt\r\n"
        "lrwxrwxrwx 1 owner group 8 Jan 10 12:31 current -> notes.txt\r\n";
    Expect(RemoteFtpsProvider::ParseDirectoryListing(unix_listing, "/docs", entries),
        "Unix FTPS listing should parse.");
    Expect(entries.size() == 3, "Unix FTPS listing entry count mismatch.");
    Expect(entries[0].type == RemoteEntryType::Directory, "Unix directory type mismatch.");
    Expect(entries[0].name == "Folder One", "Unix directory name mismatch.");
    Expect(entries[0].path == "/docs/Folder One", "Unix directory path mismatch.");
    Expect(entries[1].size == 123, "Unix file size mismatch.");
    Expect(entries[2].type == RemoteEntryType::Symlink, "Unix symlink type mismatch.");
    Expect(entries[2].name == "current", "Unix symlink name mismatch.");

    const std::string windows_listing =
        "01-10-2025  12:30PM       <DIR>          Uploads\r\n"
        "01-10-2025  12:31PM               456 report.txt\r\n";
    Expect(RemoteFtpsProvider::ParseDirectoryListing(windows_listing, "/", entries),
        "Windows FTPS listing should parse.");
    Expect(entries.size() == 2, "Windows FTPS listing entry count mismatch.");
    Expect(entries[0].type == RemoteEntryType::Directory, "Windows directory type mismatch.");
    Expect(entries[1].size == 456, "Windows file size mismatch.");

    RemoteFtpsProvider provider;
    RemoteConnectionConfig wrong_type;
    wrong_type.type = RemoteConnectionType::Sftp;
    wrong_type.host = "example.com";
    std::string error;
    Expect(!provider.Connect(wrong_type, error), "FTPS provider must reject non-FTPS config.");

    return 0;
}
