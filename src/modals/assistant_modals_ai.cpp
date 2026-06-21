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

std::string FormatBytes(unsigned long long value) {
    std::string text = std::to_string(value);
    std::string formatted;
    int group_count = 0;
    for (auto iter = text.rbegin(); iter != text.rend(); ++iter) {
        if (group_count == 3) {
            formatted.push_back(' ');
            group_count = 0;
        }
        formatted.push_back(*iter);
        ++group_count;
    }
    std::reverse(formatted.begin(), formatted.end());
    return formatted;
}

std::string SelectedAiModelDescription(int selected_model) {
    using namespace assistant_modal_detail;

    Json root;
    if (LoadUserRegistryJson(kAiRegistryFile, &root) != RegistryLoadResult::Loaded) {
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

AiActionsModalContent::AiActionsModalContent(const Theme* theme)
    : theme_(theme) {
    renderer_ = ftxui::Renderer([this] { return Render(); });
}

ftxui::Element AiActionsModalContent::Render() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    return vbox({
        text(" AI Actions") | bold | color(theme.modal_accent),
        separator() | color(theme.modal_border),
        StatusLine("Model", "gemma3:1b", theme),
        text(""),
        text(" Actions") | bold | color(theme.modal_text_color),
        text("  - improve") | color(theme.modal_text_color),
        text("  - translate") | color(theme.modal_text_color),
        text("  - summarize") | color(theme.modal_text_color),
        text("  - explain") | color(theme.modal_text_color),
    }) |
        bgcolor(theme.modal_input_bg) |
        color(theme.modal_input_fg);
}

void AssistantSettingsModalContent::LoadAiRegistry() {
    using namespace assistant_modal_detail;

    Json root;
    const RegistryLoadResult load_result = LoadUserRegistryJson(kAiRegistryFile, &root);
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

void AssistantSettingsModalContent::FetchAiRegistry() {
    using namespace assistant_modal_detail;

    ai_status_ = "Fetching registry...";
    ai_progress_ = 0.0f;
    const RegistryDownloadResult result =
        DownloadRegistry(kAiRegistryUrl, kAiRegistryFile);
    if (result == RegistryDownloadResult::Saved) {
        LoadAiRegistry();
        ai_status_ = "Registry loaded";
    } else if (result == RegistryDownloadResult::Empty) {
        ai_status_ = "Registry file is empty";
    } else if (result == RegistryDownloadResult::InvalidJson) {
        ai_status_ = "Downloaded registry is invalid JSON";
    } else {
        ai_status_ = "Registry download failed";
    }
}

ftxui::Element AssistantSettingsModalContent::RenderAiTab(const Theme& theme) {
    using namespace ftxui;
    bool refresh_models = false;
    bool show_runtime_delete_confirmation = false;
    bool show_runtime_progress = false;
    bool show_extraction_progress = false;
    bool show_model_progress = false;
    unsigned long long downloaded_bytes = 0;
    unsigned long long total_bytes = 0;
    unsigned long long model_downloaded_bytes = 0;
    unsigned long long model_total_bytes = 0;
    {
        std::lock_guard<std::mutex> lock(ai_runtime_mutex_);
        refresh_models = ai_refresh_after_model_download_;
        ai_refresh_after_model_download_ = false;
        show_runtime_delete_confirmation = ai_runtime_delete_confirm_visible_;
        show_runtime_progress = ai_runtime_progress_visible_;
        show_extraction_progress = ai_runtime_extracting_;
        show_model_progress = ai_model_progress_visible_;
        downloaded_bytes = ai_runtime_downloaded_bytes_;
        total_bytes = ai_runtime_total_bytes_;
        model_downloaded_bytes = ai_model_downloaded_bytes_;
        model_total_bytes = ai_model_total_bytes_;
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
        std::string byte_text = FormatBytes(downloaded_bytes) + " bytes";
        if (total_bytes > 0) {
            byte_text = FormatBytes(downloaded_bytes) + " / " +
                        FormatBytes(total_bytes) + " bytes";
        }
        rows.push_back(hbox({
            text(" " + byte_text) | color(theme.modal_text_color),
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
    if (show_model_progress) {
        std::string byte_text = FormatBytes(model_downloaded_bytes) + " bytes";
        if (model_total_bytes > 0) {
            byte_text = FormatBytes(model_downloaded_bytes) + " / " +
                        FormatBytes(model_total_bytes) + " bytes";
        }
        const int percent = static_cast<int>(ai_progress * 100.0f + 0.5f);
        rows.push_back(hbox({
            text(" " + byte_text) | color(theme.modal_text_color),
            filler(),
            text(std::to_string(percent) + "% ") | color(theme.modal_text_color),
        }));
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
