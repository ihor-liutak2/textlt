#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifndef TEXTLT_SOURCE_DIR
#define TEXTLT_SOURCE_DIR "."
#endif

namespace {

bool HasSuffix(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
        value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool IsScannedFile(const std::filesystem::path& path) {
    const std::string name = path.filename().string();
    const std::string ext = path.extension().string();
    if (ext == ".cpp" || ext == ".hpp" || ext == ".h" || ext == ".c" || ext == ".cmake" || ext == ".yml" || ext == ".yaml") {
        return true;
    }
    return name == "CMakeLists.txt";
}

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream out;
    out << file.rdbuf();
    return out.str();
}

bool ShouldSkipDirectory(const std::filesystem::path& path) {
    const std::string name = path.filename().string();
    return name == ".git" ||
        name == "build" ||
        name == "cmake-build-debug" ||
        name == "cmake-build-release" ||
        name == "_deps" ||
        name == "package" ||
        name == "AppDir";
}

bool Contains(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

void RequireContains(const std::filesystem::path& root,
                     const std::filesystem::path& relative,
                     const std::string& needle,
                     bool& ok) {
    const std::filesystem::path path = root / relative;
    if (!std::filesystem::exists(path)) {
        std::cerr << "Missing file: " << relative.string() << "\n";
        ok = false;
        return;
    }
    const std::string content = ReadFile(path);
    if (!Contains(content, needle)) {
        std::cerr << "Expected " << relative.string() << " to contain: " << needle << "\n";
        ok = false;
    }
}


} // namespace

int main() {
    const std::filesystem::path root = TEXTLT_SOURCE_DIR;
    const std::vector<std::string> banned = {
        std::string("<") + "curl/" + "curl.h>",
        std::string("#include <") + "curl/",
        std::string("curl_") + "easy_" + "init",
        std::string("curl_") + "easy_" + "setopt",
        std::string("curl_") + "easy_" + "perform",
        std::string("CURL::") + "lib" + "curl",
        std::string("find_package(") + "CURL",
        std::string("lib") + "curl4-openssl-dev",
        std::string("vcpkg install ") + "curl",
    };

    bool ok = true;
    std::error_code error;
    std::filesystem::recursive_directory_iterator iter(
        root,
        std::filesystem::directory_options::skip_permission_denied,
        error);
    const std::filesystem::recursive_directory_iterator end;

    for (; iter != end; iter.increment(error)) {
        if (error) {
            error.clear();
            continue;
        }

        const std::filesystem::path path = iter->path();
        if (path.filename() == "no_embedded_curl_test.cpp") {
            continue;
        }
        if (iter->is_directory(error)) {
            if (ShouldSkipDirectory(path)) {
                iter.disable_recursion_pending();
            }
            continue;
        }
        if (!iter->is_regular_file(error) || !IsScannedFile(path)) {
            continue;
        }

        const std::string text = ReadFile(path);
        for (const std::string& token : banned) {
            if (text.find(token) != std::string::npos) {
                std::cerr << "Forbidden embedded curl-library token found in "
                          << path.string() << ": " << token << "\n";
                ok = false;
            }
        }
    }

    RequireContains(root, "scripts/textlt-install-runtime-deps.sh", "curl openssh-client", ok);
    RequireContains(root, "scripts/textlt-install-runtime-deps.sh", "openssh-clients", ok);
    RequireContains(root, "scripts/textlt-install-runtime-deps.sh", "curl ssh sftp", ok);
    RequireContains(root, "scripts/textlt-launcher.sh", "for tool in curl ssh sftp", ok);
    RequireContains(root, "scripts/textlt-install-runtime-deps.ps1", "ssh.exe", ok);
    RequireContains(root, "scripts/textlt-install-runtime-deps.ps1", "sftp.exe", ok);
    RequireContains(root, "scripts/textlt-install-runtime-deps.ps1", "curl.exe", ok);
    RequireContains(root, ".github/workflows/release.yml", "openssh-client", ok);
    RequireContains(root, ".github/workflows/release.yml", "ssh -V", ok);
    RequireContains(root, ".github/workflows/release.yml", "sftp", ok);
    RequireContains(root, "src/remote/remote_http_client.cpp", "speed-time", ok);
    RequireContains(root, "src/remote/remote_http_client.cpp", "kProgressWindowSeconds = 20", ok);
    RequireContains(root, "src/remote/remote_http_client.cpp", "speed-limit", ok);
    RequireContains(root, "src/remote/remote_http_client.cpp", "kIdleSpeedLimitBytesPerSecond = 1", ok);
    RequireContains(root, "src/remote/remote_http_client.cpp", "connect-timeout", ok);
    RequireContains(root, "src/remote/remote_http_client.cpp", "kConnectTimeoutSeconds = 20", ok);

    return ok ? 0 : 1;
}
