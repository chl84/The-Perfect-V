#include "presentation_timing.h"
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
double follow(const std::vector<double> &cadence) {
    double elapsed = 0, position = 0;
    for (size_t i = 0; elapsed < .5 - 1e-12; ++i) {
        const double dt = std::min(cadence[i % cadence.size()], .5 - elapsed);
        position += (1 - position) * sjp::visual::cameraBlend(dt);
        elapsed += dt;
    }
    return position;
}
} // namespace

int main() {
    try {
        using Schedule = sjp::visual::FrameSchedule;
        using namespace std::chrono;
        const Schedule::Clock::time_point start{};
        Schedule schedule(start);
        auto now = start;
        // Reproduce a timer waking 250 us late on every frame, with varying
        // render work. The lateness must not accumulate into a sub-60 FPS beat.
        for (int frame = 0; frame < 600; ++frame) {
            const auto completed = now + microseconds(2500 + frame % 7 * 900);
            const auto deadline = schedule.nextDeadline(completed);
            require(deadline == start + Schedule::period * (frame + 1),
                    "Timer overshoot shifted the next frame deadline");
            now = deadline + microseconds(250);
        }
        require(now == start + Schedule::period * 600 + microseconds(250),
                "Repeated sleep overshoot must not accumulate");
        now += milliseconds(120);
        require(schedule.nextDeadline(now) == now,
                "A long stall must discard missed frame deadlines");
        require(schedule.nextDeadline(now + milliseconds(3)) == now + Schedule::period,
                "The frame after a stall must resume normal pacing");
        for (int fps : {30, 60, 120}) {
            Schedule limited(start, fps);
            const nanoseconds interval(1000000000 / fps);
            auto completed = start;
            for (int frame = 1; frame <= 240; ++frame) {
                const auto next = limited.nextDeadline(completed + microseconds(300));
                require(next == start + interval * frame, "Selected FPS limit drifted");
                completed = next;
            }
        }
        Schedule unlimited(start, 0);
        require(unlimited.nextDeadline(start + milliseconds(1)) == start + milliseconds(1) &&
                    unlimited.nextDeadline(start + seconds(1)) == start + seconds(1),
                "Unlimited rendering introduced a frame delay");

        require(std::abs(sjp::visual::cameraBlend(1. / 60) - .16) < 1e-12,
                "Normal 60 FPS camera response changed");
        require(sjp::visual::cameraBlend(0) == 0 && sjp::visual::cameraBlend(-1) == 0,
                "Zero or backwards time must not move the camera");
        const double at60 = follow({1. / 60});
        for (const auto &cadence : {std::vector<double>{1. / 30}, {1. / 120}, {1. / 144},
                                   {.004, .013, .007, .021, .005}})
            require(std::abs(follow(cadence) - at60) < 1e-12,
                    "Camera response depends on the frame rate or its irregularity");
        std::cout << "Stable frame deadlines, stall recovery and time-based camera response passed.\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
