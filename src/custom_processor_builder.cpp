#include "custom_processor_builder.hpp"

#include "builtin_text_processors.hpp"
#include "text_parser_lua_engine.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace textlt {
namespace fs = std::filesystem;
namespace {

using Json = nlohmann::ordered_json;

std::string TrimCopy(const std::string& value) {
    size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }
    size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    return value.substr(begin, end - begin);
}

std::string ToLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string ExtractJsonPayload(const std::string& text) {
    const std::string fence = "```";
    const size_t first_fence = text.find(fence);
    if (first_fence != std::string::npos) {
        size_t content_start = text.find('\n', first_fence + fence.size());
        if (content_start != std::string::npos) {
            ++content_start;
            const size_t end_fence = text.find(fence, content_start);
            if (end_fence != std::string::npos && end_fence > content_start) {
                return TrimCopy(text.substr(content_start, end_fence - content_start));
            }
        }
    }

    const size_t first_brace = text.find('{');
    const size_t last_brace = text.rfind('}');
    if (first_brace != std::string::npos && last_brace != std::string::npos &&
        last_brace > first_brace) {
        return text.substr(first_brace, last_brace - first_brace + 1);
    }
    return TrimCopy(text);
}

std::string ToSnakeId(std::string value) {
    std::string result;
    bool previous_underscore = false;
    for (unsigned char ch : value) {
        if (std::isalnum(ch) != 0) {
            result.push_back(static_cast<char>(std::tolower(ch)));
            previous_underscore = false;
            continue;
        }
        if (!previous_underscore && !result.empty()) {
            result.push_back('_');
            previous_underscore = true;
        }
    }
    while (!result.empty() && result.back() == '_') {
        result.pop_back();
    }
    if (result.empty()) {
        result = "custom_processor";
    }
    if (result.rfind("user_", 0) != 0) {
        result = "user_" + result;
    }
    return result;
}

bool IsSafeId(const std::string& id) {
    if (id.empty() || id.size() > 80) {
        return false;
    }
    for (unsigned char ch : id) {
        if (std::isalnum(ch) != 0 || ch == '_' || ch == '-') {
            continue;
        }
        return false;
    }
    return id.find("..") == std::string::npos && id.find('/') == std::string::npos &&
           id.find('\\') == std::string::npos;
}

std::string JsonString(const Json& object, const char* key, const std::string& fallback = "") {
    if (!object.contains(key) || !object.at(key).is_string()) {
        return fallback;
    }
    return object.at(key).get<std::string>();
}


std::string LuaScriptFromJson(const Json& object) {
    std::string lua = JsonString(object, "lua");
    if (!lua.empty()) {
        return lua;
    }
    lua = JsonString(object, "lua_script");
    if (!lua.empty()) {
        return lua;
    }
    lua = JsonString(object, "script_text");
    if (!lua.empty()) {
        return lua;
    }
    return JsonString(object, "code");
}

bool JsonBool(const Json& object, const char* key, bool fallback = false) {
    if (!object.contains(key)) {
        return fallback;
    }
    if (object.at(key).is_boolean()) {
        return object.at(key).get<bool>();
    }
    return fallback;
}

int JsonInt(const Json& object, const char* key, int fallback = 1) {
    if (!object.contains(key)) {
        return fallback;
    }
    if (object.at(key).is_number_integer()) {
        return object.at(key).get<int>();
    }
    if (object.at(key).is_string()) {
        try {
            return std::stoi(object.at(key).get<std::string>());
        } catch (...) {
            return fallback;
        }
    }
    return fallback;
}

std::string NormalizeScopeValue(std::string value) {
    value = ToLowerCopy(TrimCopy(value));
    if (value == "paragraph") {
        return "paragraph";
    }
    if (value == "code") {
        return "code";
    }
    return "text";
}

std::string NormalizeOutputValue(std::string value) {
    value = ToLowerCopy(TrimCopy(value));
    if (value == "report" || value == "analysis") {
        return "report";
    }
    return "replace_text";
}

std::string ScopeToPromptValue(TextParserScope scope) {
    switch (scope) {
        case TextParserScope::Paragraph:
            return "paragraph";
        case TextParserScope::Code:
            return "code";
        case TextParserScope::Text:
        default:
            return "text";
    }
}

