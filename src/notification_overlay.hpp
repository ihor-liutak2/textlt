#pragma once

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "ftxui/component/app.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "notification_manager.hpp"
#include "theme.hpp"

namespace textlt {

// FTXUI presentation and interaction layer. The manager remains UI-independent.
class NotificationOverlay {
public:
    NotificationOverlay(NotificationManager& manager, ftxui::App& app);
    ~NotificationOverlay();

    NotificationOverlay(const NotificationOverlay&) = delete;
    NotificationOverlay& operator=(const NotificationOverlay&) = delete;

    ftxui::Element Render(const Theme& theme);
    bool HandleEvent(ftxui::Event event);
    bool DismissNewest();
    void StateChanged();
    void Stop();

private:
    struct HitTarget {
        std::uint64_t id;
        ftxui::Box toast;
        ftxui::Box close;
    };

    void ScheduleNextExpiration();
    void TimerLoop();

    NotificationManager& manager_;
    ftxui::App& app_;
    std::vector<HitTarget> hit_targets_;
    std::optional<std::uint64_t> hovered_close_;
    std::mutex timer_mutex_;
    std::condition_variable timer_condition_;
    std::optional<NotificationManager::Clock::time_point> timer_deadline_;
    bool stopped_ = false;
    std::thread timer_thread_;
};

} // namespace textlt
