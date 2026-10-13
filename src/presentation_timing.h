#pragma once
#include <algorithm>
#include <chrono>
#include <cmath>

namespace sjp::visual {
class FrameSchedule {
  public:
    using Clock = std::chrono::steady_clock;
    static constexpr std::chrono::nanoseconds period{1000000000 / 60};

    explicit FrameSchedule(Clock::time_point start) : deadline(start) {}

    Clock::time_point nextDeadline(Clock::time_point now) {
        // Keep ordinary timer overshoot out of the next frame's deadline.
        deadline += period;
        // After a stall, resume here instead of submitting a burst to catch up.
        if (now - deadline >= period)
            deadline = now;
        return deadline;
    }

  private:
    Clock::time_point deadline;
};

inline double cameraBlend(double elapsed) {
    // Retain the existing 16% response at 60 FPS, with the same response time
    // when frames are irregular or a display runs at another refresh rate.
    return elapsed > 0 ? -std::expm1(std::log(.84) * 60 * std::min(elapsed, .08)) : 0;
}
} // namespace sjp::visual
