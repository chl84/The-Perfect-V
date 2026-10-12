#include "original_scoring.h"
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
    while (std::getline(stream,item,',')) {
        if (!item.empty() && item.back() == '\r') item.pop_back();
        fields.push_back(item);
    }
    return fields;
}

int main(int argc,char **argv) {
    std::ifstream input(argc>1?argv[1]:"captures/fidelity-controls/scoring-golden.csv");
    if (!input) { std::cerr<<"Missing scoring golden CSV\n"; return 1; }
    std::string line;std::getline(input,line);auto header=split(line);
    std::unordered_map<std::string,size_t> index;
    for(size_t i=0;i<header.size();++i)index[header[i]]=i;
    size_t count=0;
    while(std::getline(input,line)) {
        auto row=split(line);
        auto get=[&](const std::string &name){return std::stod(row.at(index.at(name)));};
        sjp::OriginalLandingState state;
        state.impactSpeed=get("impact_speed");state.impactAngle=get("impact_angle");state.telemarkWidth=get("width");
        state.bodyAngle=get("body_angle");state.bodyTarget=get("body_target");state.leg1Angle=get("leg1_angle");state.leg1Target=get("leg1_target");
        state.sideAngle1=get("side1");state.sideAngle2=get("side2");state.height1=get("height1");state.height2=get("height2");
        state.takeoffDuration=get("takeoff_duration");state.flightErrorIntegral=get("flight_integral");state.flightDuration=get("flight_duration");
        state.beforeTakeoff=bool(get("before_board"));state.leadingLeg=int(get("leading_leg"));
        sjp::OriginalRandom random(uint32_t(get("seed")));
        auto result=sjp::assessOriginalLanding(state,random);
        auto check=[&](const std::string &field,double actual){
            if(std::abs(actual-get(field))>2e-5){std::cerr<<row[0]<<": "<<field<<" "<<actual<<" vs "<<get(field)<<'\n';return false;}return true;
        };
        for(size_t i=0;i<5;++i)if(!check("penalty_"+std::to_string(i),result.penalties[i])||!check("judge_"+std::to_string(i),result.judges[i]))return 1;
        if(!check("mean",result.mean)||!check("crash_kind",result.crashKind)||!check("roughness",result.roughness)||
           !check("perturbation_0",result.perturbations[0])||!check("perturbation_1",result.perturbations[1])||
           !check("arm_factor_0",result.armFactors[0])||!check("arm_factor_1",result.armFactors[1])||
           !check("normalized_flight",result.normalizedFlightError)||!check("style_points",result.stylePoints)||
           !check("random_final_state",random.state))return 1;
        count++;
    }
    std::cout<<"PASS: "<<count<<" original landing/judge fixtures, including seeded RNG state\n";
    return count==12?0:1;
}
