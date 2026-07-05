#include "assistant_modals.hpp"

#include <algorithm>
#include <utility>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"

namespace textlt {
namespace {

ftxui::Element StatusLine(const std::string& label,
                          const std::string& value,
                          const Theme& theme) {
    using namespace ftxui;
    return hbox({
        text(" " + label + ": ") | bold | color(theme.modal_accent),
        text(value) | color(theme.modal_text_color),
    });
}

bool AiModelInstalled(const Json& model) {
    using namespace assistant_modal_detail;

    const std::string filename = JsonString(model, "filename");
    if (filename.empty()) {
        return false;
    }

    std::error_code error;
    return std::filesystem::exists(
        UserDataDirectory() / "ai" / "models" / filename,
        error);
}

std::filesystem::path AiRuntimeDirectory() {
    using namespace assistant_modal_detail;
    return UserDataDirectory() / "ai" / "runtimes" / "llama_cpp";
}

std::string AiRuntimeDisplayPath() {
#ifdef _WIN32
    return "%LOCALAPPDATA%\\textlt\\ai\\runtimes\\llama_cpp\\llama-cli.exe";
#else
    return "~/.local/share/textlt/ai/runtimes/llama_cpp/llama-cli";
#endif
}

bool AiRuntimeInstalled() {
    std::error_code error;
    const std::filesystem::path directory = AiRuntimeDirectory();
    if (!std::filesystem::exists(directory, error)) {
        return false;
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory, error)) {
        if (error) {
            return false;
        }
        const std::string filename = entry.path().filename().string();
        if (filename == "llama-cli" || filename == "llama-cli.exe") {
            return true;
        }
    }
    return false;
}

std::string SelectedAiModelDescription(int selected_model) {
    using namespace assistant_modal_detail;

    Json root;
    if (LoadUserRegistryJson(RegistryKind::Ai, &root) != RegistryLoadResult::Loaded) {
        return {};
    }

    const auto models = root.find("models");
    if (models == root.end() || !models->is_array()) {
        return {};
    }

    int visible_index = 0;
    for (const Json& model : *models) {
        if (!model.is_object()) {
            continue;
        }
        if (visible_index == selected_model) {
            return JsonString(model, "description");
        }
        ++visible_index;
    }
    return {};
}

std::vector<std::string> WrapText(const std::string& text, size_t width) {
    std::vector<std::string> lines;
    std::string current;
    size_t position = 0;
    while (position < text.size()) {
        while (position < text.size() && text[position] == ' ') {
            ++position;
        }
        const size_t start = position;
        while (position < text.size() && text[position] != ' ') {
            ++position;
        }
        std::string word = text.substr(start, position - start);
        if (word.empty()) {
            continue;
        }
        if (current.empty()) {
            current = std::move(word);
        } else if (current.size() + 1 + word.size() <= width) {
            current += " " + word;
        } else {
            lines.push_back(current);
            current = std::move(word);
        }
    }
    if (!current.empty()) {
        lines.push_back(current);
    }
    return lines;
}

} // namespace

void AssistantSettingsModalContent::LoadAiRegistry() {
    using namespace assistant_modal_detail;

    Json root;
    const RegistryLoadResult load_result = LoadUserRegistryJson(RegistryKind::Ai, &root);
    ai_model_labels_.clear();
    if (load_result == RegistryLoadResult::Loaded) {
        const auto models = root.find("models");
        if (models != root.end() && models->is_array()) {
            for (const Json& model : *models) {
                if (!model.is_object()) {
                    continue;
                }
                std::string label = JsonLabel(model, "title", "id");
                const std::string purpose = JsonString(model, "purpose");
                const std::string backend = JsonString(model, "backend");
                label += " | " + (purpose.empty() ? "unknown" : purpose);
                label += " | " + (backend.empty() ? "unknown" : backend);
                label += AiModelInstalled(model) ? " | installed" : " | not installed";
                ai_model_labels_.push_back(label);
            }
        }
    }
    if (ai_model_labels_.empty()) {
        ai_model_labels_.push_back("No models");
        if (load_result == RegistryLoadResult::Missing) {
            ai_status_ = "Registry not loaded";
        } else if (load_result == RegistryLoadResult::ParseFailed) {
            ai_status_ = "Failed to parse registry";
        } else {
            ai_status_ = "Registry loaded, no items found";
        }
    } else if (ai_status_.find("TODO:") != 0) {
        ai_status_ = "Registry loaded";
    }
    selected_ai_model_ = 0;
}

