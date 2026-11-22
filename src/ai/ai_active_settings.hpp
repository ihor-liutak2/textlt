#pragma once

#include "ai/ai_backend.hpp"
#include "editor_config.hpp"

namespace textlt {

inline AiBackendSettings ActiveAiSettings(const EditorConfig& config) {
    AiBackendSettings settings;
    settings.provider = AiBackend::ProviderFromConfig(config.ai_active_provider);
    const auto found = config.ai_providers.find(config.ai_active_provider);
    if (found != config.ai_providers.end()) {
        const auto& profile = found->second;
        settings.server_url = profile.endpoint;
        settings.api_key = profile.api_key;
        settings.model = profile.model;
        settings.opencode_use_go = profile.opencode_use_go;
        const auto effort = profile.model_efforts.find(profile.model);
        if (effort != profile.model_efforts.end()) {
            settings.effort = effort->second;
        }
    }
    return settings;
}

} // namespace textlt
