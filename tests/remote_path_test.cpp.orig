#include <iostream>
#include <string>

#include "remote/remote_connection_config.hpp"

namespace {

bool ExpectEqual(const std::string& label, const std::string& actual, const std::string& expected) {
    if (actual == expected) {
        return true;
    }
    std::cerr << label << " failed\n"
              << "  expected: " << expected << "\n"
              << "  actual:   " << actual << "\n";
    return false;
}

bool ExpectType(
    const std::string& label,
    textlt::RemoteConnectionType actual,
    textlt::RemoteConnectionType expected) {
    if (actual == expected) {
        return true;
    }
    std::cerr << label << " failed\n";
    return false;
}

} // namespace

int main() {
    bool ok = true;

    ok &= ExpectEqual("normalize root", textlt::NormalizeRemoteDirectory("/"), "/");
    ok &= ExpectEqual("normalize empty", textlt::NormalizeRemoteDirectory(""), "/");
    ok &= ExpectEqual("normalize slashes", textlt::NormalizeRemoteDirectory("/a//b/./c/"), "/a/b/c");
    ok &= ExpectEqual("normalize parent", textlt::NormalizeRemoteDirectory("/a/b/../c"), "/a/c");
    ok &= ExpectEqual("normalize windows separators", textlt::NormalizeRemoteDirectory("/a\\b\\c"), "/a/b/c");

    ok &= ExpectEqual("join remote path", textlt::JoinRemotePath("/a/b", "c.txt"), "/a/b/c.txt");
    ok &= ExpectEqual("join absolute path", textlt::JoinRemotePath("/a/b", "/x/y.txt"), "/x/y.txt");
    ok &= ExpectEqual("join root", textlt::JoinRemotePath("/", "file.txt"), "/file.txt");

    ok &= ExpectEqual("parent root", textlt::RemoteParentPath("/"), "/");
    ok &= ExpectEqual("parent file", textlt::RemoteParentPath("/a/b/file.txt"), "/a/b");
    ok &= ExpectEqual("parent top", textlt::RemoteParentPath("/a"), "/");

    ok &= ExpectEqual("base root", textlt::RemoteBaseName("/"), "/");
    ok &= ExpectEqual("base file", textlt::RemoteBaseName("/a/b/file.txt"), "file.txt");

    ok &= ExpectEqual(
        "type to string",
        textlt::RemoteConnectionTypeToString(textlt::RemoteConnectionType::MicrosoftDrive),
        "microsoft_drive");
    ok &= ExpectType(
        "type sftp",
        textlt::RemoteConnectionTypeFromString("sftp"),
        textlt::RemoteConnectionType::Sftp);
    ok &= ExpectType(
        "type google alias",
        textlt::RemoteConnectionTypeFromString("google-drive"),
        textlt::RemoteConnectionType::GoogleDrive);
    ok &= ExpectType(
        "type onedrive alias",
        textlt::RemoteConnectionTypeFromString("onedrive"),
        textlt::RemoteConnectionType::MicrosoftDrive);
    ok &= ExpectType(
        "type dropbox",
        textlt::RemoteConnectionTypeFromString("dropbox"),
        textlt::RemoteConnectionType::Dropbox);

    return ok ? 0 : 1;
}
