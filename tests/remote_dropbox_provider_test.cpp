#include <cstdlib>
#include <iostream>
#include <string>

#include "remote/remote_dropbox_provider.hpp"

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
    using textlt::RemoteDropboxProvider;

    ExpectEqual(RemoteDropboxProvider::DropboxApiPathFromRemotePath("/"), "", "root path");
    ExpectEqual(RemoteDropboxProvider::DropboxApiPathFromRemotePath(""), "", "empty path");
    ExpectEqual(RemoteDropboxProvider::DropboxApiPathFromRemotePath("folder/file.txt"), "/folder/file.txt", "relative path");
    ExpectEqual(RemoteDropboxProvider::DropboxApiPathFromRemotePath("/folder//sub/../file.txt"), "/folder/file.txt", "normalized path");

    ExpectEqual(RemoteDropboxProvider::RemotePathFromDropboxDisplayPath(""), "/", "display root");
    ExpectEqual(RemoteDropboxProvider::RemotePathFromDropboxDisplayPath("/Folder/File.txt"), "/Folder/File.txt", "display path");
    ExpectEqual(RemoteDropboxProvider::RemotePathFromDropboxDisplayPath("Folder/File.txt"), "/Folder/File.txt", "display relative path");

    return 0;
}
