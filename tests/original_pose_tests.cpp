#include "original_pose.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

int main(int argc, char **argv) {
    std::ifstream input(argc > 1 ? argv[1] : "captures/fidelity-controls/pose-golden.csv");
    if (!input) { std::cerr << "Missing original pose golden CSV\n"; return 1; }
    std::string line;
    std::getline(input, line);
    sjp::OriginalPose pose;
    size_t cases = 0;
    while (std::getline(input, line)) {
        std::vector<std::string> columns;
        std::istringstream row(line);
        std::string value;
        while (std::getline(row, value, ',')) columns.push_back(value);
        if (columns.size() != 34) { std::cerr << "Bad fixture row\n"; return 1; }
        auto number = [&](size_t i) { return std::stod(columns[i]); };
        if (number(1)) pose.reset(number(6));
        pose.leadingLeg = int(number(9));
        for (int i = 0; i < int(number(3)); ++i)
            pose.update(int(number(2)), number(4), number(5), 1.0/70, number(6), bool(number(7)), number(8));
        if (pose.mode != int(number(10)) || pose.bodyFactor != number(11)) {
            std::cerr << columns[0] << ": mode/factor mismatch\n"; return 1;
        }
        for (size_t i = 0; i < 22; ++i) {
            double actual = i < 4 ? pose.offsets[i] : pose.angles[i-4];
            if (std::abs(actual-number(12+i)) > 1e-6) {
                std::cerr << columns[0] << ": original pose component " << i << " mismatch: "
                          << actual << " vs " << number(12+i) << '\n'; return 1;
            }
        }
        cases++;
    }
    // Starting mode2 runs before grounded targets; zero-rate mode-table
    // entries must not erase the slope-joint slew installed by 0x2e033.
    pose.reset(-.5);
    pose.prepare(2, 0, 0, -.5, false);
    if (pose.angleTargets[8] != float(double(float(-.5))/3+.2) || pose.angleRates[8] != 1 ||
        pose.angleRates[1] != 3 || pose.angleRates[10] != 3) {
        std::cerr << "Start erased original grounded joint targets/rates\n";
        return 1;
    }
    pose.advance(1.0/70);
    if (pose.angles[8] != float(1.0/70)) {
        std::cerr << "Grounded slope joint failed to advance on the start tick\n";
        return 1;
    }
    // Contact 0x26d25 adds width, but 0x26d5f skips the landing-target callback.
    // prepare must preserve the three landing targets until assessment, while
    // still updating the foot targets for the newly grounded surface.
    pose.reset(-.5);
    pose.prepare(7, -.3, 0, -.3, true);
    pose.advance(1.0/70);
    pose.prepare(8, .75, .2, -.3, true);
    const auto beforeTargets = pose.angleTargets;
    const auto beforeAngles = pose.angles;
    pose.prepare(8, .75, .2 + 1.0/70, -.4, false, 0, false);
    for (size_t i : {size_t(2), size_t(4), size_t(11)}) {
        if (pose.angleTargets[i] != beforeTargets[i]) {
            std::cerr << "Contact unexpectedly refreshed landing target " << i << '\n';
            return 1;
        }
    }
    if (pose.angles != beforeAngles || pose.angleTargets[1] == beforeTargets[1]) {
        std::cerr << "Contact prepare ordering/grounded foot target mismatch\n";
        return 1;
    }
    // When a landing click coincides with contact, source 0x26c63 has already
    // refreshed flight targets with that tick's mouse/noise input. Mode6's
    // negative leg rates retain them, and 0x26d5f skips landing targets.
    pose.reset(-.5);
    pose.prepare(7, .6, 0, -.3, true);
    // Actual DOS Finland standing run: frame630 mode5/time4.000010967;
    // frame631 landing click mode6 preserves that clock before interpolation.
    pose.modeTime = 4.0000109672546387f;
    pose.flightTargets(-.6);
    pose.prepare(9, .75, 1.0/70, -.4, false, 0, false, true);
    if (pose.modeTime != 4.0000109672546387f) {
        std::cerr << "Landing click incorrectly reset the original pose timer\n";
        return 1;
    }
    for (size_t i : {size_t(2), size_t(11)}) {
        if (pose.angleTargets[i] != float(.64 + double(float(-.6)))) {
            std::cerr << "Same-tick landing/contact retained stale flight target " << i << '\n';
            return 1;
        }
    }
    pose.advance(1.0/70);
    if (pose.modeTime != 4.014296531677246f) {
        std::cerr << "Landing pose clock differs from DOS standing frame631\n";
        return 1;
    }
    // Actual DOS CPU telemark: frame663 advances the width before freezing
    // preparation. Its targets use that final width and the current slope.
    pose.reset(-.5);
    pose.prepare(7, -.3, 0, -.7415032386779785, true);
    pose.prepare(8, .75, .34285715222358704, -.7415032386779785, true);
    pose.prepare(9, .75, .3571428656578064, -.7411330342292786, true);
    if (pose.angleTargets[2] != 1.362209677696228f ||
        pose.angleTargets[4] != 1.0705171823501587f ||
        pose.angleTargets[11] != .8264954090118408f) {
        std::cerr << "Telemark freeze retained targets from the preceding width tick\n";
        return 1;
    }
    // Unprepared contacts also set stage9 through the width/contact branch;
    // they retain the original inrun or flight mode for impact assessment.
    for (int priorStage : {3, 7}) {
        pose.reset(-.5);
        pose.prepare(priorStage, -.3, 0, -.3, priorStage == 7);
        const int beforeMode = pose.mode;
        const auto originalLegTargets = pose.angleTargets;
        pose.prepare(9, -.3, 1.0/70, -.4, false, 0, false, false);
        if (pose.mode != beforeMode || pose.angleTargets[2] != originalLegTargets[2] ||
            pose.angleTargets[11] != originalLegTargets[11]) {
            std::cerr << "Unprepared contact incorrectly initiated a landing from stage " << priorStage << '\n';
            return 1;
        }
    }
    std::cout << "PASS: " << cases << " original-derived pose fixtures (330 components), contact target ordering\n";
    return cases == 15 ? 0 : 1;
}
