#include "original_replay.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unistd.h>

namespace {
void require(bool condition, const std::string &message) { if(!condition)throw std::runtime_error(message); }
std::vector<uint8_t> bytes(const std::string &path) {
    std::ifstream file(path,std::ios::binary);
    return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
std::vector<std::string> split(const std::string &line) {
    std::istringstream stream(line);std::string item;std::vector<std::string> out;
    while(std::getline(stream,item,','))out.push_back(item);
    return out;
}
void checkActualRuntime(const std::string &csv,const sjp::OriginalReplayData &data) {
    std::ifstream file(csv);require(bool(file),"Cannot read actual DOS playback fixture");
    std::string line;std::getline(file,line);auto columns=split(line);
    std::unordered_map<std::string,size_t> index;
    for(size_t i=0;i<columns.size();++i)index[columns[i]]=i;
    sjp::Simulation s;s.resetWithWeather(data.hill,data.weather,data.seed);size_t count=0;
    while(std::getline(file,line)) {
        auto row=split(line);require(row.size()==columns.size(),"Incomplete original playback CSV");
        s.step(sjp::ORIGINAL_TICK,sjp::originalReplayInput(data,s.originalCounter+1));
        auto equal=[&](const std::string &field,double actual){
            double original=std::stod(row[index.at(field)]);
            require(original==actual,"Actual DOS replay differs at counter "+std::to_string(s.originalCounter)+" in "+field);
        };
        equal("frame_counter",s.originalCounter);equal("control_phase",s.originalStage);
        equal("x",float(s.x+s.hill().inrunParameters[3]));equal("y",float(s.y+s.hill().takeoffAltitude()));
        equal("vx",s.vx);equal("vy_down",-s.vy);equal("q",s.pitch);
        equal("wind_speed",s.windSpeed);equal("wind_angle",s.windAngle);
        equal("pose_mode",s.pose.mode);equal("pose_time",s.pose.modeTime);
        equal("body_force_factor",s.pose.bodyFactor);equal("leading_leg",s.pose.leadingLeg);
        const auto &w=s.weatherState();
        equal("wind_x",w.x);equal("wind_y",w.y);equal("wind_variation_step",w.variationStep);
        equal("wind_visual_angle",w.displayAngle);equal("wind_angular_step",w.angularStep);equal("wind_angular_cap",w.angularCap);
        equal("landing_width",s.telemarkWidth);equal("distance",s.result.distance);equal("total",s.result.total);
        for(size_t i=0;i<18;++i){
            equal("angle_"+std::to_string(i),s.pose.angles[i]);
            equal("angle_target_"+std::to_string(i),s.pose.angleTargets[i]);
            equal("angle_rate_"+std::to_string(i),s.pose.angleRates[i]);
        }
        for(size_t i=0;i<4;++i){
            equal("offset_"+std::to_string(i),s.pose.offsets[i]);
            equal("offset_target_"+std::to_string(i),s.pose.offsetTargets[i]);
            equal("offset_rate_"+std::to_string(i),s.pose.offsetRates[i]);
        }
        for(size_t i=0;i<5;++i)equal("judge_"+std::to_string(i),s.result.judges[i]);
        ++count;
    }
    require(count==1130&&s.originalCounter==1131&&s.phase==sjp::Phase::Finished,"Actual original playback must reach its complete physical stop");
}
}
int main(int argc,char **argv) {
    auto directory=std::filesystem::temp_directory_path()/("sjp-original-replay-tests-"+std::to_string(getpid()));
    std::filesystem::create_directories(directory);
    try {
        require(argc==3,"Usage: original-replay-tests EXAMPLE.RPL original-runtime.csv");
        const auto sample=bytes(argv[1]);
        sjp::OriginalReplayData data;std::string error;
        require(sjp::readOriginalReplay(argv[1],data,&error),error);
        require(data.hill==0&&data.seed==0x2b19081c,"Original one-based Finland ID and RNG seed");
        require(data.athleteName=="EXAMPLE"&&data.date=="2000/08/27"&&data.author=="DEMO","Original metadata");
        require(data.recordedDistance==109.25&&data.startCounter==65&&data.takeoffCounter==336&&data.flightCounter==349&&data.rightLandingCounter==672&&data.impactCounter==693,"Original event timeline");
        require(data.takeoffButtons[0]==2&&data.takeoffButtons[12]==2&&data.takeoffButtons[13]==0,"Original summed button counts");
        require(sjp::originalReplayInput(data,64).buttonMask==0&&sjp::originalReplayInput(data,65).buttonMask==1&&
                sjp::originalReplayInput(data,336).buttonMask==3&&sjp::originalReplayInput(data,349).buttonMask==0&&
                sjp::originalReplayInput(data,672).buttonMask==2,"Input replay preserves original start, takeoff and landing polling");
        std::vector<uint8_t> encoded;
        require(sjp::encodeOriginalReplay(data,encoded,&error)&&encoded==sample,"Imported original replay round-trips byte for byte");
        require(sjp::saveOriginalReplay((directory/"roundtrip.rpl").string(),data,&error),error);
        require(bytes((directory/"roundtrip.rpl").string())==sample,"File exporter preserves original checksum and opaque padding");
        auto corrupt=sample;corrupt[55]^=1;sjp::OriginalReplayData untouched;untouched.hill=31;
        require(!sjp::decodeOriginalReplay(corrupt,untouched,&error)&&untouched.hill==31,"Bad checksum rejected without changing output");
        corrupt=sample;corrupt.pop_back();require(!sjp::decodeOriginalReplay(corrupt,untouched,&error),"Truncated replay rejected");
        corrupt=sample;corrupt.push_back(0);require(!sjp::decodeOriginalReplay(corrupt,untouched,&error),"Trailing data rejected");
        auto invalid=data;invalid.hill=32;require(!sjp::encodeOriginalReplay(invalid,encoded,&error),"Invalid hill rejected");
        invalid=data;invalid.weather.x=std::numeric_limits<float>::quiet_NaN();require(!sjp::encodeOriginalReplay(invalid,encoded,&error),"NaN weather rejected");
        invalid=data;invalid.flightCounter=0xffffffffu;require(!sjp::encodeOriginalReplay(invalid,encoded,&error),"Invalid event timeline rejected");
        invalid=data;invalid.takeoffButtons[3]=3;require(!sjp::encodeOriginalReplay(invalid,encoded,&error),"Invalid summed buttons rejected");
        invalid=data;invalid.author=std::string(26,'x');require(!sjp::encodeOriginalReplay(invalid,encoded,&error),"Oversized metadata rejected");
        sjp::Replay replay;sjp::OriginalReplayMetadata metadata;
        require(sjp::loadOriginalReplay(argv[1],replay,&metadata,&error),error);
        require(metadata.distanceMatches&&metadata.stopCounter==1131&&replay.result.distance==109&&
                replay.result.total==double(float(126.6))&&!replay.result.crashed,"Original example complete reconstruction");
        checkActualRuntime(argv[2],data);
        sjp::Simulation simulation;simulation.resetWithWeather(data.hill,data.weather,data.seed);
        while(simulation.phase!=sjp::Phase::Finished&&simulation.originalCounter<20000)
            simulation.step(sjp::ORIGINAL_TICK,sjp::originalReplayInput(data,simulation.originalCounter+1));
        sjp::OriginalReplayData exported;
        require(sjp::buildOriginalReplay(simulation,exported,&error),error);
        require(exported.recordedDistance==109.25,"Live export preserves original hundredth-metre distance metadata");
        require(sjp::saveOriginalReplay((directory/"live-export.rpl").string(),exported,&error),error);
        sjp::Replay restored;
        require(sjp::loadOriginalReplay((directory/"live-export.rpl").string(),restored,nullptr,&error),error);
        require(restored.result.distance==replay.result.distance&&restored.result.total==replay.result.total&&
                restored.result.judges==replay.result.judges&&restored.result.crashed==replay.result.crashed,"Live original input export reproduces distance, judges, points and landing");
        // Native format/reconstruction coverage for all source-derived profiles.
        // This loop does not claim actual DOS runtime coverage outside the demo.
        for(int hill=0;hill<32;++hill) {
            auto variant=data;variant.hill=hill;sjp::Replay run;
            require(sjp::reconstructOriginalReplay(variant,run,nullptr,&error),"Native RPL reconstruction failed for original hill "+std::to_string(hill)+": "+error);
            require(std::isfinite(run.result.total)&&std::isfinite(run.result.distance)&&run.frames.size()>2,"Native all-hill replay must complete with finite results");
            variant.recordedDistance=run.result.distance;
            require(sjp::encodeOriginalReplay(variant,encoded,&error),error);
            sjp::OriginalReplayData decoded;
            require(sjp::decodeOriginalReplay(encoded,decoded,&error)&&decoded.hill==hill,"Original hill ID encode/decode round trip");
        }
        sjp::Simulation partial;require(!sjp::buildOriginalReplay(partial,exported,&error),"Incomplete jump cannot be exported");
        std::filesystem::remove_all(directory);
        std::cout<<"Original RPL import/export, malformed input rejection, live recording round-trip, 32 native hill variants, and 1130 actual DOS playback ticks passed.\n";
        return 0;
    } catch(const std::exception &e) {
        std::filesystem::remove_all(directory);std::cerr<<e.what()<<'\n';return 1;
    }
}
