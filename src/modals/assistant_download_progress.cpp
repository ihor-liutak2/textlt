#include "assistant_download_progress.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

namespace textlt {
namespace assistant_modal_detail {

std::thread StartAssistantDownloadProgress(
    std::atomic_bool& running,
    std::function<void(float)> set_progress,
    std::function<void()> request_redraw) {
    return std::thread([&running,
                        set_progress = std::move(set_progress),
                        request_redraw = std::move(request_redraw)]() mutable {
        using namespace std::chrono_literals;

        constexpr int kProgressWindowSeconds = 20;
        int elapsed_seconds = 0;
        int target_seconds = kProgressWindowSeconds;

        while (running.load()) {
            std::this_thread::sleep_for(1s);
            if (!running.load()) {
                break;
            }

            ++elapsed_seconds;
            if (elapsed_seconds > target_seconds) {
                target_seconds += kProgressWindowSeconds;
            }

            const float progress = std::clamp(
                static_cast<float>(elapsed_seconds) /
                    static_cast<float>(target_seconds),
                0.0f,
                1.0f);
            if (set_progress) {
                set_progress(progress);
            }
            if (request_redraw) {
                request_redraw();
            }
        }
    });
}

} // namespace assistant_modal_detail
} // namespace textlt
