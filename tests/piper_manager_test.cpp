#include "piper_manager.hpp"

#include <cassert>
#include <filesystem>
#include <string>
#include <vector>

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

void TestAssetPathFromUrl() {
    const std::filesystem::path path = textlt::PiperManager::AssetPathFromUrl(
        "https://example.com/assets/a/b/file.onnx");
    assert(path == std::filesystem::path("assets/a/b/file.onnx"));
}

void TestVoicePathsUseRegistryIdDirectory() {
    textlt::Json voice = textlt::Json::object();
    voice["id"] = "en_US-lessac-medium";
    voice["model_url"] =
        "https://huggingface.co/rhasspy/piper-voices/resolve/main/"
        "en/en_US/lessac/medium/en_US-lessac-medium.onnx";
    voice["config_url"] =
        "https://huggingface.co/rhasspy/piper-voices/resolve/main/"
        "en/en_US/lessac/medium/en_US-lessac-medium.onnx.json";

    const std::filesystem::path voice_directory = textlt::PiperManager::VoiceDirectory(voice);
    const std::filesystem::path model_path = textlt::PiperManager::VoiceModelPath(voice);
    const std::filesystem::path config_path = textlt::PiperManager::VoiceConfigPath(voice);
    if (!voice_directory.empty()) {
        assert(voice_directory.filename() == "en_US-lessac-medium");
    }
    if (!model_path.empty()) {
        assert(model_path.parent_path().filename() == "en_US-lessac-medium");
        assert(model_path.filename() == "en_US-lessac-medium.onnx");
    }
    if (!config_path.empty()) {
        assert(config_path.parent_path().filename() == "en_US-lessac-medium");
        assert(config_path.filename() == "en_US-lessac-medium.onnx.json");
    }

    const std::vector<std::filesystem::path> candidates =
        textlt::PiperManager::VoiceModelPathCandidates(voice);
    assert(!candidates.empty());
    assert(candidates.front() == model_path);
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
    TestAssetPathFromUrl();
    TestVoicePathsUseRegistryIdDirectory();
    TestRuntimeUrlAndArchiveNameMatchPlatform();
    return 0;
}
