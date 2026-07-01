#include "piper_manager.hpp"

#include <cassert>
#include <filesystem>
#include <string>

namespace {

void TestShellQuoting() {
    const std::string quoted = textlt::PiperManager::QuoteShellPath(
        std::filesystem::path("folder name") / "voice's file.onnx");
    assert(!quoted.empty());
#ifndef _WIN32
    assert(quoted.front() == '\'' && quoted.back() == '\'');
    assert(quoted.find("'\\''") != std::string::npos);
#else
    assert(quoted.front() == '"' && quoted.back() == '"');
#endif
}

void TestRuntimePathsAreConsistent() {
    const std::filesystem::path runtime = textlt::PiperManager::RuntimeDirectory();
    const std::filesystem::path models = textlt::PiperManager::ModelsDirectory();
    if (!runtime.empty()) {
        assert(runtime.filename() == "bin");
    }
    if (!models.empty()) {
        assert(models.filename() == "models");
    }
}

void TestRuntimeUrlAndArchiveNameMatchPlatform() {
    const std::filesystem::path archive = textlt::PiperManager::RuntimeDownloadArchivePath();
    if (!archive.empty()) {
#ifdef _WIN32
        assert(archive.filename() == "piper_windows_amd64.zip");
#else
        assert(archive.filename() == "piper_linux_x86_64.tar.gz");
#endif
    }

#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
    assert(textlt::PiperManager::RuntimeDownloadUrl().find("piper_windows_amd64.zip") != std::string::npos);
#elif !defined(_WIN32) && defined(__x86_64__)
    assert(textlt::PiperManager::RuntimeDownloadUrl().find("piper_linux_x86_64.tar.gz") != std::string::npos);
#endif
}

} // namespace

int main() {
    TestShellQuoting();
    TestRuntimePathsAreConsistent();
    TestRuntimeUrlAndArchiveNameMatchPlatform();
    return 0;
}
