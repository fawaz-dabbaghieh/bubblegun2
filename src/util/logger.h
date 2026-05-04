#ifndef BUBBLEGUN_CPP_UTIL_LOGGER_H
#define BUBBLEGUN_CPP_UTIL_LOGGER_H

#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>

namespace bubblegun::util {

class Logger {
public:
    Logger() = default;

    explicit Logger(const std::string& path) {
        open(path);
    }

    bool open(const std::string& path) {
        stream_.open(path, std::ios::out | std::ios::trunc);
        return stream_.good();
    }

    bool enabled() const {
        return stream_.is_open();
    }

    void info(std::string_view message) {
        write(message);
    }

    void error(std::string_view message) {
        write(message);
    }

private:
    static std::string timestamp(std::string_view message) {
        using namespace std::chrono;
        const auto now = system_clock::now();
        const auto seconds = system_clock::to_time_t(now);
        const auto millis = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

        std::tm local_tm{};
#if defined(_WIN32)
        localtime_s(&local_tm, &seconds);
#else
        localtime_r(&seconds, &local_tm);
#endif

        std::ostringstream out;
        out << '['
            << std::put_time(&local_tm, "%Y-%m-%d %H:%M:%S")
            << ','
            << std::setw(3)
            << std::setfill('0')
            << millis.count()
            << "] "
            << message;
        return out.str();
    }

    void write(std::string_view message) {
        if (!stream_.is_open()) {
            return;
        }
        stream_ << timestamp(message) << '\n';
        stream_.flush();
    }

    std::ofstream stream_;
};

}  // namespace bubblegun::util

#endif  // BUBBLEGUN_CPP_UTIL_LOGGER_H
