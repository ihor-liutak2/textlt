#include "remote/remote_http_client.hpp"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <chrono>

#include "remote/remote_command_runner.hpp"

namespace textlt {
namespace {

constexpr const char* kHttpStatusMarker = "TEXTLT_HTTP_STATUS:";
constexpr const char* kUserAgent = "textlt/1.0";

std::filesystem::path MakeTempPath(const std::string& prefix) {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    const auto ticks = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
    const auto thread_hash = std::hash<std::thread::id>{}(std::this_thread::get_id());
    return std::filesystem::temp_directory_path() /
        (prefix + "-" + std::to_string(ticks) + "-" + std::to_string(thread_hash));
}

bool WriteBinaryFile(const std::filesystem::path& path, const std::string& content, std::string& error) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        error = "Cannot write temporary HTTP body file: " + path.string();
        return false;
    }
    file.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!file) {
        error = "Cannot finish writing temporary HTTP body file: " + path.string();
        return false;
    }
    return true;
}

std::string CurlConfigQuote(const std::string& value) {
    std::string quoted = "\"";
    for (char ch : value) {
        switch (ch) {
            case '\\':
                quoted += "\\\\";
                break;
            case '"':
                quoted += "\\\"";
                break;
            case '\n':
                quoted += "\\n";
                break;
            case '\r':
                break;
            case '\t':
                quoted += "\\t";
                break;
            default:
                quoted += ch;
                break;
        }
    }
    quoted += "\"";
    return quoted;
}

bool ContainsControlNewline(const std::string& value) {
    return value.find('\n') != std::string::npos || value.find('\r') != std::string::npos;
}

bool ValidateCurlConfigValue(const std::string& label, const std::string& value, std::string& error) {
    if (ContainsControlNewline(value)) {
        error = label + " cannot contain newline characters.";
        return false;
    }
    return true;
}

std::string BuildCurlConfig(
    const std::string& method,
    const std::string& url,
    const std::vector<std::string>& headers,
    const std::filesystem::path* output_path,
    const std::filesystem::path* upload_file_path,
    const std::filesystem::path* body_file_path,
    int timeout_seconds,
    std::string& error) {
    if (!ValidateCurlConfigValue("HTTP method", method, error) ||
        !ValidateCurlConfigValue("HTTP URL", url, error)) {
        return {};
    }

    std::ostringstream config;
    config << "silent\n";
    config << "show-error\n";
    config << "location\n";
    config << "user-agent = " << CurlConfigQuote(kUserAgent) << "\n";
    config << "connect-timeout = 15\n";
    config << "max-time = " << (timeout_seconds > 0 ? timeout_seconds : 300) << "\n";
    config << "request = " << CurlConfigQuote(method.empty() ? "GET" : method) << "\n";
    config << "url = " << CurlConfigQuote(url) << "\n";

    for (const std::string& header : headers) {
        if (!ValidateCurlConfigValue("HTTP header", header, error)) {
            return {};
        }
        config << "header = " << CurlConfigQuote(header) << "\n";
    }

    if (body_file_path) {
        config << "data-binary = " << CurlConfigQuote("@" + body_file_path->string()) << "\n";
    } else if (upload_file_path) {
        config << "data-binary = " << CurlConfigQuote("@" + upload_file_path->string()) << "\n";
    }

    if (output_path) {
        config << "output = " << CurlConfigQuote(output_path->string()) << "\n";
    }

    config << "write-out = " << CurlConfigQuote(std::string("\\n") + kHttpStatusMarker + "%{http_code}\\n") << "\n";
    return config.str();
}

long ExtractHttpStatus(std::string& output) {
    const std::string marker = kHttpStatusMarker;
    const std::size_t marker_pos = output.rfind(marker);
    if (marker_pos == std::string::npos) {
        return 0;
    }

    const std::size_t status_start = marker_pos + marker.size();
    std::size_t status_end = status_start;
    while (status_end < output.size() && std::isdigit(static_cast<unsigned char>(output[status_end]))) {
        ++status_end;
    }

    long status = 0;
    try {
        status = std::stol(output.substr(status_start, status_end - status_start));
    } catch (...) {
        status = 0;
    }

    std::size_t remove_start = marker_pos;
    if (remove_start > 0 && output[remove_start - 1] == '\n') {
        --remove_start;
    }
    std::size_t remove_end = status_end;
    if (remove_end < output.size() && output[remove_end] == '\r') {
        ++remove_end;
    }
    if (remove_end < output.size() && output[remove_end] == '\n') {
        ++remove_end;
    }
    output.erase(remove_start, remove_end - remove_start);
    return status;
}