ftxui::Element AssistantSettingsModalContent::RenderAiTab(const Theme& theme) {
    using namespace ftxui;
    bool refresh_models = false;
    bool show_runtime_delete_confirmation = false;
    bool show_runtime_progress = false;
    bool show_extraction_progress = false;
    bool show_model_progress = false;
    bool show_model_delete_confirmation = false;
    bool show_model_delete_progress = false;
    {
        std::lock_guard<std::mutex> lock(ai_runtime_mutex_);
        refresh_models = ai_refresh_after_model_download_;
        ai_refresh_after_model_download_ = false;
        show_runtime_delete_confirmation = ai_runtime_delete_confirm_visible_;
        show_runtime_progress = ai_runtime_progress_visible_;
        show_extraction_progress = ai_runtime_extracting_;
        show_model_progress = ai_model_progress_visible_;
        show_model_delete_confirmation = ai_model_delete_confirm_visible_;
        show_model_delete_progress = ai_model_deleting_;
    }
    if (refresh_models) {
        std::string status;
        {
            std::lock_guard<std::mutex> lock(ai_runtime_mutex_);
            status = ai_status_;
        }
        LoadAiRegistry();
        {
            std::lock_guard<std::mutex> lock(ai_runtime_mutex_);
            ai_status_ = status;
        }
    }
    const float ai_progress = std::max(0.0f, std::min(1.0f, ai_progress_.load()));

    Elements rows;
    rows.push_back(text(" Runtime") | bold | color(theme.modal_text_color));
    rows.push_back(StatusLine(
        "llama.cpp runtime",
        AiRuntimeInstalled() ? "installed" : "not installed",
        theme));
    rows.push_back(StatusLine("runtime path", AiRuntimeDisplayPath(), theme));
    rows.push_back(separator() | color(theme.modal_border));
    rows.push_back(hbox({
        ai_runtime_download_button_->Render(),
        text(" "),
        ai_runtime_delete_button_->Render(),
    }));
    if (show_runtime_delete_confirmation) {
        rows.push_back(text(" Delete llama.cpp runtime?") |
                       bold |
                       color(theme.modal_text_color));
        rows.push_back(hbox({
            ai_runtime_confirm_delete_button_->Render(),
            text(" "),
            ai_runtime_cancel_delete_button_->Render(),
        }));
    }
    if (show_runtime_progress) {
        const int percent = static_cast<int>(ai_progress * 100.0f + 0.5f);
        if (show_extraction_progress) {
            rows.push_back(hbox({
                filler(),
                text(std::to_string(percent) + "% ") | color(theme.modal_text_color),
            }));
            rows.push_back(gauge(ai_progress) | border);
        } else {
        rows.push_back(hbox({
            filler(),
            text(std::to_string(percent) + "% ") | color(theme.modal_text_color),
        }));
        rows.push_back(gauge(ai_progress) | border);
        }
    }
    rows.push_back(separator() | color(theme.modal_border));
    rows.push_back(text(" Models") | bold | color(theme.modal_text_color));
    rows.push_back(hbox({
        fetch_ai_button_->Render(),
        text(" "),
        ai_model_download_button_->Render(),
        text(" "),
        ai_delete_model_button_->Render(),
    }));
    if (show_model_delete_confirmation) {
        rows.push_back(text(" Delete selected model?") |
                       bold |
                       color(theme.modal_text_color));
        rows.push_back(hbox({
            ai_model_confirm_delete_button_->Render(),
            text(" "),
            ai_model_cancel_delete_button_->Render(),
        }));
    }
    if (show_model_progress) {
        const int percent = static_cast<int>(ai_progress * 100.0f + 0.5f);
        if (show_model_delete_progress) {
            rows.push_back(hbox({
                filler(),
                text(std::to_string(percent) + "% ") | color(theme.modal_text_color),
            }));
        } else {
            rows.push_back(hbox({
                filler(),
                text(std::to_string(percent) + "% ") | color(theme.modal_text_color),
            }));
        }
        rows.push_back(gauge(ai_progress) | border);
    }
    rows.push_back(ai_model_menu_->Render() | border);
    const std::string description = SelectedAiModelDescription(selected_ai_model_);
    if (!description.empty()) {
        for (const std::string& line : WrapText(description, 76)) {
            rows.push_back(text(" " + line) |
                           color(theme.modal_text_color) |
                           dim);
        }
    }
    return vbox(std::move(rows)) | border;
}

} // namespace textlt
