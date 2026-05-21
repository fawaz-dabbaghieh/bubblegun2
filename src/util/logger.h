#ifndef BUBBLEGUN_CPP_UTIL_LOGGER_H
#define BUBBLEGUN_CPP_UTIL_LOGGER_H

#include <fstream>
#include <string>
#include <string_view>

#include "Timer.h"

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
        return '[' + get_time() + "] " + std::string(message);
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
