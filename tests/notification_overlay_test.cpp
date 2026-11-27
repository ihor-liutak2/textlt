#include "notification_manager.hpp"
#include "notification_overlay.hpp"
#include "theme.hpp"

#include "ftxui/component/component.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"

#include <iostream>
#include <string>
#include <utility>

int main() {
    using namespace ftxui;

    App app = App::FixedSize(64, 20);
    textlt::NotificationManager manager;
    textlt::NotificationOverlay overlay(manager, app);
    manager.SetOnChanged([&] { overlay.StateChanged(); });

    manager.Success("Expires without user input.");
    Screen frame(64, 20);
    Render(frame, overlay.Render(textlt::FallbackTheme()));
    const std::string frame_text = frame.ToString();
    if (frame_text.find("Expires without user input.") == std::string::npos) {
        std::cerr << frame_text << '\n';
        return 1;
    }
    manager.DismissNewest();

    auto renderer = Renderer([&] {
        auto element = overlay.Render(textlt::FallbackTheme());
        if (manager.Empty()) app.Exit();
        return element;
    });
    auto root = CatchEvent(renderer, [&](Event event) {
        return overlay.HandleEvent(std::move(event));
    });

    manager.Success("Expires without user input.");
    app.Loop(root);
    if (!manager.Empty()) return 2;
    overlay.Stop();
}
