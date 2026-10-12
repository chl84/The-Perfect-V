#include "physics.h"
#include "original_replay.h"
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace sjp {
// Read-only access for the DOS runtime comparison; no per-tick state injection.
struct RuntimeComparisonAccess {
    static std::vector<std::pair<std::string, double>> fields(const Simulation &s) {
        const auto &w = s.windModel;
        std::vector<std::pair<std::string, double>> f = {
            {"frame_counter", s.originalCounter}, {"control_phase", s.originalStage},
            {"countdown", s.countdown}, {"start_hold_ticks", s.startHoldTicks},
            {"fall_bounce_count", s.fallBounceCount},
            {"x", float(s.x+s.hill().inrunParameters[3])},
            {"y", float(s.y+s.hill().takeoffAltitude())}, {"vx", s.vx}, {"vy_down", -s.vy},
            {"surface_speed", s.scalarSpeed}, {"q", s.pitch}, {"hill_angle", s.groundAngle},
            {"work", s.inrunWork}, {"start_y", s.inrunStartY}, {"initial_speed", s.inrunInitialSpeed},
            {"impulse_x", s.impulseX}, {"impulse_y", s.impulseY},
            {"flight_flag_byte", s.phase == Phase::Flight || s.outrunAirborne},
            {"launch_pending", s.pendingLaunch}, {"rng_state", s.randomState},
            {"wind_x", w.x}, {"wind_y", w.y}, {"wind_variation_step", w.variationStep},
            {"wind_speed", s.windSpeed}, {"wind_angle", s.windAngle},
            {"wind_visual_angle", w.displayAngle}, {"wind_angular_step", w.angularStep},
            {"wind_angular_cap", w.angularCap}, {"wind_cosmetic", w.weatherIntensity},
            {"pose_mode", s.pose.mode}, {"pose_time", s.pose.modeTime},
            {"body_force_factor", s.pose.bodyFactor}, {"leading_leg", s.pose.leadingLeg},
            {"trim_left", s.pose.footTrims[0]}, {"trim_right", s.pose.footTrims[1]},
            {"aux_left", s.pose.auxiliary[0]}, {"aux_right", s.pose.auxiliary[1]},
            {"flight_integral", s.flightStyle.integral}, {"flight_duration", s.flightStyle.duration},
            {"landing_timer", s.flightStyle.landingTimer}, {"takeoff_duration", s.takeoffDuration},
            {"landing_width", s.telemarkWidth}, {"impact_speed", s.impactSpeed},
            {"impact_angle", s.impactAngle}, {"crash_kind", s.impactAssessment.crashKind},
            {"roughness", s.impactAssessment.roughness}, {"distance", s.result.distance},
            {"landing_complete", s.phase==Phase::Outrun || s.phase==Phase::Finished},
            {"style_points", s.result.stylePoints}, {"total", s.result.total},
            {"native_phase", int(s.phase)}, {"native_elapsed", s.elapsed},
            {"ai_left", s.ai.leftTakeoffTime}, {"ai_right", s.ai.rightTakeoffTime},
            {"ai_delay", s.ai.pitchDelay}, {"ai_duration", s.ai.pitchDuration},
            {"ai_remaining", s.ai.pitchRemaining}, {"ai_target", s.ai.targetPitch},
            {"ai_lead", s.ai.landingLead}, {"ai_width", s.ai.landingWidth},
            {"ai_assess", s.ai.assessImpactOnce}, {"ai_unusual", s.ai.unusualLanding},
            {"ai_button_mask", s.lastAiInput.buttonMask}, {"ai_mouse_dy", s.lastAiInput.mouseDy}
        };
        for (size_t i = 0; i < s.pose.angles.size(); ++i) {
            f.emplace_back("angle_"+std::to_string(i), s.pose.angles[i]);
            f.emplace_back("angle_target_"+std::to_string(i), s.pose.angleTargets[i]);
            f.emplace_back("angle_rate_"+std::to_string(i), s.pose.angleRates[i]);
        }
        for (size_t i = 0; i < s.pose.offsets.size(); ++i) {
            f.emplace_back("offset_"+std::to_string(i), s.pose.offsets[i]);
            f.emplace_back("offset_target_"+std::to_string(i), s.pose.offsetTargets[i]);
            f.emplace_back("offset_rate_"+std::to_string(i), s.pose.offsetRates[i]);
        }
        for (size_t i = 0; i < s.result.judges.size(); ++i)
            f.emplace_back("judge_"+std::to_string(i), s.result.judges[i]);
        return f;
    }
};
}
namespace {
template <class T> T read(const std::vector<char> &ds, size_t address) {
    T value;
    if (address + sizeof(T) > ds.size()) throw std::runtime_error("Incomplete initial DOS data segment");
    std::memcpy(&value, ds.data()+address, sizeof(T));
    return value;
}
void emit(const sjp::Simulation &s, bool header = false) {
    auto fields = sjp::RuntimeComparisonAccess::fields(s);
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i) std::cout << ',';
        if (header) std::cout << fields[i].first;
        else std::cout << fields[i].second;
    }
    std::cout << '\n';
}
}
int main(int argc,char **argv) {
    try {
        if(argc!=2)throw std::runtime_error("Usage: original-replay-probe input.RPL");
        sjp::OriginalReplayData data;std::string error;
        if(!sjp::readOriginalReplay(argv[1],data,&error))throw std::runtime_error(error);
        sjp::Simulation simulation;simulation.resetWithWeather(data.hill,data.weather,data.seed);
        std::cout<<std::setprecision(17);emit(simulation,true);
        while(simulation.phase!=sjp::Phase::Finished&&simulation.phase!=sjp::Phase::Disqualified&&simulation.originalCounter<20000) {
            simulation.step(sjp::ORIGINAL_TICK,sjp::originalReplayInput(data,simulation.originalCounter+1));
            emit(simulation);
        }
        if(simulation.phase!=sjp::Phase::Finished)throw std::runtime_error("Original replay did not finish");
        return 0;
    } catch(const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}
