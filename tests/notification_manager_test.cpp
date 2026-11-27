#include "notification_manager.hpp"

#include <chrono>

using namespace std::chrono_literals;

#define REQUIRE(condition) do { if (!(condition)) return __LINE__; } while (false)

int main() {
    using Manager = textlt::NotificationManager;
    const Manager::Clock::time_point start{};
    Manager manager;

    const auto success = manager.Success("File renamed.", start);
    const auto warning = manager.Warning("Already exists.", start + 100ms);
    const auto error = manager.Error("Delete failed.", start + 200ms);
    REQUIRE(manager.Visible().size() == 3);
    REQUIRE(manager.Visible()[0].id == success);
    REQUIRE(manager.Visible()[0].type == textlt::NotificationType::Success);
    REQUIRE(manager.Visible()[1].id == warning);
    REQUIRE(manager.Visible()[1].type == textlt::NotificationType::Warning);
    REQUIRE(manager.Visible()[2].id == error);
    REQUIRE(manager.Visible()[2].type == textlt::NotificationType::Error);

    const auto queued = manager.Success("Queued.", start + 300ms);
    REQUIRE(manager.QueuedCount() == 1);
    REQUIRE(manager.Visible()[0].expires_at == start + 1000ms);
    REQUIRE(manager.Visible()[1].expires_at == start + 1100ms);
    REQUIRE(manager.Visible()[2].expires_at == start + 1200ms);

    // Expiration is independent. The queued item begins its own duration only
    // when it becomes visible.
    REQUIRE(manager.Tick(start + 1000ms));
    REQUIRE(manager.Visible().size() == 3);
    REQUIRE(manager.Visible().back().id == queued);
    REQUIRE(manager.Visible().back().expires_at == start + 2000ms);
    REQUIRE(manager.Dismiss(warning, start + 1050ms));
    REQUIRE(manager.Visible().size() == 2);
    REQUIRE(manager.DismissNewest(start + 1050ms));
    REQUIRE(manager.Visible().size() == 1);
    REQUIRE(!manager.Dismiss(queued, start + 1050ms));

    REQUIRE(manager.Tick(start + 1200ms));
    REQUIRE(manager.Visible().empty());
    REQUIRE(manager.Empty());

    // Each severity has an independent centralized duration.
    textlt::NotificationConfig config;
    config.success_duration = 5ms;
    config.warning_duration = 10ms;
    config.error_duration = 15ms;
    Manager configured(config);
    configured.Success("short", start);
    configured.Warning("medium", start);
    configured.Error("long", start);
    REQUIRE(configured.Tick(start + 5ms));
    REQUIRE(configured.Visible().size() == 2);
    REQUIRE(configured.Visible().front().type == textlt::NotificationType::Warning);
}
