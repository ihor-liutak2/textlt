#pragma once

#include <atomic>
#include <functional>
#include <thread>

namespace textlt {
namespace assistant_modal_detail {

std::thread StartAssistantDownloadProgress(
    std::atomic_bool& running,
    std::function<void(float)> set_progress,
    std::function<void()> request_redraw = {});

} // namespace assistant_modal_detail
} // namespace textlt
