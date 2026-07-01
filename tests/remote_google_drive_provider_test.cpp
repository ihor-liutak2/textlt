#include <cstdlib>
#include <iostream>
#include <string>

#include "remote/remote_google_drive_provider.hpp"

namespace {

void ExpectEqual(const std::string& actual, const std::string& expected, const char* label) {
    if (actual == expected) {
        return;
    }
    std::cerr << label << " failed\nexpected: " << expected << "\nactual:   " << actual << "\n";
    std::exit(1);
}

void ExpectTrue(bool value, const char* label) {
    if (value) {
        return;
    }
    std::cerr << label << " failed\n";
    std::exit(1);
}

void ExpectFalse(bool value, const char* label) {
    if (!value) {
        return;
    }
    std::cerr << label << " failed\n";
    std::exit(1);
}

} // namespace

int main() {
    using textlt::RemoteConnectionConfig;
    using textlt::RemoteGoogleDriveProvider;

    ExpectEqual(RemoteGoogleDriveProvider::GoogleRemotePathFromDisplayPath("/"), "/", "root path");
    ExpectEqual(RemoteGoogleDriveProvider::GoogleRemotePathFromDisplayPath(""), "/", "empty path");
    ExpectEqual(RemoteGoogleDriveProvider::GoogleRemotePathFromDisplayPath("folder/file.txt"), "/folder/file.txt", "relative path");
    ExpectEqual(RemoteGoogleDriveProvider::GoogleRemotePathFromDisplayPath("/folder//sub/../file.txt"), "/folder/file.txt", "normalized path");

    RemoteConnectionConfig config;
    ExpectEqual(RemoteGoogleDriveProvider::GoogleRootIdFromConfig(config), "root", "default root id");
    config.root_folder_id = "abc123";
    ExpectEqual(RemoteGoogleDriveProvider::GoogleRootIdFromConfig(config), "abc123", "configured root id");

    ExpectTrue(RemoteGoogleDriveProvider::IsGoogleWorkspaceMimeType("application/vnd.google-apps.document"), "google doc mime");
    ExpectTrue(RemoteGoogleDriveProvider::IsGoogleWorkspaceMimeType("application/vnd.google-apps.spreadsheet"), "google sheet mime");
    ExpectFalse(RemoteGoogleDriveProvider::IsGoogleWorkspaceMimeType("application/vnd.google-apps.folder"), "folder is not exported doc");
    ExpectFalse(RemoteGoogleDriveProvider::IsGoogleWorkspaceMimeType("text/plain"), "plain file mime");

    return 0;
}
