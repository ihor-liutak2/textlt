#include "custom_processor_builder.hpp"
#include "text_parser_manager.hpp"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>

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
    TestForbiddenLuaRejected();
    return 0;
}
