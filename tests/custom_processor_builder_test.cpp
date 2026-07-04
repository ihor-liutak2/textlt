#include "custom_processor_builder.hpp"
#include "text_parser_manager.hpp"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <sstream>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace {

void SetConfigHome(const fs::path& path) {
#ifdef _WIN32
    _putenv_s("XDG_CONFIG_HOME", path.string().c_str());
#else
    setenv("XDG_CONFIG_HOME", path.string().c_str(), 1);
#endif
}

fs::path PrepareDefaultProcessors(const fs::path& root) {
    const fs::path defaults = root / "defaults";
    fs::create_directories(defaults);
    std::ofstream(defaults / "default_text_parsers.json")
        << "{\n  \"version\": 1,\n  \"parsers\": []\n}\n";
    return defaults;
}


std::string ReadFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

int CountProcessorEntries(const fs::path& config_path, const std::string& id) {
    const auto root = nlohmann::json::parse(ReadFile(config_path));
    int count = 0;
    for (const auto& parser : root.at("parsers")) {
        if (parser.is_object() && parser.value("id", "") == id) {
            ++count;
        }
    }
    return count;
}

void TestPromptMentionsTransformAndSandbox() {
    textlt::CustomProcessorPromptRequest request;
    request.user_request = "Report lines longer than 120 characters and keep dialogue.";
    request.group = "User";
    request.scope = "text";
    request.output = "report";

    const std::string prompt = textlt::BuildCustomProcessorAiPrompt(request);
    assert(prompt.find("transform(text, params)") != std::string::npos);
    assert(prompt.find("No file system") != std::string::npos);
    assert(prompt.find("Return ONLY one raw JSON object") != std::string::npos);
    assert(prompt.find("Do not use Markdown fences") != std::string::npos);
    assert(prompt.find("analysis.recommended_existing_processors") != std::string::npos);
    assert(prompt.find("AVAILABLE BUILT-IN TEXT PROCESSORS") != std::string::npos);
    assert(prompt.find("builtin_analysis_long_lines") != std::string::npos);
    assert(prompt.find("\"params\": []") != std::string::npos);
    assert(prompt.find("\"id\": \"min_chars\"") != std::string::npos);
    assert(prompt.find("params.min_chars") != std::string::npos);
    assert(prompt.find("default \"120\"") != std::string::npos);
    assert(prompt.find("LUA-IN-JSON STRING RULES") != std::string::npos);
    assert(prompt.find("BAD LUA-IN-JSON EXAMPLE") != std::string::npos);
    assert(prompt.find("tonumber(params.width or '100')") != std::string::npos);
    assert(prompt.find("string.char(10)") != std::string::npos);
    assert(prompt.find("Do not write '\\n'") != std::string::npos);
    assert(prompt.find("BAD LUA-IN-JSON EXAMPLE 2") != std::string::npos);
    assert(prompt.find("Do not use byte-range UTF-8 patterns") != std::string::npos);
    assert(prompt.find("\"group\": \"User\"") != std::string::npos);
    assert(prompt.find("\"output\": \"report\"") != std::string::npos);
}

void TestRepairPromptContainsValidationErrorAndBadJson() {
    textlt::CustomProcessorPromptRequest request;
    request.user_request = "Remove page numbers but keep dialogue.";
    request.group = "User";
    request.scope = "text";
    request.output = "replace_text";

    const std::string bad_json = R"json({"id":"user_bad","lua":"return os.getenv('HOME')"})json";
    const std::string repair_prompt = textlt::BuildCustomProcessorRepairPrompt(
        request, bad_json, "Lua script uses forbidden token: os.");

    assert(repair_prompt.find("Return ONLY one corrected raw JSON object") != std::string::npos);
    assert(repair_prompt.find("VALIDATION ERROR FROM TEXTLT") != std::string::npos);
    assert(repair_prompt.find("Lua script uses forbidden token: os.") != std::string::npos);
    assert(repair_prompt.find("INVALID JSON THAT MUST BE FIXED") != std::string::npos);
    assert(repair_prompt.find("infer the intent from INVALID JSON fields") != std::string::npos);
    assert(repair_prompt.find("os.getenv") != std::string::npos);
    assert(repair_prompt.find("Remove page numbers but keep dialogue") != std::string::npos);
    assert(repair_prompt.find("MOST COMMON JSON REPAIR CHECKS") != std::string::npos);
    assert(repair_prompt.find("Replace tonumber(params.x or \"100\") with tonumber(params.x or '100')") != std::string::npos);
    assert(repair_prompt.find("unfinished string near quote") != std::string::npos);
    assert(repair_prompt.find("table.concat(out, string.char(10))") != std::string::npos);
    assert(repair_prompt.find("LUA-IN-JSON STRING RULES") != std::string::npos);
    assert(repair_prompt.find("transform(text, params)") != std::string::npos);
}