std::string OutputToPromptValue(TextParserOutput output) {
    return output == TextParserOutput::Report ? "report" : "replace_text";
}

std::string BuildBuiltinProcessorReference() {
    std::ostringstream out;
    out << "AVAILABLE BUILT-IN TEXT PROCESSORS. Analyze these first and recommend them "
        << "when they already solve the user's request faster than a custom Lua processor.\n";

    const std::vector<TextParserDefinition> processors = CreateBuiltinTextProcessors();
    std::string current_group;
    for (const TextParserDefinition& processor : processors) {
        if (processor.group != current_group) {
            current_group = processor.group;
            out << "\n[" << current_group << "]\n";
        }
        out << "- id: " << processor.id
            << "; name: " << processor.name
            << "; scope: " << ScopeToPromptValue(processor.scope)
            << "; output: " << OutputToPromptValue(processor.output)
            << "; description: " << processor.description;
        if (!processor.params.empty()) {
            out << "; params: ";
            for (size_t i = 0; i < processor.params.size(); ++i) {
                const TextParserParam& param = processor.params[i];
                if (i > 0) {
                    out << ", ";
                }
                out << param.id << "(" << param.type << ", default="
                    << param.default_value << ")";
            }
        }
        out << "\n";
    }
    return out.str();
}

bool IsAllowedParamType(const std::string& type) {
    const std::string lower = ToLowerCopy(type);
    return lower == "text" || lower == "integer" || lower == "decimal" || lower == "boolean";
}

bool ContainsForbiddenLuaToken(const std::string& lua, std::string& token) {
    const std::vector<std::string> forbidden = {
        "io.", "os.", "package", "require", "dofile", "loadfile",
        "load(", "debug", "collectgarbage", "coroutine"
    };
    const std::string lower = ToLowerCopy(lua);
    for (const std::string& item : forbidden) {
        if (lower.find(item) != std::string::npos) {
            token = item;
            return true;
        }
    }
    return false;
}

bool ValidateLuaScript(const std::string& lua, std::string& error) {
    if (TrimCopy(lua).empty()) {
        error = "Lua script is empty.";
        return false;
    }
    if (lua.find("transform") == std::string::npos) {
        error = "Lua script must define function transform(text, params).";
        return false;
    }
    std::string token;
    if (ContainsForbiddenLuaToken(lua, token)) {
        error = "Lua script uses forbidden token: " + token;
        return false;
    }

    TextParserLuaEngine engine;
    LuaParserLimits limits;
    limits.timeout_ms = 500;
    limits.instruction_hook_count = 5000;
    limits.max_memory_bytes = 4U * 1024U * 1024U;
    limits.max_output_bytes = 1024U * 1024U;
    const LuaParserRunResult test = engine.RunScript(lua, "Sample text\nSecond line", {}, limits);
    if (!test.success) {
        error = "Lua validation failed: " + test.error;
        return false;
    }
    return true;
}

std::string ReadTextFile(const fs::path& path, std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        error = "Cannot open file: " + path.string();
        return {};
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    if (input.bad()) {
        error = "Cannot read file: " + path.string();
        return {};
    }
    return buffer.str();
}

bool WriteTextFile(const fs::path& path, const std::string& text, std::string& error) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) {
        error = "Cannot create directory: " + path.parent_path().string();
        return false;
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        error = "Cannot write file: " + path.string();
        return false;
    }
    output << text;
    if (!output.good()) {
        error = "Cannot save file: " + path.string();
        return false;
    }
    return true;
}

Json NormalizeParamJson(const Json& param, std::string& error) {
    Json result = Json::object();
    if (!param.is_object()) {
        error = "Each param must be an object.";
        return result;
    }

    const std::string id = ToSnakeId(JsonString(param, "id"));
    if (!IsSafeId(id)) {
        error = "Invalid param id.";
        return result;
    }

    const std::string type = ToLowerCopy(JsonString(param, "type", "text"));
    if (!IsAllowedParamType(type)) {
        error = "Invalid param type for " + id + ": " + type;
        return result;
    }

    result["id"] = id;
    result["label"] = JsonString(param, "label", id);
    result["type"] = type;
    result["default"] = JsonString(param, "default", "");
    result["description"] = JsonString(param, "description", "");
    if (type == "decimal") {
        result["decimal_separator"] = JsonString(param, "decimal_separator", ".");
    }
    return result;
}

