#pragma once
#include <fstream>
#include <string>
#include <sstream>
#include <mutex>
#include <ftxui/component/event.hpp>

/**
 * @brief Thread-safe Logger that overwrites 'debug.log' on each start.
 * Supports arbitrary objects if operator<< is overloaded.
 */

// Overload to allow logging of ftxui::Mouse objects
inline std::ostream& operator<<(std::ostream& os, const ftxui::Mouse& m) {
    return os << "Mouse{x=" << m.x << ", y=" << m.y 
              << ", btn=" << (int)m.button << ", mov=" << (int)m.motion << "}";
}

// Overload to allow logging of ftxui::Box objects
inline std::ostream& operator<<(std::ostream& os, const ftxui::Box& b) {
    return os << "Box{x:[" << b.x_min << "," << b.x_max 
              << "], y:[" << b.y_min << "," << b.y_max << "]}";
}

// Overload to allow logging of ftxui::Event objects
inline std::ostream& operator<<(std::ostream& os, ftxui::Event e) {
    if (e.is_mouse()) return os << e.mouse();
    if (e.is_character()) return os << "Key{" << e.character() << "}";
    return os << "Event{...}";
}

class Logger {
public:
    // Get the singleton instance
    static Logger& Get() {
        static Logger instance;
        return instance;
    }

    // Template method to write log entries
    template<typename... Args>
    void Write(const std::string& context, Args&&... args) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::stringstream ss;
        ss << "[" << context << "] ";
        // Use C++17 fold expression to concatenate all arguments
        (ss << ... << std::forward<Args>(args));
        file_ << ss.str() << std::endl;
        file_.flush(); // Ensure immediate write to disk
    }

private:
    // Initialize logger and open file in overwrite mode
    Logger() : file_("debug.log", std::ios::out) {}
    std::ofstream file_;
    std::mutex mutex_;
};

// Macro to automatically capture function name using __FUNCTION__
#define LOG(...) Logger::Get().Write(__FUNCTION__, __VA_ARGS__)