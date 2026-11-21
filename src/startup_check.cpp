#include "startup_check.hpp"

#include <archive.h>
#include <pugixml.hpp>

#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <system_error>

#ifndef _WIN32
#include <unistd.h>
#endif

#include "actions/action_repository.hpp"
#include "app_resources.hpp"
#include "cloud_tts_pipeline.hpp"
#include "editor_config.hpp"
#include "notes/note_repository.hpp"
#include "path_utils.hpp"
#include "piper_manager.hpp"
#include "project_store.hpp"
#include "remote/remote_command_runner.hpp"
#include "remote/remote_config_store.hpp"
#include "remote/remote_oauth_token_store.hpp"
#include "text_parser_manager.hpp"

namespace textlt {
namespace {
namespace fs = std::filesystem;

class Report {
public:
    void Check(bool ok, const std::string& label, const std::string& detail = {}) {
        std::cout << (ok ? "[OK] " : "[FAIL] ") << label;
        if (!detail.empty()) std::cout << " — " << detail;
        std::cout << std::endl;
        passed &= ok;
    }
    bool passed = true;
};

void CheckTool(Report& report, const std::vector<std::string>& args, bool usage = false) {
    const auto result = RemoteCommandRunner{}.RunStreaming(args, {}, nullptr, {}, {5});
    const auto output = result.output.empty() ? result.error : result.output;
    // OpenSSH sftp prints its usage to stderr and exits with 1 for -h.
    const bool ok = result.exit_code == 0 ||
        (usage && result.exit_code == 1 && output.find("usage: sftp") != std::string::npos);
    report.Check(ok, args.front(), "exit " + std::to_string(result.exit_code) + " " +
        output.substr(0, output.find_first_of("\r\n")));
}

void CheckDirectory(Report& report, const fs::path& directory) {
    const std::string label = "Write, read, rename and delete in " + Utf8FromPath(directory);
    if (!directory.is_absolute()) {
        report.Check(false, label, "cannot resolve the current user's directory");
        return;
    }
    std::error_code ec;
    fs::create_directories(directory, ec);
    if (ec) {
        report.Check(false, label, ec.message());
        return;
    }
    const auto name = ".textlt-check-" + actions::GenerateActionId();
    const fs::path source = directory / name;
    const fs::path target = directory / (name + ".tmp");
    // Reserve both names exclusively. Only remove files created by this run.
    std::string problem;
    auto create = [&](const fs::path& path, const char* contents) {
#ifdef _WIN32
        FILE* file = nullptr;
        const auto error = _wfopen_s(&file, path.c_str(), L"wbx");
#else
        FILE* file = std::fopen(path.c_str(), "wbx");
        const auto error = file ? 0 : errno;
#endif
        if (!file) {
            problem = "create: " + std::error_code(error, std::generic_category()).message();
            return false;
        }
        const bool wrote = std::fputs(contents, file) >= 0;
        const bool closed = std::fclose(file) == 0;
        if (!wrote || !closed) problem = "writing or closing the probe failed";
        return true;
    };
    bool source_exists = create(source, "new");
    const bool target_exists = source_exists && problem.empty() && create(target, "old");
    if (target_exists && problem.empty()) {
#ifdef _WIN32
        if (!MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            ec = std::error_code(GetLastError(), std::system_category());
        }
#else
        fs::rename(source, target, ec);
#endif
        if (ec) {
            problem = "replace: " + ec.message();
        } else {
            source_exists = false;
            std::ifstream input(target, std::ios::binary);
            std::string contents;
            if (!std::getline(input, contents) || contents != "new") problem = "read-back failed";
        }
    }
    auto cleanup = [&](const fs::path& path) {
        fs::remove(path, ec);
        if (ec) problem += " delete " + Utf8FromPath(path) + ": " + ec.message();
    };
    if (source_exists) cleanup(source);
    if (target_exists) cleanup(target);
    report.Check(problem.empty(), label, problem);
}

void CheckProcessors(Report& report) {
    const auto root = FindTextProcessorsDirectory();
    if (root.empty()) {
        report.Check(false, "Processor resources", "cannot locate default_text_parsers.json");
        return;
    }
    TextParserManager processors;
    std::string error;
    const bool loaded = processors.LoadFromFile(root / "default_text_parsers.json", error);
    report.Check(loaded, "Processor manifest " + Utf8FromPath(root), error);
    if (!loaded) return;
    for (const auto& processor : processors.GetParsers()) {
        if (processor.engine != TextParserEngine::Lua) continue;
        const auto script = root / processor.script_path;
        std::ifstream input(script, std::ios::binary);
        report.Check(input && input.peek() != std::ifstream::traits_type::eof(),
                     "Read processor script " + Utf8FromPath(script));
    }
}

} // namespace

bool CheckStartupEnvironment() {
    Report report;
#ifdef _WIN32
    HANDLE token;
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size)) {
            std::cout << "[INFO] Running " << (elevation.TokenIsElevated ? "elevated" : "without elevation") << '\n';
        }
        CloseHandle(token);
    }
