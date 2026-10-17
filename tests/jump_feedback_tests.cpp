#include "jump_feedback.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
bool text(const sjp::visual::FeedbackItem &item, const char *expected) {
    return std::strcmp(item.text, expected) == 0;
}
sjp::Simulation jump(double lead, bool balance = true, int hill = 0) {
    sjp::Simulation sim(hill, 0);
    sim.resetOriginal(hill, 12345, 98765);
    sim.setWind(0, 0);
    for (int i = 0; i < 20000 && sim.phase != sjp::Phase::Finished; ++i) {
        auto input = sjp::autopilot(sim);
        input.takeoff = lead >= 0 && sim.originalStage == 3 && sim.x > -std::max(1.0, sim.vx) * lead;
        if (!balance)
            input.pitchDelta = 0;
        sim.step(sjp::ORIGINAL_TICK, input);
    }
    require(sim.phase == sjp::Phase::Finished, "Feedback fixture did not finish");
    return sim;
}
} // namespace

int main() {
    using namespace sjp;
    using namespace sjp::visual;
    try {
        const auto early = jump(.42), slightlyEarly = jump(.29), onTime = jump(.21),
                   slightlyLate = jump(.10), late = jump(.02), missed = jump(-1);
        require(text(jumpFeedback(early).takeoff, "Early"), "Early takeoff was not identified");
        require(text(jumpFeedback(slightlyEarly).takeoff, "Slightly early"),
                "Slightly early takeoff was not identified");
        require(text(jumpFeedback(onTime).takeoff, "On time"), "Well-timed takeoff was not identified");
        require(text(jumpFeedback(slightlyLate).takeoff, "Slightly too late"),
                "Slightly late takeoff was not identified");
        require(text(jumpFeedback(late).takeoff, "Too late"), "Late takeoff was not identified");
        require(text(jumpFeedback(missed).takeoff, "No takeoff"), "Missing takeoff was not identified");
        require(text(jumpFeedback(onTime).flight, "Balanced"), "Balanced flight was not identified");
        require(text(jumpFeedback(jump(.21, false)).flight, "Unstable"),
                "Poor flight balance was not identified");
        require(!onTime.result.crashed && text(jumpFeedback(onTime).landing, "Telemark"),
                "Standing telemark was not identified");

        auto fixture = onTime;
        fixture.result.landing = Landing::TwoFoot;
        require(text(jumpFeedback(fixture).landing, "Two-foot"), "Two-foot landing was not identified");
        fixture.result.crashed = true;
        fixture.result.landing = Landing::None;
        require(text(jumpFeedback(fixture).landing, "Unprepared"), "Missing landing preparation was lost");
        fixture.result.landing = Landing::Telemark;
        const std::array<const char *, 5> causes{"Hard impact", "Too wide", "Body unsettled",
                                                 "Skis unsettled", "Low stance"};
        for (int cause = 1; cause <= 5; ++cause) {
            fixture.result.crashKind = cause;
            require(text(jumpFeedback(fixture).landing, causes[size_t(cause - 1)]),
                    "Feedback misidentified the original fall cause");
        }
        fixture.result.beforeTakeoff = true;
        require(text(jumpFeedback(fixture).flight, "--") && text(jumpFeedback(fixture).landing, "Inrun fall"),
                "Inrun fall received misleading flight feedback");
        fixture.result.disqualified = true;
        const auto dq = jumpFeedback(fixture);
        require(text(dq.takeoff, "Start expired") && text(dq.flight, "--") && text(dq.landing, "--"),
                "Start timeout received misleading jump feedback");

        for (int hill = 0; hill < int(HILL_COUNT); ++hill) {
            for (const auto &[lead, label] : std::array<std::pair<double, const char *>, 5>{{
                     {.42, "Early"}, {.29, "Slightly early"}, {.21, "On time"},
                     {.10, "Slightly too late"}, {.02, "Too late"}}}) {
                const auto feedback = jumpFeedback(jump(lead, true, hill));
                require(text(feedback.takeoff, label), "Takeoff timing category differs between venues");
                require(feedback.takeoff.tone == (lead == .21 ? FeedbackTone::Good : FeedbackTone::Improve),
                        "Takeoff feedback has the wrong tone");
            }
            auto sim = jump(.21, true, hill);
            const auto original = sim;
            const auto feedback = jumpFeedback(sim);
            const auto again = jumpFeedback(sim);
            require(std::strcmp(feedback.takeoff.text, again.takeoff.text) == 0 &&
                        std::strcmp(feedback.flight.text, again.flight.text) == 0 &&
                        std::strcmp(feedback.landing.text, again.landing.text) == 0,
                    "Feedback changes for the same jump");
            require(sim.result.total == original.result.total && sim.instability == original.instability &&
                        sim.weatherState().speed == original.weatherState().speed &&
                        sim.recordedInputs().size() == original.recordedInputs().size(),
                    "Feedback changed scores, flight balance, wind or recorded input");
            require(!text(feedback.takeoff, "--") && !text(feedback.takeoff, "No takeoff"),
                    "Takeoff recording could not be read on a venue");
        }
        std::cout << "Jump feedback: timing, balance, all landing causes and 32 venues passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
