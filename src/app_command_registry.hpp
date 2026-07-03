#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace textlt {

struct AppCommand {
    std::string id;
    std::string title;
    std::string category;
    std::string shortcut;
    std::function<bool()> enabled;
    std::function<void()> run;
};

class AppCommandRegistry {
public:
    bool Register(AppCommand command);
    bool Has(const std::string& id) const;
    bool Enabled(const std::string& id) const;
    bool Run(const std::string& id) const;
    const AppCommand* Find(const std::string& id) const;
    std::vector<AppCommand> Commands() const;

private:
    std::unordered_map<std::string, AppCommand> commands_;
};

} // namespace textlt