#else
    std::cout << "[INFO] Effective user ID " << geteuid() << '\n';
#endif
    CheckTool(report, {"curl", "--version"});
    CheckTool(report, {"ssh", "-V"});
    CheckTool(report, {"sftp", "-h"}, true);
#ifdef _WIN32
    CheckTool(report, {"powershell.exe", "-NoProfile", "-NonInteractive", "-Command",
                       "$PSVersionTable.PSVersion.ToString()"});
#endif
    report.Check(archive_version_number() > 0, "Archive runtime", archive_version_string());
    pugi::xml_document xml;
    report.Check(xml.load_string("<textlt/>"), "Embedded XML parser");
    const auto lua = TextParserLuaEngine{}.RunScript("function transform(s) return s end", "test", {});
    report.Check(lua.success && lua.text == "test", "Embedded Lua processor", lua.error);
    CheckProcessors(report);

    const auto config = EditorConfig::DefaultConfigPath();
    const auto parsers = TextParserManager::UserConfigDirectory();
    const auto notes_root = notes::NoteRepository::DefaultRoot();
    const auto actions_file = actions::ActionRepository::DefaultPath();
    const ProjectStore project_store;
    const auto projects = project_store.Path();
    report.Check(project_store.IsUsable(), "Project registry " + Utf8FromPath(projects), project_store.LoadError());
    const auto connections = RemoteConfigStore{}.Path();
    std::error_code ec;
    const auto temp = fs::temp_directory_path(ec);
    const std::set<fs::path> directories = {
        temp, config.parent_path(), config.parent_path() / "themes", config.parent_path() / "registries",
        parsers, parsers / "text_parsers",
        projects.parent_path(), connections.parent_path(), DefaultRemoteTokenDirectory(),
        actions_file.parent_path(), notes_root, notes_root / "items", CloudTtsPipeline::LibraryDirectory(),
        CloudTtsPipeline::RegistryDirectory(), PiperManager::RuntimeDirectory(),
        PiperManager::ModelsDirectory(), PiperManager::RuntimeDownloadArchivePath().parent_path(),
    };
    for (const auto& directory : directories) CheckDirectory(report, directory);

    // An administrator can leave a protected file inside a writable directory.
    // Open existing stores without truncating or changing their contents.
    for (const auto& file : {config, projects, connections, actions_file, notes_root / "sections.json",
                             TextParserManager::UserConfigFilePath(), parsers / "quick_text_commands.json"}) {
        const bool exists = fs::exists(file, ec);
        if (ec) report.Check(false, Utf8FromPath(file), ec.message());
        else if (exists) {
            const auto label = "Read/write existing " + Utf8FromPath(file);
#ifdef _WIN32
            // Atomic saves need replacement access as well as permission to write.
            HANDLE handle = CreateFileW(file.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (handle == INVALID_HANDLE_VALUE) {
                report.Check(false, label, std::system_category().message(GetLastError()));
            } else {
                CloseHandle(handle);
                report.Check(true, label);
            }
#else
            std::fstream stream(file, std::ios::in | std::ios::out | std::ios::binary);
            report.Check(stream.good() && fs::is_regular_file(file, ec), label);
#endif
        }
    }
    std::cout << "[INFO] Optional Piper runtime: "
              << (PiperManager::RuntimeExecutablePath().empty() ? "not installed" : "installed") << '\n';
    return report.passed;
}

} // namespace textlt
