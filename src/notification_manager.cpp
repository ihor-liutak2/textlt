#include "notification_manager.hpp"

#include <algorithm>
#include <utility>

namespace textlt {

NotificationManager::NotificationManager(NotificationConfig config)
    : config_(std::move(config)) {}

std::chrono::milliseconds NotificationManager::DurationFor(NotificationType type) const {
    switch (type) {
        case NotificationType::Success: return config_.success_duration;
        case NotificationType::Warning: return config_.warning_duration;
        case NotificationType::Error: return config_.error_duration;
    }
    return config_.success_duration;
}

std::uint64_t NotificationManager::Add(
    NotificationType type, std::string message, Clock::time_point now) {
    const std::uint64_t id = next_id_++;
    if (visible_.size() < NotificationLayout::maximum_visible) {
        visible_.push_back({id, type, std::move(message), now + DurationFor(type)});
    } else {
        queued_.push_back({id, type, std::move(message)});
    }
    Changed();
    return id;
}

std::uint64_t NotificationManager::Success(std::string message, Clock::time_point now) {
    return Add(NotificationType::Success, std::move(message), now);
}

std::uint64_t NotificationManager::Warning(std::string message, Clock::time_point now) {
    return Add(NotificationType::Warning, std::move(message), now);
}

std::uint64_t NotificationManager::Error(std::string message, Clock::time_point now) {
    return Add(NotificationType::Error, std::move(message), now);
}

bool NotificationManager::PromoteQueued(Clock::time_point now) {
    bool promoted = false;
    while (visible_.size() < NotificationLayout::maximum_visible && !queued_.empty()) {
        Pending pending = std::move(queued_.front());
        queued_.pop_front();
        visible_.push_back({
            pending.id, pending.type, std::move(pending.message),
            now + DurationFor(pending.type)});
        promoted = true;
    }
    return promoted;
}

void NotificationManager::Changed() {
    if (on_changed_) on_changed_();
}

bool NotificationManager::Tick(Clock::time_point now) {
    const std::size_t old_visible = visible_.size();
    visible_.erase(
        std::remove_if(visible_.begin(), visible_.end(),
            [now](const Notification& notification) {
                return now >= notification.expires_at;
            }),
        visible_.end());
    const bool expired = old_visible != visible_.size();
    const bool promoted = PromoteQueued(now);
    if (expired || promoted) Changed();
    return expired || promoted;
}

bool NotificationManager::Dismiss(std::uint64_t id, Clock::time_point now) {
    const auto iter = std::find_if(visible_.begin(), visible_.end(),
        [id](const Notification& notification) { return notification.id == id; });
    if (iter == visible_.end()) return false;
    visible_.erase(iter);
    PromoteQueued(now);
    Changed();
    return true;
}

bool NotificationManager::DismissNewest(Clock::time_point now) {
    if (visible_.empty()) return false;
    visible_.pop_back();
    PromoteQueued(now);
    Changed();
    return true;
}

NotificationManager::Clock::time_point NotificationManager::NextExpiration() const {
    if (visible_.empty()) return Clock::time_point::max();
    return std::min_element(visible_.begin(), visible_.end(),
        [](const Notification& left, const Notification& right) {
            return left.expires_at < right.expires_at;
        })->expires_at;
}

} // namespace textlt
