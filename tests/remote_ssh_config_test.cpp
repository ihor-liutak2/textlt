#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "remote/remote_ssh_config.hpp"

namespace {

void Expect(bool value, const std::string& message) {
    if (!value) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

} // namespace

int main() {
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "textlt_ssh_config_test";
    std::error_code error_code;
    std::filesystem::remove_all(directory, error_code);
    std::filesystem::create_directories(directory / "conf.d");

    {
        std::ofstream config(directory / "config");
        config << "Host production staging\n"
               << "  HostName shared.example.com\n"
               << "Host *.internal !blocked.internal\n"
               << "Include conf.d/*.conf\n";
    }
    {
        std::ofstream included(directory / "conf.d" / "hosts.conf");
        included << "Host jump-box\n"
                 << "Host=quoted-alias # comment\n"
                 << "Include ../config\n";
    }

    std::string error;
    const std::vector<std::string> hosts =
        textlt::DiscoverSshConfigHosts(directory / "config", error);
    Expect(error.empty(), "SSH config discovery returned an error: " + error);
    const std::vector<std::string> expected = {
        "jump-box", "production", "quoted-alias", "staging",
    };
    Expect(hosts == expected, "SSH config host discovery mismatch.");

    std::filesystem::remove_all(directory, error_code);
    return 0;
}
