#pragma once
#include <algorithm>
#include <chrono>
#include <cmath>

namespace sjp::visual {
class FrameSchedule {
  public:
    using Clock = std::chrono::steady_clock;
    static constexpr std::chrono::nanoseconds period{1000000000 / 60};

    explicit FrameSchedule(Clock::time_point start, int fps = 60)
        : deadline(start), interval(fps > 0 ? std::chrono::nanoseconds(1000000000 / fps)
                                          : std::chrono::nanoseconds::zero()) {}

    Clock::time_point nextDeadline(Clock::time_point now) {
        // Keep ordinary timer overshoot out of the next frame's deadline.
        if (interval == std::chrono::nanoseconds::zero())
            return now;
        deadline += interval;
        // After a stall, resume here instead of submitting a burst to catch up.
        if (now - deadline >= interval)
            deadline = now;
        return deadline;
    }

  private:
    Clock::time_point deadline;
    std::chrono::nanoseconds interval;
};

inline double cameraBlend(double elapsed) {
    // Retain the existing 16% response at 60 FPS, with the same response time
    // when frames are irregular or a display runs at another refresh rate.
    return elapsed > 0 ? -std::expm1(std::log(.84) * 60 * std::min(elapsed, .08)) : 0;
}
} // namespace sjp::visual