std::string JoinError(const RemoteCommandResult& command_result) {
    std::string error;
    if (!command_result.error.empty()) {
        error += command_result.error;
    }
    if (!command_result.output.empty()) {
        if (!error.empty()) {
            error += "\n";
        }
        error += command_result.output;
    }
    return error;
}

} // namespace

RemoteHttpResponse RemoteHttpClient::Request(
    const std::string& method,
    const std::string& url,
    const std::vector<std::string>& headers,
    const std::string& request_body,
    int timeout_seconds) const {
    const std::string* body = request_body.empty() ? nullptr : &request_body;
    return RunCurl(method, url, headers, nullptr, nullptr, body, timeout_seconds);
}

RemoteHttpResponse RemoteHttpClient::Download(
    const std::string& method,
    const std::string& url,
    const std::vector<std::string>& headers,
    const std::filesystem::path& output_path,
    const std::string& request_body,
    int timeout_seconds) const {
    const std::string* body = request_body.empty() ? nullptr : &request_body;
    return RunCurl(method, url, headers, &output_path, nullptr, body, timeout_seconds);
}

RemoteHttpResponse RemoteHttpClient::UploadFile(
    const std::string& method,
    const std::string& url,
    const std::vector<std::string>& headers,
    const std::filesystem::path& input_path,
    int timeout_seconds) const {
    return RunCurl(method, url, headers, nullptr, &input_path, nullptr, timeout_seconds);
}

RemoteHttpResponse RemoteHttpClient::RunCurl(
    const std::string& method,
    const std::string& url,
    const std::vector<std::string>& headers,
    const std::filesystem::path* output_path,
    const std::filesystem::path* upload_file_path,
    const std::string* request_body,
    int timeout_seconds) const {
    RemoteHttpResponse response;
    std::string executable_error;
    if (!CheckCurlExecutable(executable_error)) {
        response.error = executable_error;
        return response;
    }

    std::error_code fs_error;
    if (output_path) {
        const std::filesystem::path parent = output_path->parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent, fs_error);
            if (fs_error) {
                response.error = "Cannot create local directory: " + parent.string() + "\n" + fs_error.message();
                return response;
            }
        }
    }
    if (upload_file_path && !std::filesystem::exists(*upload_file_path, fs_error)) {
        response.error = "Cannot read upload file: " + upload_file_path->string();
        return response;
    }

    std::filesystem::path body_path;
    const std::filesystem::path* body_path_ptr = nullptr;
    if (request_body) {
        body_path = MakeTempPath("textlt-remote-http-body");
        if (!WriteBinaryFile(body_path, *request_body, response.error)) {
            return response;
        }
        body_path_ptr = &body_path;
    }

    std::string config_error;
    const std::string config = BuildCurlConfig(
        method,
        url,
        headers,
        output_path,
        upload_file_path,
        body_path_ptr,
        timeout_seconds,
        config_error);
    if (!config_error.empty()) {
        response.error = config_error;
        if (!body_path.empty()) {
            std::filesystem::remove(body_path, fs_error);
        }
        return response;
    }

    RemoteCommandRunner runner;
    RemoteCommandResult command_result = runner.Run({"curl", "--config", "-"}, config);
    response.body = command_result.output;
    response.status_code = ExtractHttpStatus(response.body);

    if (!body_path.empty()) {
        std::filesystem::remove(body_path, fs_error);
    }

    response.ok = command_result.exit_code == 0 && response.status_code >= 200 && response.status_code < 300;
    if (!response.ok) {
        response.error = command_result.error;
        if (response.error.empty() && command_result.exit_code != 0) {
            response.error = "curl exited with code " + std::to_string(command_result.exit_code) + ".";
        }
        if (output_path) {
            std::filesystem::remove(*output_path, fs_error);
        }
        if (response.error.empty() && response.status_code == 0) {
            response.error = JoinError(command_result);
        }
    }

    return response;
}

std::string RemoteHttpClient::UrlEncode(const std::string& value) {
    std::ostringstream encoded;
    encoded << std::uppercase << std::hex << std::setfill('0');
    for (unsigned char ch : value) {
        if ((ch >= 'A' && ch <= 'Z') ||
            (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            encoded << static_cast<char>(ch);
        } else {
            encoded << '%' << std::setw(2) << static_cast<int>(ch);
        }
    }
    return encoded.str();
}

bool RemoteHttpClient::CheckCurlExecutable(std::string& error) {
    RemoteCommandRunner runner;
    const RemoteCommandResult result = runner.Run({"curl", "--version"});
    if (result.exit_code == 0) {
        error.clear();
        return true;
    }
    error = "The external curl executable was not found or cannot be started.\n"
            "Install it on Debian/Ubuntu/MX Linux with:\n"
            "  sudo apt install curl";
    if (!result.error.empty()) {
        error += "\n" + result.error;
    }
    return false;
}

} // namespace textlt
