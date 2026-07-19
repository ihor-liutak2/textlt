#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#include "remote/remote_command_runner.hpp"

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--child-output") {
        std::cout << "first" << std::flush;
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        std::cout << " second" << std::flush;
        return 0;
    }
    if (argc > 1 && std::string(argv[1]) == "--child-slow") {
        for (int index = 0; index < 100; ++index) {
            std::cout << "x" << std::flush;
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
        }
        return 0;
    }

    textlt::RemoteCommandRunner runner;
    std::string streamed;
    const textlt::RemoteCommandResult output_result = runner.RunStreaming(
        {argv[0], "--child-output"},
        {},
        nullptr,
        [&](const std::string& chunk) { streamed += chunk; });
    assert(output_result.exit_code == 0);
    assert(output_result.output == "first second");
    assert(streamed == output_result.output);

    std::atomic<bool> cancel_requested{false};
    std::thread cancel_thread([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        cancel_requested.store(true);
    });
    const auto started = std::chrono::steady_clock::now();
    const textlt::RemoteCommandResult stopped_result = runner.RunStreaming(
        {argv[0], "--child-slow"},
        {},
        &cancel_requested,
        {});
    cancel_thread.join();
    const auto elapsed = std::chrono::steady_clock::now() - started;
    assert(stopped_result.exit_code != 0);
    assert(elapsed < std::chrono::seconds(3));
    return 0;
}