void TestInstallAndApplyCustomProcessor() {
    const fs::path root = fs::temp_directory_path() / "textlt_custom_processor_builder_test";
    fs::remove_all(root);
    fs::create_directories(root);
    SetConfigHome(root / "config");
    const fs::path defaults = PrepareDefaultProcessors(root);

    const std::string json = R"json({
  "id": "user_replace_foo",
  "name": "Replace Foo",
  "group": "User",
  "scope": "text",
  "output": "replace_text",
  "description": "Replaces foo with bar.",
  "params": [],
  "lua": "function transform(text, params)\n  return text:gsub('foo', 'bar')\nend\n"
})json";

    const textlt::CustomProcessorInstallResult validation =
        textlt::ValidateCustomProcessorJson(json);
    assert(validation.success);
    assert(validation.id == "user_replace_foo");

    const textlt::CustomProcessorInstallResult install =
        textlt::InstallCustomProcessorFromJson(json, defaults);
    assert(install.success);
    assert(fs::exists(install.config_path));
    assert(fs::exists(install.script_path));

    std::string error;
    textlt::TextParserManager manager;
    assert(manager.LoadFromUserConfiguration(error));
    const textlt::TextParserApplyResult result =
        manager.ApplyParser("user_replace_foo", "foo and foo", {}, 1);
    assert(result.success);
    assert(result.text == "bar and bar");
}

void TestManageEditableCustomProcessors() {
    const fs::path root = fs::temp_directory_path() / "textlt_custom_processor_manage_test";
    fs::remove_all(root);
    fs::create_directories(root);
    SetConfigHome(root / "config");
    const fs::path defaults = PrepareDefaultProcessors(root);

    const std::string json = R"json({
  "id": "user_replace_foo",
  "name": "Replace Foo",
  "group": "User",
  "scope": "text",
  "output": "replace_text",
  "description": "Replaces foo with bar.",
  "params": [],
  "lua": "function transform(text, params)\n  return text:gsub('foo', 'bar')\nend\n"
})json";

    const textlt::CustomProcessorInstallResult install =
        textlt::InstallCustomProcessorFromJson(json, defaults);
    assert(install.success);

    std::string error;
    std::vector<textlt::CustomProcessorSummary> processors =
        textlt::ListEditableCustomProcessors(error);
    assert(error.empty());
    assert(processors.size() == 1);
    assert(processors.front().id == "user_replace_foo");
    assert(processors.front().script_path == install.script_path);

    const textlt::CustomProcessorLoadResult load =
        textlt::LoadCustomProcessorForEditing("user_replace_foo");
    assert(load.success);
    assert(load.json_text.find("\"id\": \"user_replace_foo\"") != std::string::npos);
    assert(load.json_text.find("\"lua\"") != std::string::npos);
    assert(load.json_text.find("text:gsub('foo', 'bar')") != std::string::npos);

    std::string edited_json = load.json_text;
    const std::string old_text = "text:gsub('foo', 'bar')";
    const std::string new_text = "text:gsub('foo', 'baz')";
    const size_t pos = edited_json.find(old_text);
    assert(pos != std::string::npos);
    edited_json.replace(pos, old_text.size(), new_text);

    const textlt::CustomProcessorInstallResult edited =
        textlt::InstallCustomProcessorFromJson(edited_json, defaults);
    assert(edited.success);
    assert(CountProcessorEntries(textlt::TextParserManager::UserConfigFilePath(),
                                 "user_replace_foo") == 1);

    textlt::TextParserManager manager;
    assert(manager.LoadFromUserConfiguration(error));
    const textlt::TextParserApplyResult result =
        manager.ApplyParser("user_replace_foo", "foo", {}, 1);
    assert(result.success);
    assert(result.text == "baz");

    const textlt::CustomProcessorDeleteResult deleted =
        textlt::DeleteCustomProcessor("user_replace_foo");
    assert(deleted.success);
    assert(CountProcessorEntries(textlt::TextParserManager::UserConfigFilePath(),
                                 "user_replace_foo") == 0);
    assert(!fs::exists(install.script_path));
}