std::string BuildLuaJsonEscapingRules() {
    return R"TEXTLT_PROMPT(LUA-IN-JSON STRING RULES:
- The lua field is a JSON string that contains Lua source code, not a raw Lua file.
- Every double quote used inside Lua code must be escaped as \" in JSON.
- Prefer single quotes in Lua string literals: tonumber(params.width or '100') instead of tonumber(params.width or "100").
- Do not put raw control characters into JSON strings. Do not put literal newlines inside Lua string literals.
- Lua source line breaks between statements must be encoded in JSON as \n. This is only for separating Lua statements.
- Do not write '\n', "\n", '\\n', or "\\n" as a Lua string literal inside the JSON lua field. JSON may decode it into a real line break inside Lua quotes and cause: unfinished string near quote.
- If Lua code needs a newline character value, use string.char(10). For joining lines, always write: table.concat(out, string.char(10)).
- Do not use byte-range UTF-8 patterns such as '[\1-\127...]' in JSON strings. Use pcall(utf8.len, text) or simple text processing instead.
- Backslashes must be JSON-safe. Lua patterns like %S+ are fine; do not add raw ASCII control escapes.
- Before returning the answer, mentally parse the whole result as JSON and then mentally compile the decoded Lua. If the lua field contains unescaped " characters or Lua quoted strings split across lines, fix them.

BAD LUA-IN-JSON EXAMPLE 1:
{"lua": "function transform(text, params)\n  local n = tonumber(params.width or "100") or 100\n  return text\nend\n"}
The example above is invalid JSON because the Lua double quotes around 100 break the JSON string.

GOOD LUA-IN-JSON EXAMPLE 1:
{"lua": "function transform(text, params)\n  local n = tonumber(params.width or '100') or 100\n  return text\nend\n"}

BAD LUA-IN-JSON EXAMPLE 2:
{"lua": "function transform(text, params)\n  return table.concat(lines, '\n')\nend\n"}
The example above can decode to Lua code with an actual line break inside single quotes and fail with unfinished string near quote.

GOOD LUA-IN-JSON EXAMPLE 2:
{"lua": "function transform(text, params)\n  return table.concat(lines, string.char(10))\nend\n"}

)TEXTLT_PROMPT";
}
std::string BuildJsonRepairAdvice() {
    return std::string()
        + "MOST COMMON JSON REPAIR CHECKS:\n"
        + "- If JSON parsing failed near the lua field, first look for unescaped double quotes inside Lua string literals.\n"
        + "- Replace Lua double-quoted strings with single-quoted strings where possible.\n"
        + "- Replace tonumber(params.x or \"100\") with tonumber(params.x or '100').\n"
        + "- Replace params.flag == \"true\" with params.flag == 'true'.\n"
        + "- If Lua validation says unfinished string near quote, look for '\\n', \"\\n\", '\\\\n', or \"\\\\n\" inside Lua string literals. Use string.char(10) instead.\n"
        + "- Replace table.concat(out, '\\n') or table.concat(out, \"\\n\") with table.concat(out, string.char(10)).\n"
        + "- Remove raw byte/control-character patterns and use pcall(utf8.len, value) when you need text length.\n\n";
}

bool ReplaceOrAppendParser(Json& root, const Json& parser_json, const std::string& id, std::string& error) {
    if (!root.contains("parsers") || !root.at("parsers").is_array()) {
        error = "Text parser config must contain parsers array.";
        return false;
    }

    for (auto& existing : root["parsers"]) {
        if (!existing.is_object() || !existing.contains("id") || !existing.at("id").is_string()) {
            continue;
        }
        if (existing.at("id").get<std::string>() == id) {
            const std::string engine = ToLowerCopy(JsonString(existing, "engine", "lua"));
            if (engine == "builtin" || JsonBool(existing, "locked", false)) {
                error = "Cannot replace locked/built-in processor: " + id;
                return false;
            }
            existing = parser_json;
            return true;
        }
    }

    root["parsers"].push_back(parser_json);
    return true;
}


bool IsEditableUserProcessor(const Json& parser_json, std::string& id, std::string& error) {
    error.clear();
    id = JsonString(parser_json, "id");
    if (id.rfind("user_", 0) != 0) {
        return false;
    }
    if (!IsSafeId(id)) {
        error = "Invalid processor id in config: " + id;
        return false;
    }
    const std::string engine = ToLowerCopy(JsonString(parser_json, "engine", "lua"));
    if (engine == "builtin" || JsonBool(parser_json, "locked", false)) {
        return false;
    }
    return true;
}

