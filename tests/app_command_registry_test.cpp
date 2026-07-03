#include "app_command_registry.hpp"

#include <cassert>
#include <string>

int main() {
    textlt::AppCommandRegistry registry;
    int runs = 0;

    textlt::AppCommand command;
    command.id = "test.run";
    command.title = "Run Test";
    command.category = "Test";
    command.shortcut = "Ctrl+T";
    command.run = [&runs] { ++runs; };

    assert(registry.Register(command));
    assert(!registry.Register(command));
    assert(registry.Has("test.run"));
    assert(registry.Enabled("test.run"));
    assert(registry.Run("test.run"));
    assert(runs == 1);

    const textlt::AppCommand* found = registry.Find("test.run");
    assert(found != nullptr);
    assert(found->title == "Run Test");
    assert(found->shortcut == "Ctrl+T");

    textlt::AppCommand disabled;
    disabled.id = "test.disabled";
    disabled.title = "Disabled";
    disabled.category = "Test";
    disabled.enabled = [] { return false; };
    disabled.run = [&runs] { runs += 10; };

    assert(registry.Register(disabled));
    assert(!registry.Enabled("test.disabled"));
    assert(!registry.Run("test.disabled"));
    assert(runs == 1);

    assert(!registry.Run("missing.command"));
    assert(registry.Commands().size() == 2);

    return 0;
}
