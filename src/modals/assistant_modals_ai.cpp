#include "assistant_modals.hpp"

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
    Elements rows = {
        hbox({
            fetch_ai_button_->Render(),
            text(" "),
            ai_runtime_download_button_->Render(),
            text(" "),
            ai_model_download_button_->Render(),
            text(" "),
            ai_delete_model_button_->Render(),
        }),
    };
    if (ai_progress_ > 0.0f) {
        rows.push_back(gauge(ai_progress_) | border);
    }
    rows.push_back(separator() | color(theme.modal_border));
    rows.push_back(text(" Models") | bold | color(theme.modal_text_color));
    rows.push_back(ai_model_menu_->Render() | border);
    return vbox(std::move(rows)) | border;
}

} // namespace textlt
