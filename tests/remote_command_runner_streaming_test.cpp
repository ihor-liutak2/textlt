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
    if (argc > 1 && std::string(argv[1]) == "--child-active-stream") {
        for (int index = 0; index < 15; ++index) {
            std::cout << "x" << std::flush;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return 0;
    }
    if (argc > 1 && std::string(argv[1]) == "--child-stalled-stream") {
        std::cout << "first" << std::flush;
        std::this_thread::sleep_for(std::chrono::seconds(2));
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
    assert(stopped_result.cancelled);
    assert(stopped_result.exit_code == 130);
    assert(elapsed < std::chrono::seconds(3));

    textlt::RemoteCommandControl control;
    std::thread direct_stop_thread([&] {
        while (!control.IsRunning()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        control.RequestStop();
    });
    const auto direct_started = std::chrono::steady_clock::now();
    const textlt::RemoteCommandResult direct_stopped_result = runner.RunStreaming(
        {argv[0], "--child-slow"},
        {},
        nullptr,
        {},
        textlt::RemoteCommandOptions{},
        &control);
    direct_stop_thread.join();
    assert(direct_stopped_result.cancelled);
    assert(direct_stopped_result.exit_code == 130);
    assert(std::chrono::steady_clock::now() - direct_started < std::chrono::seconds(3));

    textlt::RemoteCommandControl pre_cancelled_control;
    pre_cancelled_control.RequestStop();
    const auto pre_cancelled_started = std::chrono::steady_clock::now();
    const textlt::RemoteCommandResult pre_cancelled_result = runner.RunStreaming(
        {argv[0], "--child-slow"},
        {},
        nullptr,
        {},
        textlt::RemoteCommandOptions{},
        &pre_cancelled_control);
    assert(pre_cancelled_result.cancelled);
    assert(pre_cancelled_result.exit_code == 130);
    assert(!pre_cancelled_control.IsRunning());
    assert(std::chrono::steady_clock::now() - pre_cancelled_started <
           std::chrono::seconds(3));

    pre_cancelled_control.Reset();
    const textlt::RemoteCommandResult reused_control_result = runner.RunStreaming(
        {argv[0], "--child-output"},
        {},
        nullptr,
        {},
        textlt::RemoteCommandOptions{},
        &pre_cancelled_control);
    assert(reused_control_result.exit_code == 0);
    assert(!reused_control_result.cancelled);
    assert(reused_control_result.output == "first second");

    const textlt::RemoteCommandResult active_stream_result = runner.RunStreaming(
        {argv[0], "--child-active-stream"},
        {},
        nullptr,
        {},
        textlt::RemoteCommandOptions{0, 100, true, 1});
    assert(active_stream_result.exit_code == 0);
    assert(!active_stream_result.timed_out);
    assert(active_stream_result.output.size() == 15);

    const auto idle_timeout_started = std::chrono::steady_clock::now();
    const textlt::RemoteCommandResult idle_timeout_result = runner.RunStreaming(
        {argv[0], "--child-stalled-stream"},
        {},
        nullptr,
        {},
        textlt::RemoteCommandOptions{0, 100, true, 1});
    assert(idle_timeout_result.timed_out);
    assert(idle_timeout_result.exit_code == 124);
    assert(idle_timeout_result.error.find("without output") != std::string::npos);
    assert(std::chrono::steady_clock::now() - idle_timeout_started <
           std::chrono::seconds(3));

    const auto timeout_started = std::chrono::steady_clock::now();
    const textlt::RemoteCommandResult timeout_result = runner.RunStreaming(
        {argv[0], "--child-slow"},
        {},
        nullptr,
        {},
        textlt::RemoteCommandOptions{1, 100, true});
    assert(timeout_result.timed_out);
    assert(timeout_result.exit_code == 124);
    assert(timeout_result.error.find("timed out") != std::string::npos);
    assert(std::chrono::steady_clock::now() - timeout_started < std::chrono::seconds(3));
    return 0;
}