fs::path ResolveUserScriptPath(const Json& parser_json) {
    const fs::path script = fs::path(JsonString(parser_json, "script"));
    if (script.empty()) {
        return {};
    }
    if (script.is_absolute()) {
        return script;
    }
    return TextParserManager::UserConfigDirectory() / script;
}

bool ReadUserProcessorRoot(Json& root, std::string& error) {
    const fs::path config_path = TextParserManager::UserConfigFilePath();
    const std::string config_text = ReadTextFile(config_path, error);
    if (!error.empty()) {
        return false;
    }
    try {
        root = Json::parse(config_text);
    } catch (const std::exception& ex) {
        error = std::string("Cannot parse user text parser config: ") + ex.what();
        return false;
    }
    if (!root.contains("parsers") || !root.at("parsers").is_array()) {
        error = "Text parser config must contain parsers array.";
        return false;
    }
    return true;
}

const Json* FindEditableProcessorJson(
    const Json& root,
    const std::string& processor_id,
    std::string& error) {
    if (!IsSafeId(processor_id)) {
        error = "Invalid processor id: " + processor_id;
        return nullptr;
    }
    for (const auto& parser_json : root.at("parsers")) {
        if (!parser_json.is_object()) {
            continue;
        }
        const std::string id = JsonString(parser_json, "id");
        if (id != processor_id) {
            continue;
        }
        std::string editable_id;
        std::string editable_error;
        if (!IsEditableUserProcessor(parser_json, editable_id, editable_error)) {
            error = editable_error.empty()
                ? "Processor is not editable: " + processor_id
                : editable_error;
            return nullptr;
        }
        return &parser_json;
    }
    error = "Processor not found: " + processor_id;
    return nullptr;
}

Json BuildProcessorEditJson(const Json& parser_json, const std::string& lua) {
    Json editable = Json::object();
    editable["id"] = JsonString(parser_json, "id");
    editable["name"] = JsonString(parser_json, "name");
    editable["group"] = JsonString(parser_json, "group", "User");
    editable["scope"] = NormalizeScopeValue(JsonString(parser_json, "scope", "text"));
    editable["output"] = NormalizeOutputValue(JsonString(parser_json, "output", "replace_text"));
    editable["description"] = JsonString(parser_json, "description", "");
    editable["repeat_default"] = JsonInt(parser_json, "repeat_default", 1);
    editable["params"] = parser_json.contains("params") && parser_json.at("params").is_array()
        ? parser_json.at("params")
        : Json::array();
    editable["pinned"] = JsonBool(parser_json, "pinned", false);
    editable["lua"] = lua;
    return editable;
}

} // namespace

std::vector<std::string> CustomProcessorGroupChoices() {
    return {
        "User",
        "Cleanup",
        "Lines",
        "Paragraphs",
        "Case",
        "Code",
        "Punctuation",
        "Tables",
        "Markdown",
        "Analysis",
        "Custom",
    };
}

std::vector<std::string> CustomProcessorScopeChoices() {
    return {"text", "paragraph", "code"};
}

std::vector<std::string> CustomProcessorOutputChoices() {
    return {"replace_text", "report"};
}

