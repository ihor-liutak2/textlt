#include "notification_overlay.hpp"

#include <algorithm>
#include <string>

#include "ftxui/component/mouse.hpp"
#include "ftxui/screen/string.hpp"

namespace textlt {
namespace {

using namespace ftxui;

bool ValidBox(const Box& box) {
    return box.x_max >= box.x_min && box.y_max >= box.y_min;
}

const char* TitleFor(NotificationType type) {
    switch (type) {
        case NotificationType::Success: return "Success";
        case NotificationType::Warning: return "Warning";
        case NotificationType::Error: return "Error";
    }
    return "Success";
}

const char* IndicatorFor(NotificationType type) {
    return type == NotificationType::Success ? "✓" : "!";
}

Color AccentFor(const Theme& theme, NotificationType type) {
    switch (type) {
        case NotificationType::Success: return theme.button_success;
        case NotificationType::Warning: return theme.button_warning;
        case NotificationType::Error: return theme.button_danger;
    }
    return theme.button_success;
}

} // namespace

NotificationOverlay::NotificationOverlay(NotificationManager& manager, ftxui::App& app)
    : manager_(manager), app_(app), timer_thread_([this] { TimerLoop(); }) {}

NotificationOverlay::~NotificationOverlay() {
    Stop();
}

void NotificationOverlay::Stop() {
    {
        std::lock_guard<std::mutex> lock(timer_mutex_);
        if (stopped_) return;
        stopped_ = true;
        timer_deadline_.reset();
    }
    timer_condition_.notify_all();
    if (timer_thread_.joinable()) timer_thread_.join();
}

void NotificationOverlay::StateChanged() {
    ScheduleNextExpiration();
    app_.PostEvent(ftxui::Event::Custom);
}

void NotificationOverlay::ScheduleNextExpiration() {
    const auto next = manager_.NextExpiration();
    std::lock_guard<std::mutex> lock(timer_mutex_);
    if (stopped_) return;
    if (next == NotificationManager::Clock::time_point::max()) {
        timer_deadline_.reset();
    } else {
        timer_deadline_ = next;
    }
    timer_condition_.notify_all();
}

void NotificationOverlay::TimerLoop() {
    std::unique_lock<std::mutex> lock(timer_mutex_);
    while (!stopped_) {
        if (!timer_deadline_) {
            timer_condition_.wait(lock, [this] { return stopped_ || timer_deadline_.has_value(); });
            continue;
        }
        const auto deadline = *timer_deadline_;
        const bool changed = timer_condition_.wait_until(lock, deadline, [this, deadline] {
            return stopped_ || !timer_deadline_ || *timer_deadline_ != deadline;
        });
        if (stopped_) return;
        if (changed) continue;
        timer_deadline_.reset();
        lock.unlock();
        app_.PostEvent(ftxui::Event::Custom);
        lock.lock();
    }
}

bool NotificationOverlay::HandleEvent(ftxui::Event event) {
    if (manager_.Tick()) hit_targets_.clear();
    ScheduleNextExpiration();
    if (!event.is_mouse()) return false;

    auto mouse_event = event;
    const ftxui::Mouse& mouse = mouse_event.mouse();
    hovered_close_.reset();
    for (auto iter = hit_targets_.rbegin(); iter != hit_targets_.rend(); ++iter) {
        if (ValidBox(iter->close) && iter->close.Contain(mouse.x, mouse.y)) {
            hovered_close_ = iter->id;
            if (mouse.button == ftxui::Mouse::Left &&
                mouse.motion == ftxui::Mouse::Pressed) {
                manager_.Dismiss(iter->id);
            }
            return true;
        }
        if (ValidBox(iter->toast) && iter->toast.Contain(mouse.x, mouse.y)) {
            return true;
        }
    }
    return false;
}

bool NotificationOverlay::DismissNewest() {
    if (!manager_.DismissNewest()) return false;
    return true;
}

ftxui::Element NotificationOverlay::Render(const Theme& theme) {
    using namespace ftxui;
    manager_.Tick();
    ScheduleNextExpiration();
    hit_targets_.clear();

    const int terminal_width = std::max(1, app_.dimx());
    const int available_width = std::max(1, terminal_width - NotificationLayout::right_margin);
    const auto& notifications = manager_.Visible();
    if (notifications.empty()) return emptyElement();

    Elements items;
    hit_targets_.reserve(NotificationLayout::maximum_visible);
    const size_t first = notifications.size() > NotificationLayout::maximum_visible
        ? notifications.size() - NotificationLayout::maximum_visible : 0;
    for (size_t index = first; index < notifications.size(); ++index) {
        hit_targets_.emplace_back();
        HitTarget& hit_target = hit_targets_.back();
        const Notification& notification = notifications[index];
        hit_target.id = notification.id;
        const int max_width = std::min(NotificationLayout::maximum_width, available_width);
        const int message_width = std::min(
            std::max(1, string_width(notification.message)),
            std::max(1, max_width - 2 * NotificationLayout::horizontal_padding));
        const int desired_width = std::clamp(
            std::max(NotificationLayout::minimum_width,
                     message_width + 2 * NotificationLayout::horizontal_padding),
            1, max_width);
        const int content_width = std::max(
            1, desired_width - 2 * NotificationLayout::horizontal_padding);

        const bool close_hovered = hovered_close_ && *hovered_close_ == notification.id;
        Element close_button = text("×") |
            color(close_hovered ? theme.button_selected_fg : theme.button_cancel);
        if (close_hovered) close_button |= bgcolor(theme.button_focused_bg);
        close_button |= reflect(hit_target.close);

        Element header = hbox({
            text("  "),
            text(IndicatorFor(notification.type)) | color(AccentFor(theme, notification.type)) | bold,
            text(" "),
            text(TitleFor(notification.type)) | color(AccentFor(theme, notification.type)) | bold,
            filler(),
            std::move(close_button),
            text("  "),
        });
        Element message = hbox({
            text("  "),
            paragraph(notification.message) |
                size(WIDTH, EQUAL, content_width) |
                color(theme.modal_text_color),
            text("  "),
        });
        Element toast = vbox({
            text("") | size(HEIGHT, EQUAL, NotificationLayout::vertical_padding),
            std::move(header),
            text("") | size(HEIGHT, EQUAL, NotificationLayout::vertical_spacing),
            std::move(message),
            text("") | size(HEIGHT, EQUAL, NotificationLayout::vertical_padding),
        }) |
            size(WIDTH, EQUAL, desired_width) |
            clear_under |
            bgcolor(theme.modal_background) |
            color(theme.modal_text_color) |
            reflect(hit_target.toast);

        items.push_back(std::move(toast));
        if (index + 1 < notifications.size()) {
            items.push_back(
                text("") | size(HEIGHT, EQUAL, NotificationLayout::vertical_spacing));
        }
    }

    Element stack = vbox(std::move(items));
    // The status bar occupies the last application row. Leave one clear row
    // above it, then anchor the stack to the lower-right without changing the
    // dimensions of the base workspace.
    Element positioned = vbox({
        filler(),
        hbox({filler(), std::move(stack), text("  ")}),
        text("") | size(HEIGHT, EQUAL,
            NotificationLayout::bottom_margin_above_status + 1),
    }) | size(WIDTH, EQUAL, terminal_width) | size(HEIGHT, EQUAL, std::max(1, app_.dimy()));
    return positioned;
}

} // namespace textlt
