#include "original_outrun.h"
#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
using Row=std::unordered_map<std::string,double>;
std::vector<std::string> split(const std::string &line) {
    std::vector<std::string> result;std::stringstream stream(line);
    for(std::string field;std::getline(stream,field,',');)result.push_back(field);
    return result;
}
std::vector<Row> read(const std::string &path) {
    std::ifstream input(path);
    if(!input){std::cerr<<"Cannot open "<<path<<'\n';std::exit(2);}
    std::string line;std::getline(input,line);auto names=split(line);
    if(!names.empty()&&!names.back().empty()&&names.back().back()=='\r')names.back().pop_back();
    std::vector<Row> result;
    while(std::getline(input,line)){
        auto fields=split(line);Row row;
        for(size_t i=0;i<names.size();++i)row[names[i]]=std::stod(fields.at(i));
        result.push_back(std::move(row));
    }
    return result;
}
void verify(double value,const Row &row,const std::string &field,bool integer=false) {
    double expected=integer?row.at(field):float(row.at(field));
    if(value!=expected){
        std::cerr.precision(16);
        std::cerr<<"case "<<row.at("case")<<" type "<<row.at("type")<<" "<<field
                 <<" expected "<<expected<<" got "<<value<<'\n';std::exit(1);
    }
}
void loadPose(sjp::OriginalPose &p,const Row &row) {
    for(size_t i=0;i<18;++i){
        p.angles[i]=float(row.at("input_current"+std::to_string(i)));
        p.angleTargets[i]=float(row.at("input_target"+std::to_string(i)));
        p.angleRates[i]=float(row.at("input_rate"+std::to_string(i)));
    }
    for(size_t i=0;i<4;++i){
        p.offsets[i]=float(row.at("input_offset"+std::to_string(i)));
        p.offsetTargets[i]=float(row.at("input_offset_target"+std::to_string(i)));
        p.offsetRates[i]=float(row.at("input_offset_rate"+std::to_string(i)));
    }
    p.mode=int(row.at("input_mode"));p.modeTime=float(row.at("input_timer"));
    p.bodyFactor=float(row.at("input_body_factor"));p.leadingLeg=int(row.at("leading"));
}
void verifyPose(const sjp::OriginalPose &p,const Row &row){
    for(size_t i=0;i<18;++i){
        verify(p.angles[i],row,"current"+std::to_string(i));
        verify(p.angleTargets[i],row,"target"+std::to_string(i));
        verify(p.angleRates[i],row,"rate"+std::to_string(i));
    }
    for(size_t i=0;i<4;++i){
        verify(p.offsets[i],row,"offset"+std::to_string(i));
        verify(p.offsetTargets[i],row,"offset_target"+std::to_string(i));
        verify(p.offsetRates[i],row,"offset_rate"+std::to_string(i));
    }
    verify(p.mode,row,"mode",true);verify(p.modeTime,row,"timer");verify(p.bodyFactor,row,"body_factor");
}
}
int main(int argc,char **argv){
    std::filesystem::path fixturePath=argc>1?argv[1]:"captures/fidelity-outrun";
    if(std::filesystem::is_directory(fixturePath))fixturePath/="outrun-golden.csv";
    auto rows=read(fixturePath.string());
    for(const auto &row:rows){
        sjp::OriginalPose p;loadPose(p,row);sjp::OriginalRandom random(uint32_t(row.at("seed")));
        int type=int(row.at("type"));
        if(type==0){
            sjp::originalRandomizeFallPose(p,row.at("base"),row.at("span"),row.at("rate_base"),row.at("rate_span"),random);
        }else if(type==1){
            sjp::OriginalLandingAssessment assessment;
            assessment.crashKind=int(row.at("kind"));assessment.roughness=int(row.at("roughness"));
            assessment.perturbations={float(row.at("noise0")),float(row.at("noise1"))};
            assessment.armFactors={int(row.at("arm0")),int(row.at("arm1"))};
            sjp::originalInitializeImpact(p,assessment,row.at("width"),row.at("terrain"));
        }else if(type==3){
            sjp::originalNearSurfaceHeights(p,int(row.at("stage")),row.at("y"),
                                           row.at("surface_ahead"),row.at("slope"));
        }else{
            sjp::OriginalOutrunInput in;
            in.originalStage=int(row.at("stage"));in.crashKind=int(row.at("kind"));in.roughness=int(row.at("roughness"));
            in.fallBounceCount=int(row.at("bounce_count"));
            in.width=row.at("width");in.terrainAngle=row.at("terrain");in.terrainSlope=row.at("slope");in.aheadSlope=row.at("ahead");
            in.vx=row.at("vx");in.vyDown=row.at("vy_down");in.speed=row.at("speed");in.impactAngle=row.at("impact");in.mass=row.at("mass");
            in.existingImpulseX=row.at("existing_impulse_x");in.existingImpulseYUp=row.at("existing_impulse_y");
            in.airborne=bool(row.at("airborne"));in.contactEvent=bool(row.at("contact"));in.fastAdvance=bool(row.at("fast"));
            auto out=sjp::originalOutrunUpdate(p,in,random);
            verify(out.originalStage,row,"stage_out",true);verify(out.pendingLaunch,row,"pending",true);
            verify(out.fallBounceCount,row,"bounce_count_out",true);
            verify(out.impulseX,row,"impulse_x");verify(out.impulseYUp,row,"impulse_y");
            verify(out.impactSoundFrequency,row,"sound",true);
            if(out.pitchChanged!=(row.at("pitch")!=0)){
                std::cerr<<"Original pitch override flag differs\n";return 1;
            }
            if(out.pitchChanged)verify(out.pitch,row,"pitch");
        }
        verifyPose(p,row);verify(random.state,row,"final_seed",true);
    }
    struct StopCase{bool airborne;double speed;bool complete;bool expected;};
    // Boundaries from original CS:c5ed/c7fb REAL80 .01 and its two guards.
    for(const auto &s:std::array<StopCase,8>{{
            {false,0,true,true},{false,.005,true,true},{false,.01,true,true},
            {false,.01001,true,false},{false,.1,true,false},{false,1,true,false},
            {true,0,true,false},{false,0,false,false}}})
        if(sjp::originalOutrunStopped(s.airborne,s.speed,s.complete)!=s.expected){
            std::cerr<<"Original stop condition differs\n";return 1;
        }
    std::cout<<rows.size()<<" original-instruction outrun/impact/random-pose fixtures match exactly.\n";
}
