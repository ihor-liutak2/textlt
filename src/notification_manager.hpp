#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace textlt {

enum class NotificationType {
    Success,
    Warning,
    Error,
};

struct Notification {
    using Clock = std::chrono::steady_clock;

    std::uint64_t id = 0;
    NotificationType type = NotificationType::Success;
    std::string message;
    Clock::time_point expires_at{};
};

struct NotificationLayout {
    static constexpr int minimum_width = 25;
    static constexpr int maximum_width = 45;
    static constexpr int horizontal_padding = 2;
    static constexpr int vertical_padding = 1;
    static constexpr int right_margin = 2;
    static constexpr int bottom_margin_above_status = 1;
    static constexpr std::size_t maximum_visible = 3;
    static constexpr int vertical_spacing = 1;
};

struct NotificationConfig {
    std::chrono::milliseconds success_duration{1000};
    std::chrono::milliseconds warning_duration{1000};
    std::chrono::milliseconds error_duration{1000};
};

// UI-independent notification lifetime and queue management.
class NotificationManager {
public:
    using Clock = Notification::Clock;

    explicit NotificationManager(NotificationConfig config = {});
    void SetOnChanged(std::function<void()> callback) {
        on_changed_ = std::move(callback);
    }

    std::uint64_t Add(NotificationType type, std::string message,
                      Clock::time_point now = Clock::now());
    std::uint64_t Success(std::string message, Clock::time_point now = Clock::now());
    std::uint64_t Warning(std::string message, Clock::time_point now = Clock::now());
    std::uint64_t Error(std::string message, Clock::time_point now = Clock::now());

    // Removes expired visible notifications and starts queued lifetimes when
    // their entries become visible.
    bool Tick(Clock::time_point now = Clock::now());
    bool Dismiss(std::uint64_t id, Clock::time_point now = Clock::now());
    bool DismissNewest(Clock::time_point now = Clock::now());

    const std::vector<Notification>& Visible() const { return visible_; }
    std::size_t QueuedCount() const { return queued_.size(); }
    bool Empty() const { return visible_.empty() && queued_.empty(); }
    Clock::time_point NextExpiration() const;
    const NotificationConfig& Config() const { return config_; }

private:
    struct Pending {
        std::uint64_t id;
        NotificationType type;
        std::string message;
    };

    std::chrono::milliseconds DurationFor(NotificationType type) const;
    bool PromoteQueued(Clock::time_point now);
    void Changed();

    NotificationConfig config_;
    std::uint64_t next_id_ = 1;
    std::vector<Notification> visible_;
    std::deque<Pending> queued_;
    std::function<void()> on_changed_;
};

} // namespace textlt