void TestRejectLockedAndNonUserProcessorManagement() {
    const fs::path root = fs::temp_directory_path() / "textlt_custom_processor_reject_test";
    fs::remove_all(root);
    fs::create_directories(root);
    SetConfigHome(root / "config");
    const fs::path defaults = PrepareDefaultProcessors(root);

    textlt::TextParserManager manager;
    std::string error;
    assert(manager.EnsureUserConfiguration(defaults, error));

    const fs::path config_path = textlt::TextParserManager::UserConfigFilePath();
    const fs::path script_dir = textlt::TextParserManager::UserConfigDirectory() / "text_parsers";
    fs::create_directories(script_dir);
    const fs::path locked_script = script_dir / "user_locked.lua";
    const fs::path external_script = script_dir / "external.lua";
    std::ofstream(locked_script) << "function transform(text, params)\n  return text\nend\n";
    std::ofstream(external_script) << "function transform(text, params)\n  return text\nend\n";

    nlohmann::json root_json;
    root_json["version"] = 1;
    root_json["parsers"] = nlohmann::json::array();
    root_json["parsers"].push_back({
        {"id", "user_locked"},
        {"name", "Locked"},
        {"scope", "text"},
        {"script", "text_parsers/user_locked.lua"},
        {"locked", true}
    });
    root_json["parsers"].push_back({
        {"id", "external_processor"},
        {"name", "External"},
        {"scope", "text"},
        {"script", "text_parsers/external.lua"}
    });
    std::ofstream(config_path) << root_json.dump(2) << '\n';

    const textlt::CustomProcessorLoadResult locked_load =
        textlt::LoadCustomProcessorForEditing("user_locked");
    assert(!locked_load.success);

    const textlt::CustomProcessorDeleteResult locked_delete =
        textlt::DeleteCustomProcessor("user_locked");
    assert(!locked_delete.success);
    assert(fs::exists(locked_script));

    const textlt::CustomProcessorLoadResult external_load =
        textlt::LoadCustomProcessorForEditing("external_processor");
    assert(!external_load.success);

    const textlt::CustomProcessorDeleteResult external_delete =
        textlt::DeleteCustomProcessor("external_processor");
    assert(!external_delete.success);
    assert(fs::exists(external_script));
}

void TestForbiddenLuaRejected() {
    const std::string json = R"json({
  "id": "user_bad",
  "name": "Bad",
  "group": "User",
  "scope": "text",
  "output": "replace_text",
  "description": "Bad script.",
  "params": [],
  "lua": "function transform(text, params)\n  return os.getenv('HOME')\nend\n"
})json";

    const textlt::CustomProcessorInstallResult validation =
        textlt::ValidateCustomProcessorJson(json);
    assert(!validation.success);
    assert(validation.error.find("forbidden") != std::string::npos);
}

} // namespace

int main() {
    TestPromptMentionsTransformAndSandbox();
    TestRepairPromptContainsValidationErrorAndBadJson();
    TestInstallAndApplyCustomProcessor();
    TestManageEditableCustomProcessors();
    TestRejectLockedAndNonUserProcessorManagement();
    TestForbiddenLuaRejected();
    return 0;
}