std::string BuildCustomProcessorAiPrompt(const CustomProcessorPromptRequest& request) {
    const std::string group = TrimCopy(request.group).empty() ? "User" : TrimCopy(request.group);
    const std::string scope = NormalizeScopeValue(request.scope);
    const std::string output = NormalizeOutputValue(request.output);

    std::ostringstream prompt;
    prompt
        << "You are an expert Lua 5.4 developer for TextLT custom text processors.\n"
        << "The user does not write code. Convert the user's plain-language request into one safe TextLT processor JSON object.\n\n"
        << "ABSOLUTE OUTPUT RULES:\n"
        << "- Return ONLY one raw JSON object.\n"
        << "- Do not write explanations before or after the JSON.\n"
        << "- Do not use Markdown fences. Do not write ```json.\n"
        << "- The JSON must be ready to copy and paste into TextLT Custom Processor Builder.\n"
        << "- Put any analysis for the human inside the JSON field named \"analysis\".\n\n"
        << BuildLuaJsonEscapingRules()
        << "USER REQUEST:\n"
        << TrimCopy(request.user_request) << "\n\n"
        << "USER-SELECTED METADATA. Preserve these values unless the request clearly contradicts them:\n"
        << "group: " << group << "\n"
        << "scope: " << scope << "\n"
        << "output: " << output << "\n\n"
        << BuildBuiltinProcessorReference() << "\n"
        << "FIRST ANALYSIS REQUIREMENT:\n"
        << "- Before designing Lua, compare the request with the built-in processor list above.\n"
        << "- If built-ins can solve all or part of the request, list them in analysis.recommended_existing_processors.\n"
        << "- If a custom Lua processor is still useful, set analysis.custom_processor_needed to true.\n"
        << "- If a custom Lua processor is not useful, still return a valid JSON object, but set analysis.custom_processor_needed to false and make the Lua transform return the input unchanged.\n"
        << "- Do not invent built-in IDs. Use only IDs from the built-in list above.\n\n"
        << "TEXTLT LUA API:\n"
        << "- Define exactly one function: transform(text, params).\n"
        << "- transform receives UTF-8 text and a params table.\n"
        << "- transform must return a string. For output=report, return the report text.\n"
        << "- Params always arrive in Lua as strings. Convert integer/decimal params with tonumber.\n"
        << "- Boolean params arrive as strings, usually \"true\" or \"false\". Check them with: (params.flag_id or 'false') == 'true'.\n"
        << "- Allowed standard libraries: string, table, math, utf8.\n"
        << "- Allowed global functions: assert, error, ipairs, next, pairs, pcall, select, tonumber, tostring, type.\n"
        << "- No file system, shell, process, network, clipboard, environment, package loading, require, io, os, debug, coroutine, dofile, loadfile, load, or collectgarbage.\n"
        << "- Keep scripts deterministic and fast. No infinite loops.\n"
        << "- Do not modify files. Only transform the provided text.\n\n"
        << "SCOPE RULES:\n"
        << "- scope=text: transform receives the whole selected text or whole document. Use this for multi-line cleanup, tables, markdown blocks, and reports.\n"
        << "- scope=paragraph: TextLT calls transform separately for each non-empty paragraph. Use this only when each paragraph can be processed independently.\n"
        << "- scope=code: transform receives the selected code/text as one block. Use this for code-like formatting.\n\n"
        << "PARAMETER DESIGN RULES:\n"
        << "- Analyze the user's request and extract possible user-adjustable parameters.\n"
        << "- If the user mentions concrete values, write those values as param defaults. Example: 'longer than 120 characters' -> default \"120\".\n"
        << "- Use at most 4 params. If more are possible, keep only the most important ones.\n"
        << "- Param IDs must be safe snake_case ASCII, for example: min_chars, keep_dialogue, replacement_text.\n"
        << "- Do not invent arbitrary field names inside params. Each param object must use only these fields: id, label, type, default, description.\n"
        << "- Allowed param types are exactly: text, integer, decimal, boolean.\n"
        << "- The default value must always be a JSON string, even for integer, decimal, and boolean params.\n"
        << "- Boolean defaults must be \"true\" or \"false\".\n"
        << "- Decimal defaults must use dot as decimal separator.\n\n"
        << "PARAMETER JSON EXAMPLES:\n"
        << "[\n"
        << "  {\"id\": \"min_chars\", \"label\": \"Min characters\", \"type\": \"integer\", \"default\": \"120\", \"description\": \"Minimum line length to report.\"},\n"
        << "  {\"id\": \"keep_dialogue\", \"label\": \"Keep dialogue\", \"type\": \"boolean\", \"default\": \"true\", \"description\": \"Keep dialogue lines separated when joining wrapped text.\"},\n"
        << "  {\"id\": \"replacement\", \"label\": \"Replacement\", \"type\": \"text\", \"default\": \"; \", \"description\": \"Text inserted between detected cells.\"}\n"
        << "]\n\n"
        << "PARAMETER LUA EXAMPLE:\n"
        << "function transform(text, params)\n"
        << "  local min_chars = tonumber(params.min_chars or '120') or 120\n"
        << "  local keep_dialogue = (params.keep_dialogue or 'true') == 'true'\n"
        << "  local replacement = params.replacement or '; '\n"
        << "  return text\n"
        << "end\n\n"
        << "REQUIRED JSON OBJECT SHAPE. Use this exact top-level structure. Extra top-level fields are allowed only if they are useful analysis metadata, but the installable fields below must exist:\n"
        << "{\n"
        << "  \"analysis\": {\n"
        << "    \"can_be_done\": true,\n"
        << "    \"custom_processor_needed\": true,\n"
        << "    \"limitations\": \"Short note about what cannot be guaranteed, or empty string.\",\n"
        << "    \"recommended_existing_processors\": [\n"
        << "      {\"id\": \"builtin_id_from_list_or_empty\", \"name\": \"Existing processor name\", \"reason\": \"Why it may solve part of the request.\"}\n"
        << "    ],\n"
        << "    \"parameter_decisions\": [\n"
        << "      {\"id\": \"min_chars\", \"reason\": \"User asked for lines longer than 120 characters, so default is 120.\"}\n"
        << "    ]\n"
        << "  },\n"
        << "  \"id\": \"user_safe_snake_case_id\",\n"
        << "  \"name\": \"Human readable name\",\n"
        << "  \"group\": \"" << group << "\",\n"
        << "  \"scope\": \"" << scope << "\",\n"
        << "  \"output\": \"" << output << "\",\n"
        << "  \"description\": \"What this processor does.\",\n"
        << "  \"repeat_default\": 1,\n"
        << "  \"params\": [],\n"
        << "  \"lua\": \"function transform(text, params)\\n  return text\\nend\\n\"\n"
        << "}\n\n"
        << "LUA IMPLEMENTATION RULES:\n"
        << "- Escape Lua newlines in the JSON string as \\n.\n"
        << "- Do not use regex syntax from Python/JavaScript. Lua patterns are different.\n"
        << "- Preserve UTF-8 text. Avoid byte-level changes unless clearly needed.\n"
        << "- For report processors, do not change the source text; return a readable report string.\n"
        << "- For replace_text processors, return the transformed text only.\n"
        << "- If the request cannot be done safely in sandboxed Lua, set analysis.can_be_done to false, explain the limitation in analysis.limitations, and return a no-op transform.\n";
    return prompt.str();
}

