#pragma once
#include "physics.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace sjp::visual {
enum class FeedbackTone { Neutral, Good, Improve };
struct FeedbackItem {
    const char *text = "--";
    FeedbackTone tone = FeedbackTone::Neutral;
};
struct JumpFeedback {
    FeedbackItem takeoff, flight, landing;
};

// Presentation only: read the completed jump without advancing any simulation
// or random generator. No diagnostic data is added to scores or save formats.
inline JumpFeedback jumpFeedback(const Simulation &sim) {
    JumpFeedback feedback;
    const auto &result = sim.result;
    if (result.disqualified) {
        feedback.takeoff = {"Start expired", FeedbackTone::Improve};
        return feedback;
    }

    const auto &inputs = sim.recordedInputs();
    const auto takeoff = std::find_if(inputs.begin(), inputs.end(), [](const ReplayTick &tick) {
        return tick.stageBefore == 3 && tick.stageAfter >= 4 && tick.stageAfter <= 7;
    });
    if (takeoff == inputs.end()) {
        feedback.takeoff = {"No takeoff", FeedbackTone::Improve};
    } else if (!sim.frames.empty()) {
        // Recorded frames retain absolute jump time even when the start clears
        // their history. Interpolate at the actual 70 Hz takeoff-input event.
        const double time = (takeoff->counter - 1) * double(ORIGINAL_TICK);
        auto after = std::lower_bound(sim.frames.begin(), sim.frames.end(), time,
                                      [](const Frame &frame, double t) { return frame.elapsed < t; });
        Frame frame;
        if (after == sim.frames.begin())
            frame = *after;
        else if (after == sim.frames.end())
            frame = sim.frames.back();
        else {
            const auto &before = *(after - 1);
            frame =
                interpolateFrame(before, *after, (time - before.elapsed) / (after->elapsed - before.elapsed));
        }
        if (frame.vx > .1) {
            const double remaining = -frame.x / frame.vx;
            feedback.takeoff = remaining > .32   ? FeedbackItem{"Early", FeedbackTone::Improve}
                               : remaining > .25 ? FeedbackItem{"Slightly early", FeedbackTone::Improve}
                               : remaining < .07 ? FeedbackItem{"Too late", FeedbackTone::Improve}
                               : remaining < .14 ? FeedbackItem{"Slightly too late", FeedbackTone::Improve}
                                                 : FeedbackItem{"On time", FeedbackTone::Good};
        }
    }

    if (result.beforeTakeoff) {
        feedback.landing = {"Inrun fall", FeedbackTone::Improve};
        return feedback;
    }
    if (sim.flightDuration > .1) {
        // Same normalized imbalance used by the original style assessment;
        // the public accumulator retains its unmodified pre-contact value.
        const double error = std::clamp(sim.instability / (sim.flightDuration + .1) * 3.5 - .4, 0.0, .35);
        feedback.flight = error < .08   ? FeedbackItem{"Balanced", FeedbackTone::Good}
                          : error < .22 ? FeedbackItem{"Unsteady", FeedbackTone::Improve}
                                        : FeedbackItem{"Unstable", FeedbackTone::Improve};
    }

    if (result.crashed) {
        if (result.landing == Landing::None)
            feedback.landing = {"Unprepared", FeedbackTone::Improve};
        else {
            constexpr std::array<const char *, 6> causes{"Fall",           "Hard impact",    "Too wide",
                                                         "Body unsettled", "Skis unsettled", "Low stance"};
            feedback.landing = {causes[size_t(std::clamp(result.crashKind, 0, 5))], FeedbackTone::Improve};
        }
    } else if (result.landing == Landing::Telemark) {
        feedback.landing = {"Telemark", FeedbackTone::Good};
    } else if (result.landing == Landing::TwoFoot) {
        feedback.landing = {"Two-foot", FeedbackTone::Neutral};
    } else {
        feedback.landing = {"Unprepared", FeedbackTone::Improve};
    }
    return feedback;
}
} // namespace sjp::visual
