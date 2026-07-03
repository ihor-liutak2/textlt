#pragma once

#include "text_parser_manager.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace textlt {

struct CustomProcessorPromptRequest {
    std::string user_request;
    std::string group = "User";
    std::string scope = "text";
    std::string output = "replace_text";
};

struct CustomProcessorInstallResult {
    bool success = false;
    std::string id;
    std::string name;
    std::filesystem::path config_path;
    std::filesystem::path script_path;
    std::string error;
};

std::vector<std::string> CustomProcessorGroupChoices();
std::vector<std::string> CustomProcessorScopeChoices();
std::vector<std::string> CustomProcessorOutputChoices();

std::string BuildCustomProcessorAiPrompt(const CustomProcessorPromptRequest& request);

std::string BuildCustomProcessorRepairPrompt(
    const CustomProcessorPromptRequest& request,
    const std::string& invalid_json,
    const std::string& validation_error);

CustomProcessorInstallResult ValidateCustomProcessorJson(const std::string& json_text);

CustomProcessorInstallResult InstallCustomProcessorFromJson(
    const std::string& json_text,
    const std::filesystem::path& default_processors_directory);

} // namespace textlt
