#include "original_controls.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

static std::vector<std::string> split(const std::string &line) {
    std::vector<std::string> fields;
    std::istringstream stream(line);
    std::string item;
    while (std::getline(stream, item, ',')) {
        if (!item.empty() && item.back() == '\r') item.pop_back();
        fields.push_back(item);
    }
    return fields;
}

int main(int argc, char **argv) {
    std::ifstream input(argc > 1 ? argv[1] : "captures/fidelity-controls/controls-golden.csv");
    if (!input) { std::cerr << "Missing controls golden CSV\n"; return 1; }
    std::string line;
    std::getline(input, line);
    const auto header = split(line);
    std::unordered_map<std::string, size_t> index;
    for (size_t i = 0; i < header.size(); ++i) index[header[i]] = i;
    size_t count = 0;
    while (std::getline(input, line)) {
        const auto row = split(line);
        auto get = [&](const std::string &name) { return std::stod(row.at(index.at(name))); };
        sjp::OriginalPose pose;
        pose.footTrims = {float(get("in_trim0")), float(get("in_trim1"))};
        pose.auxiliary = {float(get("in_aux0")), float(get("in_aux1"))};
        sjp::OriginalFlightStyle style;
        style.integral = float(get("in_integral"));
        style.duration = float(get("in_duration"));
        style.landingTimer = float(get("in_timer"));
        double q = get("in_q");
        sjp::OriginalRandom random(uint32_t(get("in_seed")));
        style.accumulate(int(get("stage")), bool(get("airborne")), q, pose, get("dt"));
        const double delta = sjp::originalMousePitchDelta(int(get("mouse_dy")), get("dt")) + get("direct_delta");
        sjp::updateOriginalFlightInput(int(get("stage")), q, delta, get("dt"), get("wind_speed"),
                                     get("wind_angle"), random, pose);
        auto check = [&](const std::string &field, double actual) {
            // DWORD fields must match exactly; double delta has no original
            // store, so permit only the final double arithmetic rounding.
            const double tolerance = field == "expected_pitch_delta" ? 1e-15 : 0;
            if (std::abs(actual - get(field)) > tolerance) {
                std::cerr << row[0] << " tick " << row[1] << ": " << field << " "
                          << actual << " vs " << get(field) << '\n';
                return false;
            }
            return true;
        };
        if (!check("expected_pitch_delta", delta) || !check("out_q", q) ||
            !check("out_trim0", pose.footTrims[0]) || !check("out_trim1", pose.footTrims[1]) ||
            !check("out_aux0", pose.auxiliary[0]) || !check("out_aux1", pose.auxiliary[1]) ||
            !check("out_integral", style.integral) || !check("out_duration", style.duration) ||
            !check("out_timer", style.landingTimer) || !check("out_seed", random.state)) return 1;
        ++count;
    }
    std::cout << "PASS: " << count << " original controls/style fixtures, exact DWORD fields and RNG state\n";
    return count == 1568 ? 0 : 1;
}
