#pragma once

#include <string>
#include <vector>

namespace textlt {

struct BuiltInAiModel {
    std::string id;
    std::string title;
    std::string filename;
    std::string download_url;
    std::string description;
    std::string tier;
    bool gpu_required = false;
    int recommended_vram_mb = 0;
};

const std::vector<BuiltInAiModel>& BuiltInGpuModels();
const BuiltInAiModel* FindBuiltInAiModel(const std::string& filename);

} // namespace textlt
