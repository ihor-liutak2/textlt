#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "remote/remote_sftp_provider.hpp"

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
    const std::string output =
        "sftp> ls -la /docs\n"
        "drwxr-xr-x    2 user group 4096 Jan 10 12:30 Folder One\n"
        "-rw-r--r--    1 user group 123 Jan 10 2025 notes.txt\n"
        "lrwxrwxrwx    1 user group 8 Jan 10 12:31 current -> notes.txt\n";
    std::vector<RemoteEntry> entries;
    RemoteSftpProvider::ParseSftpListing(output, "/docs", entries);
    Expect(entries.size() == 3, "SFTP listing entry count mismatch.");
    Expect(entries[0].type == RemoteEntryType::Directory, "SFTP directory type mismatch.");
    Expect(entries[0].name == "Folder One", "SFTP directory name mismatch.");
    Expect(entries[0].path == "/docs/Folder One", "SFTP directory path mismatch.");
    Expect(entries[1].size == 123, "SFTP file size mismatch.");
    Expect(entries[2].type == RemoteEntryType::Symlink, "SFTP symlink type mismatch.");
    Expect(entries[2].name == "current", "SFTP symlink name mismatch.");
    return 0;
}
