#include "original_ai.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
std::vector<std::string> split(const std::string &line) {
    std::vector<std::string> fields;
    std::istringstream stream(line);
    std::string item;
    while (std::getline(stream, item, ',')) {
        if (!item.empty() && item.back() == '\r') item.pop_back();
        fields.push_back(item);
    }
    return fields;
}
class Csv {
  public:
    explicit Csv(const std::string &path) : stream(path) {
        std::string line;
        if (!stream || !std::getline(stream, line)) return;
        const auto header = split(line);
        for (size_t i = 0; i < header.size(); ++i) index[header[i]] = i;
    }
    bool next() {
        std::string line;
        if (!std::getline(stream, line)) return false;
        row = split(line);
        return true;
    }
    double get(const std::string &name) const { return std::stod(row.at(index.at(name))); }
    bool check(const std::string &field, double actual) const {
        if (actual != get(field)) {
            std::cerr << row[0] << ": " << field << " " << actual << " vs " << get(field) << '\n';
            return false;
        }
        return true;
    }
  private:
    std::ifstream stream;
    std::vector<std::string> row;
    std::unordered_map<std::string, size_t> index;
};
bool checkState(const Csv &csv, const sjp::OriginalAi &ai, const std::string &prefix) {
    return csv.check(prefix+"left",ai.leftTakeoffTime) && csv.check(prefix+"right",ai.rightTakeoffTime) &&
        csv.check(prefix+"delay",ai.pitchDelay) && csv.check(prefix+"duration",ai.pitchDuration) &&
        csv.check(prefix+"remaining",ai.pitchRemaining) && csv.check(prefix+"target",ai.targetPitch) &&
        csv.check(prefix+"lead",ai.landingLead) && csv.check(prefix+"width",ai.landingWidth) &&
        csv.check(prefix+"assess",ai.assessImpactOnce) && csv.check(prefix+"unusual",ai.unusualLanding);
}
} // namespace

int main(int argc, char **argv) {
    const std::string directory = argc > 1 ? argv[1] : "captures/fidelity-ai";
    Csv profile(argc >= 4 ? argv[1] : directory+"/ai-skills-golden.csv");
    size_t profiles = 0;
    while (profile.next()) {
        sjp::OriginalRandom random(uint32_t(profile.get("seed")));
        int temporary = int(profile.get("temporary_level"));
        auto skills = sjp::generateOriginalAiSkills(int(profile.get("selected")),random,
                                                   profile.get("double_team") ? &temporary : nullptr);
        if (!profile.check("generated_seed",random.state) || !profile.check("resolved_level",skills.level) ||
            !profile.check("updated_temporary",temporary) || skills.values[0] != 0 || skills.values[1] != 0) return 1;
        sjp::refreshOriginalAiHillForm(skills,random);
        for (size_t i = 0; i < 4; ++i)
            if (!profile.check("value_"+std::to_string(i),skills.values[i])) return 1;
        if (!profile.check("refreshed_seed",random.state)) return 1;
        ++profiles;
    }
    Csv initializer(argc >= 4 ? argv[2] : directory+"/ai-initializer-golden.csv");
    size_t initializers = 0;
    while (initializer.next()) {
        sjp::OriginalAiSkills skills;
        for (size_t i = 0; i < 4; ++i) skills.values[i] = float(initializer.get("value_"+std::to_string(i)));
        skills.level = int(initializer.get("level"));
        sjp::OriginalRandom random(uint32_t(initializer.get("seed")));
        sjp::OriginalAi ai;
        ai.initialize(skills,int(initializer.get("hill_k")),random);
        if (!checkState(initializer,ai,"") || !initializer.check("final_seed",random.state)) return 1;
        ++initializers;
    }
    Csv poll(argc >= 4 ? argv[3] : directory+"/ai-poll-golden.csv");
    size_t polls = 0;
    std::array<bool, 3> mouseDirections{};
    while (poll.next()) {
        sjp::OriginalAi ai;
        ai.leftTakeoffTime=float(poll.get("in_left"));ai.rightTakeoffTime=float(poll.get("in_right"));
        ai.pitchDelay=float(poll.get("in_delay"));ai.pitchDuration=float(poll.get("in_duration"));
        ai.pitchRemaining=float(poll.get("in_remaining"));ai.targetPitch=float(poll.get("in_target"));
        ai.landingLead=float(poll.get("in_lead"));ai.landingWidth=float(poll.get("in_width"));
        ai.assessImpactOnce=bool(poll.get("in_assess"));ai.unusualLanding=bool(poll.get("in_unusual"));
        sjp::OriginalAiObservation obs;
        obs.tick=uint32_t(poll.get("counter"));obs.stage=int(poll.get("stage"));
        obs.q=poll.get("q");obs.telemarkWidth=poll.get("tele_width");
        obs.distanceToTakeoff=poll.get("distance");obs.speed=poll.get("speed");obs.motionAngle=poll.get("angle");
        obs.vx=poll.get("vx");obs.vyDown=poll.get("vy_down");obs.clearance=poll.get("clearance");
        obs.landingSlope=poll.get("slope");obs.terrainAngleAhead=poll.get("angle_ahead");
        sjp::OriginalRandom global(uint32_t(poll.get("seed")));
        const auto result=ai.poll(obs,global);
        if (result.mouseDy >= -1 && result.mouseDy <= 1)
            mouseDirections[size_t(result.mouseDy+1)] = true;
        if (!checkState(poll,ai,"out_") || !poll.check("mouse_dy",result.mouseDy) ||
            !poll.check("button_mask",result.buttonMask) || !poll.check("seed",global.state)) return 1;
        ++polls;
    }
    if (!mouseDirections[0] || !mouseDirections[1] || !mouseDirections[2]) {
        std::cerr << "Original AI fixtures do not cover every mouse direction\n";
        return 1;
    }
    std::cout << "PASS: " << profiles << " original skill profiles, " << initializers << " AI initializers, " << polls
              << " input polls; exact float32 states, seeded initialization and preserved poll seed\n";
    return profiles == 240 && initializers == 500 && polls == 743 ? 0 : 1;
}