std::string BuildCustomProcessorRepairPrompt(
    const CustomProcessorPromptRequest& request,
    const std::string& invalid_json,
    const std::string& validation_error) {
    std::ostringstream prompt;
    prompt
        << "You are repairing a TextLT Custom Processor Builder JSON object that failed validation.\n"
        << "Return ONLY one corrected raw JSON object.\n"
        << "Do not write explanations before or after the JSON.\n"
        << "Do not use Markdown fences. Do not write ```json.\n"
        << "Keep the same user intent, but fix every validation error.\n"
        << "Put any notes for the human inside analysis.limitations or analysis.parameter_decisions.\n"
        << "If the original user request below is empty, infer the intent from INVALID JSON fields such as name, description, analysis, params, and lua.\n\n"
        << BuildJsonRepairAdvice()
        << BuildLuaJsonEscapingRules()
        << "VALIDATION ERROR FROM TEXTLT:\n"
        << (TrimCopy(validation_error).empty() ? "Unknown validation error." : TrimCopy(validation_error))
        << "\n\n"
        << "INVALID JSON THAT MUST BE FIXED:\n"
        << TrimCopy(invalid_json) << "\n\n"
        << "ORIGINAL REQUEST AND ALL TEXTLT RULES FOLLOW. Obey them exactly while producing the corrected JSON.\n\n"
        << BuildCustomProcessorAiPrompt(request);
    return prompt.str();
}

