#ifndef BUBBLEGUN_CPP_UTIL_TIMER_H
#define BUBBLEGUN_CPP_UTIL_TIMER_H

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

namespace bubblegun::util {

class Timer {
    using Clock = std::chrono::steady_clock;

    Clock::time_point begin_{Clock::now()};

public:
    void reset() {
        begin_ = Clock::now();
    }

    [[nodiscard]] double elapsed() const {
        return std::chrono::duration_cast<std::chrono::duration<double>>(Clock::now() - begin_).count();
    }
};

inline std::string get_time() {
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
    out << std::put_time(&local_tm, "%Y-%m-%d %H:%M:%S")
        << '.'
        << std::setw(3)
        << std::setfill('0')
        << millis.count();
    return out.str();
}

inline std::string format_elapsed(double seconds) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(3) << seconds << "s";
    return out.str();
}

}  // namespace bubblegun::util

#endif  // BUBBLEGUN_CPP_UTIL_TIMER_H
