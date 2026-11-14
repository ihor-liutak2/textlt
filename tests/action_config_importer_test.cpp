#include <cstdlib>
#include <filesystem>
#include <string>

#include "actions/action_config_importer.hpp"

using textlt::actions::ActionConfigImporter;
using textlt::actions::ActionConfigImportResult;

namespace {

void Check(bool condition) {
    if (!condition) std::abort();
}

const std::string* Find(const ActionConfigImportResult& result,
    const std::string& name) {
    for (const auto& value : result.values) {
        if (value.name == name) return &value.value;
    }
    return nullptr;
}

} // namespace

int main() {
    const auto env = ActionConfigImporter::Parse(".env.local",
        "# local config\nexport API_URL=https://example.test # endpoint\n"
        "TOKEN=\"secret value\"\nEMPTY=\n");
    Check(env.success && env.values.size() == 3);
    Check(Find(env, "API_URL") && *Find(env, "API_URL") == "https://example.test");
    Check(Find(env, "TOKEN") && *Find(env, "TOKEN") == "secret value");
    Check(Find(env, "EMPTY") && Find(env, "EMPTY")->empty());

    const auto properties = ActionConfigImporter::Parse("app.properties",
        "server.port = 8080\nmessage=hello\\\n world\npath=C:\\\\data\n");
    Check(properties.success && properties.values.size() == 3);
    Check(Find(properties, "server.port") && *Find(properties, "server.port") == "8080");
    Check(Find(properties, "message") && *Find(properties, "message") == "hello world");
    Check(Find(properties, "path") && *Find(properties, "path") == "C:\\data");

    const auto yaml = ActionConfigImporter::Parse("config.yaml",
        "database:\n"
        "  connection:\n"
        "    host: localhost\n"
        "    port: 5432\n"
        "services:\n"
        "  - name: api\n"
        "    url: https://api.test\n"
        "  - name: worker\n"
        "enabled: true # production switch\n");
    Check(yaml.success && yaml.values.size() == 6);
    Check(Find(yaml, "database.connection.host") &&
        *Find(yaml, "database.connection.host") == "localhost");
    Check(Find(yaml, "database.connection.port") &&
        *Find(yaml, "database.connection.port") == "5432");
    Check(Find(yaml, "services.0.name") && *Find(yaml, "services.0.name") == "api");
    Check(Find(yaml, "services.0.url") &&
        *Find(yaml, "services.0.url") == "https://api.test");
    Check(Find(yaml, "services.1.name") && *Find(yaml, "services.1.name") == "worker");
    Check(Find(yaml, "enabled") && *Find(yaml, "enabled") == "true");

    const auto unsupported = ActionConfigImporter::Parse("config.json", "{}");
    Check(!unsupported.success && !unsupported.error.empty());
}