CustomProcessorInstallResult ValidateCustomProcessorJson(const std::string& json_text) {
    CustomProcessorInstallResult result;

    Json input;
    try {
        input = Json::parse(ExtractJsonPayload(json_text));
    } catch (const std::exception& ex) {
        result.error = std::string("Cannot parse AI JSON result: ") + ex.what();
        return result;
    }

    if (!input.is_object()) {
        result.error = "AI result must be one JSON object.";
        return result;
    }

    std::string id = JsonString(input, "id");
    if (TrimCopy(id).empty()) {
        id = ToSnakeId(JsonString(input, "name", "custom_processor"));
    }
    id = ToSnakeId(id);
    if (!IsSafeId(id)) {
        result.error = "Invalid processor id: " + id;
        return result;
    }
    if (IsBuiltinTextProcessor(id)) {
        result.error = "Cannot replace built-in processor: " + id;
        return result;
    }

    const std::string name = TrimCopy(JsonString(input, "name"));
    if (name.empty()) {
        result.error = "Processor name is required.";
        return result;
    }

    if (input.contains("params")) {
        if (!input.at("params").is_array()) {
            result.error = "params must be an array.";
            return result;
        }
        if (input.at("params").size() > 4) {
            result.error = "Custom processors support at most 4 params.";
            return result;
        }
        for (const auto& param : input.at("params")) {
            std::string error;
            (void)NormalizeParamJson(param, error);
            if (!error.empty()) {
                result.error = error;
                return result;
            }
        }
    }

    const std::string lua = LuaScriptFromJson(input);
    if (!ValidateLuaScript(lua, result.error)) {
        return result;
    }

    result.success = true;
    result.id = id;
    result.name = name;
    return result;
}

CustomProcessorInstallResult InstallCustomProcessorFromJson(
    const std::string& json_text,
    const std::filesystem::path& default_processors_directory) {
    CustomProcessorInstallResult result;

    TextParserManager manager;
    if (!manager.EnsureUserConfiguration(default_processors_directory, result.error)) {
        return result;
    }

    Json input;
    try {
        input = Json::parse(ExtractJsonPayload(json_text));
    } catch (const std::exception& ex) {
        result.error = std::string("Cannot parse AI JSON result: ") + ex.what();
        return result;
    }

    if (!input.is_object()) {
        result.error = "AI result must be one JSON object.";
        return result;
    }

    std::string id = JsonString(input, "id");
    if (TrimCopy(id).empty()) {
        id = ToSnakeId(JsonString(input, "name", "custom_processor"));
    }
    id = ToSnakeId(id);
    if (!IsSafeId(id)) {
        result.error = "Invalid processor id: " + id;
        return result;
    }
    if (IsBuiltinTextProcessor(id)) {
        result.error = "Cannot replace built-in processor: " + id;
        return result;
    }

    const std::string name = TrimCopy(JsonString(input, "name"));
    if (name.empty()) {
        result.error = "Processor name is required.";
        return result;
    }

    const std::string group = TrimCopy(JsonString(input, "group", "User")).empty()
        ? "User"
        : TrimCopy(JsonString(input, "group", "User"));
    const std::string scope = NormalizeScopeValue(JsonString(input, "scope", "text"));
    const std::string output = NormalizeOutputValue(JsonString(input, "output", "replace_text"));
    const std::string lua = LuaScriptFromJson(input);

    if (!ValidateLuaScript(lua, result.error)) {
        return result;
    }

    Json params = Json::array();
    if (input.contains("params")) {
        if (!input.at("params").is_array()) {
            result.error = "params must be an array.";
            return result;
        }
        if (input.at("params").size() > 4) {
            result.error = "Custom processors support at most 4 params.";
            return result;
        }
        for (const auto& param : input.at("params")) {
            std::string error;
            Json normalized = NormalizeParamJson(param, error);
            if (!error.empty()) {
                result.error = error;
                return result;
            }
            params.push_back(std::move(normalized));
        }
    }

    const fs::path config_path = TextParserManager::UserConfigFilePath();
    const fs::path script_relative = fs::path("text_parsers") / (id + ".lua");
    const fs::path script_path = TextParserManager::UserConfigDirectory() / script_relative;

    Json root;
    try {
        const std::string config_text = ReadTextFile(config_path, result.error);
        if (!result.error.empty()) {
            return result;
        }
        root = Json::parse(config_text);
    } catch (const std::exception& ex) {
        result.error = std::string("Cannot parse user text parser config: ") + ex.what();
        return result;
    }

    Json parser = Json::object();
    parser["id"] = id;
    parser["name"] = name;
    parser["scope"] = scope;
    parser["script"] = script_relative.generic_string();
    parser["description"] = JsonString(input, "description", "User-created custom text processor.");
    parser["repeat_default"] = std::max(1, JsonInt(input, "repeat_default", 1));
    parser["params"] = std::move(params);
    parser["group"] = group;
    parser["output"] = output;
    parser["pinned"] = JsonBool(input, "pinned", false);

    if (!ReplaceOrAppendParser(root, parser, id, result.error)) {
        return result;
    }

    if (!WriteTextFile(script_path, lua, result.error)) {
        return result;
    }
    if (!WriteTextFile(config_path, root.dump(2) + "\n", result.error)) {
        return result;
    }

    result.success = true;
    result.id = id;
    result.name = name;
    result.config_path = config_path;
    result.script_path = script_path;
    return result;
}

