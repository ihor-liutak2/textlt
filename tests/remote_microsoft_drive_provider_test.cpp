#include <cstdlib>
#include <iostream>
#include <string>

#include "remote/remote_microsoft_drive_provider.hpp"

namespace {

void ExpectEqual(const std::string& actual, const std::string& expected, const char* label) {
    if (actual == expected) {
        return;
    }
    std::cerr << label << " failed\nexpected: " << expected << "\nactual:   " << actual << "\n";
    std::exit(1);
}

} // namespace

int main() {
    using textlt::RemoteConnectionConfig;
    using textlt::RemoteMicrosoftDriveProvider;

    ExpectEqual(RemoteMicrosoftDriveProvider::MicrosoftRemotePathFromDisplayPath("/"), "/", "root path");
    ExpectEqual(RemoteMicrosoftDriveProvider::MicrosoftRemotePathFromDisplayPath(""), "/", "empty path");
    ExpectEqual(RemoteMicrosoftDriveProvider::MicrosoftRemotePathFromDisplayPath("folder/file.txt"), "/folder/file.txt", "relative path");
    ExpectEqual(RemoteMicrosoftDriveProvider::MicrosoftRemotePathFromDisplayPath("/folder//sub/../file.txt"), "/folder/file.txt", "normalized path");

    RemoteConnectionConfig config;
    ExpectEqual(RemoteMicrosoftDriveProvider::MicrosoftDriveBasePathFromConfig(config), "/me/drive", "default drive base");
    config.site_id = "contoso.sharepoint.com,site-id,web-id";
    ExpectEqual(
        RemoteMicrosoftDriveProvider::MicrosoftDriveBasePathFromConfig(config),
        "/sites/contoso.sharepoint.com%2Csite-id%2Cweb-id/drive",
        "site default drive base");
    config.drive_id = "drive!abc";
    ExpectEqual(
        RemoteMicrosoftDriveProvider::MicrosoftDriveBasePathFromConfig(config),
        "/drives/drive%21abc",
        "explicit drive id wins");

    ExpectEqual(
        RemoteMicrosoftDriveProvider::MicrosoftEncodePathSegments("/Folder With Space/file #1.txt"),
        "Folder%20With%20Space/file%20%231.txt",
        "encoded graph path segments");

    return 0;
}
