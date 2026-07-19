#include "ai/ai_model_catalog.hpp"

namespace textlt {

const std::vector<BuiltInAiModel>& BuiltInGpuModels() {
    static const std::vector<BuiltInAiModel> models = {
        {
            "gemma-3-4b-it-qat-q4_0",
            "Gemma 3 4B IT QAT Q4_0",
            "gemma-3-4b-it-q4_0.gguf",
            "https://huggingface.co/google/gemma-3-4b-it-qat-q4_0-gguf/resolve/15f73f5eee9c28f53afefef5723e29680c2fc78a/gemma-3-4b-it-q4_0.gguf?download=true",
            "Standard GPU model · 4 GB VRAM recommended · Text only. Official Google QAT Q4_0 GGUF pinned to the published model-file revision; a GPU-enabled llama.cpp runtime, Hugging Face license acceptance and HF_TOKEN may be required.",
            "Standard",
            true,
            4096,
        },
        {
            "gemma-4-e2b-it-qat-q4_0",
            "Gemma 4 E2B IT QAT Q4_0",
            "gemma-4-E2B_q4_0-it.gguf",
            "https://huggingface.co/google/gemma-4-E2B-it-qat-q4_0-gguf/resolve/347eef722ec7f151f37d1ef0b5c7c77d8de4efcb/gemma-4-E2B_q4_0-it.gguf?download=true",
            "Advanced GPU model · 6 GB VRAM recommended · Text only. Official Google validated QAT Q4_0 checkpoint with corrected vocabulary; a GPU-enabled llama.cpp runtime, Hugging Face license acceptance and HF_TOKEN may be required.",
            "Advanced",
            true,
            6144,
        },
    };
    return models;
}

const BuiltInAiModel* FindBuiltInAiModel(const std::string& filename) {
    for (const BuiltInAiModel& model : BuiltInGpuModels()) {
        if (model.filename == filename) {
            return &model;
        }
    }
    return nullptr;
}

} // namespace textlt