std::vector<CustomProcessorSummary> ListEditableCustomProcessors(std::string& error) {
    error.clear();
    std::vector<CustomProcessorSummary> processors;

    Json root;
    if (!ReadUserProcessorRoot(root, error)) {
        return processors;
    }

    for (const auto& parser_json : root.at("parsers")) {
        if (!parser_json.is_object()) {
            continue;
        }
        std::string id;
        std::string editable_error;
        if (!IsEditableUserProcessor(parser_json, id, editable_error)) {
            if (!editable_error.empty()) {
                error = editable_error;
                processors.clear();
                return processors;
            }
            continue;
        }

        CustomProcessorSummary summary;
        summary.id = id;
        summary.name = JsonString(parser_json, "name", id);
        summary.group = JsonString(parser_json, "group", "User");
        summary.scope = NormalizeScopeValue(JsonString(parser_json, "scope", "text"));
        summary.output = NormalizeOutputValue(JsonString(parser_json, "output", "replace_text"));
        summary.description = JsonString(parser_json, "description", "");
        summary.script_path = ResolveUserScriptPath(parser_json);
        processors.push_back(std::move(summary));
    }

    std::sort(processors.begin(), processors.end(), [](const auto& left, const auto& right) {
        return ToLowerCopy(left.name) < ToLowerCopy(right.name);
    });
    return processors;
}

CustomProcessorLoadResult LoadCustomProcessorForEditing(const std::string& processor_id) {
    CustomProcessorLoadResult result;

    Json root;
    if (!ReadUserProcessorRoot(root, result.error)) {
        return result;
    }

    const Json* parser_json = FindEditableProcessorJson(root, processor_id, result.error);
    if (parser_json == nullptr) {
        return result;
    }

    const fs::path script_path = ResolveUserScriptPath(*parser_json);
    if (script_path.empty()) {
        result.error = "Processor has no Lua script path: " + processor_id;
        return result;
    }

    const std::string lua = ReadTextFile(script_path, result.error);
    if (!result.error.empty()) {
        return result;
    }

    const Json editable = BuildProcessorEditJson(*parser_json, lua);
    result.success = true;
    result.id = JsonString(*parser_json, "id");
    result.name = JsonString(*parser_json, "name", result.id);
    result.json_text = editable.dump(2) + "\n";
    return result;
}

CustomProcessorDeleteResult DeleteCustomProcessor(const std::string& processor_id) {
    CustomProcessorDeleteResult result;
    result.id = processor_id;
    result.config_path = TextParserManager::UserConfigFilePath();

    Json root;
    if (!ReadUserProcessorRoot(root, result.error)) {
        return result;
    }

    bool found = false;
    fs::path script_path;
    auto& parsers = root["parsers"];
    for (auto it = parsers.begin(); it != parsers.end(); ++it) {
        if (!it->is_object()) {
            continue;
        }
        const std::string id = JsonString(*it, "id");
        if (id != processor_id) {
            continue;
        }

        std::string editable_id;
        std::string editable_error;
        if (!IsEditableUserProcessor(*it, editable_id, editable_error)) {
            result.error = editable_error.empty()
                ? "Processor is not editable: " + processor_id
                : editable_error;
            return result;
        }

        found = true;
        result.name = JsonString(*it, "name", id);
        script_path = ResolveUserScriptPath(*it);
        result.script_path = script_path;
        parsers.erase(it);
        break;
    }

    if (!found) {
        result.error = "Processor not found: " + processor_id;
        return result;
    }

    if (!WriteTextFile(result.config_path, root.dump(2) + "\n", result.error)) {
        return result;
    }

    if (!script_path.empty()) {
        std::error_code ec;
        if (fs::exists(script_path, ec)) {
            fs::remove(script_path, ec);
            if (ec) {
                result.error = "Processor entry was deleted, but Lua script could not be removed: " +
                    script_path.string() + ": " + ec.message();
                return result;
            }
        }
    }

    result.success = true;
    return result;
}

} // namespace textlt
