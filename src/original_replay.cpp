#include "original_replay.h"
#include <algorithm>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

namespace sjp {
namespace {
constexpr size_t RECORD_SIZE = 0x4b5, MAGIC_SIZE = 13, FILE_SIZE = RECORD_SIZE + MAGIC_SIZE + 4;
constexpr char MAGIC[] = "//DSJ_v2.00//";
constexpr uint32_t FILE_SEED = 0x26b51b17u;
bool failure(std::string *error, const char *message) {
    if (error) *error = message;
    return false;
}
uint32_t read32(const uint8_t *bytes) {
    return uint32_t(bytes[0]) | uint32_t(bytes[1]) << 8 | uint32_t(bytes[2]) << 16 | uint32_t(bytes[3]) << 24;
}
void write32(uint8_t *bytes, uint32_t value) {
    for (size_t i = 0; i < 4; ++i) bytes[i] = uint8_t(value >> (8*i));
}
float readFloat(const uint8_t *bytes) {
    uint32_t raw = read32(bytes);
    float value;
    std::memcpy(&value, &raw, 4);
    return value;
}
void writeFloat(uint8_t *bytes, float value) {
    uint32_t raw;
    std::memcpy(&raw, &value, 4);
    write32(bytes, raw);
}
// Source 0x31f60: Random($8000)*65536 + Random($ffff).
uint32_t randomLong(OriginalRandom &random) {
    return uint32_t(random.next(32768)) * 65536u + uint32_t(random.next(65535));
}
std::vector<uint8_t> crypt(const std::vector<uint8_t> &input, bool encode, uint32_t &checksum) {
    OriginalRandom random(FILE_SEED);
    checksum = randomLong(random);
    std::vector<uint8_t> output;
    output.reserve(input.size());
    for (uint8_t value : input) {
        uint8_t key = uint8_t(random.next(256));
        uint8_t encoded = encode ? uint8_t(value + key) : value;
        output.push_back(encode ? encoded : uint8_t(value - key));
        random.state += encoded;
        uint32_t salt = randomLong(random) >> 8;
        checksum += uint32_t(int(encoded) - 1) * salt;
    }
    return output;
}
bool pascalString(const uint8_t *record, size_t offset, size_t capacity, std::string &value) {
    size_t length = record[offset];
    if (length > capacity) return false;
    value.assign(reinterpret_cast<const char *>(record + offset + 1), length);
    for (unsigned char ch : value) if (ch < 32 || ch == 127) return false;
    return true;
}
void putString(uint8_t *record, size_t offset, size_t capacity, const std::string &value) {
    record[offset] = uint8_t(value.size());
    std::copy(value.begin(), value.end(), record + offset + 1);
    // Deliberately retain unused original padding bytes.
    (void)capacity;
}
bool valid(const OriginalReplayData &data, std::string *error) {
    if (data.hill < 0 || size_t(data.hill) >= HILL_COUNT)
        return failure(error, "Replay hill is outside the original 32-hill catalog.");
    const auto &w = data.weather;
    for (float value : {w.x,w.y,w.variationStep,w.speed,w.angle,w.displayAngle,w.angularStep,w.angularCap})
        if (!std::isfinite(value)) return failure(error, "Replay weather contains a non-finite number.");
    if (std::abs(w.x)>20 || std::abs(w.y)>20 || w.speed<0 || w.speed>20 || w.variationStep<0 ||
        w.variationStep>10 || std::abs(w.angle)>100 || std::abs(w.displayAngle)>100 ||
        std::abs(w.angularStep)>10 || w.angularCap<0 || w.angularCap>10 ||
        !std::isfinite(data.cameraDistance) || std::abs(data.cameraDistance)>20000)
        return failure(error, "Replay weather or camera value is outside supported limits.");
    if (!std::isfinite(data.recordedDistance) || data.recordedDistance<0 || data.recordedDistance>1000)
        return failure(error, "Replay recorded distance is invalid.");
    if (data.startCounter<65 || data.startCounter>1120 || data.impactCounter<data.startCounter ||
        data.impactCounter>20000 || data.takeoffCounter>data.impactCounter ||
        (data.takeoffCounter && data.takeoffCounter<data.startCounter+20) ||
        data.flightCounter>data.impactCounter ||
        (data.flightCounter && data.flightCounter<data.takeoffCounter) ||
        (data.leftLandingCounter && (data.leftLandingCounter<data.flightCounter || data.leftLandingCounter>data.impactCounter)) ||
        (data.rightLandingCounter && (data.rightLandingCounter<data.flightCounter || data.rightLandingCounter>data.impactCounter)) ||
        (data.flightCounter && data.impactCounter-data.flightCounter>=data.mouseCounts.size()))
        return failure(error, "Replay event counters are inconsistent or exceed the original input capacity.");
    for (uint8_t buttons : data.takeoffButtons)
        if (buttons>2) return failure(error, "Replay takeoff button count is invalid.");
    for (const auto &text : {data.athleteName,data.date,data.author})
        for (unsigned char ch : text) if (ch<32 || ch==127) return failure(error, "Replay metadata contains control characters.");
    if (data.athleteName.size()>25 || data.date.size()>10 || data.author.size()>25)
        return failure(error, "Replay metadata exceeds the original field capacity.");
    return true;
}
}
bool readOriginalReplay(const std::string &path, OriginalReplayData &output, std::string *error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return failure(error, "Cannot open original replay file.");
    std::vector<uint8_t> encoded(FILE_SIZE);
    file.read(reinterpret_cast<char *>(encoded.data()), encoded.size());
    if (file.gcount()!=std::streamsize(FILE_SIZE) || file.peek()!=std::char_traits<char>::eof())
        return failure(error, "Original replay must contain exactly 1222 bytes.");
    return decodeOriginalReplay(encoded,output,error);
}
bool decodeOriginalReplay(const std::vector<uint8_t> &bytes, OriginalReplayData &output, std::string *error) {
    if(bytes.size()!=FILE_SIZE)return failure(error,"Original replay must contain exactly 1222 bytes.");
    auto encoded=bytes;
    uint32_t expected = read32(encoded.data()+FILE_SIZE-4), checksum;
    encoded.resize(FILE_SIZE-4);
    auto plain = crypt(encoded, false, checksum);
    if (checksum!=expected) return failure(error, "Original replay checksum is invalid.");
    if (!std::equal(plain.begin(), plain.begin()+MAGIC_SIZE, MAGIC))
        return failure(error, "Unsupported original replay version.");
    OriginalReplayData data;
    std::copy(plain.begin()+MAGIC_SIZE, plain.end(), data.originalRecord.begin());
    const auto *p = data.originalRecord.data();
    auto &w = data.weather;
    w.x=readFloat(p);w.y=readFloat(p+4);w.variationStep=readFloat(p+8);w.speed=readFloat(p+12);
    w.angle=readFloat(p+16);w.displayAngle=readFloat(p+20);w.angularStep=readFloat(p+24);w.angularCap=readFloat(p+28);
    w.weatherIntensity=p[0x449];
    data.hill=int(p[0x20])-1;data.cameraDistance=readFloat(p+0x21);data.seed=read32(p+0x2c);
    std::string distance;
    if (!pascalString(p,0x25,6,distance) || !pascalString(p,0x476,25,data.athleteName) ||
        !pascalString(p,0x490,10,data.date) || !pascalString(p,0x49b,25,data.author))
        return failure(error, "Original replay string field is malformed.");
    std::istringstream number(distance);
    if (!(number>>data.recordedDistance) || number.peek()!=std::char_traits<char>::eof())
        return failure(error, "Original replay distance field is malformed.");
    data.startCounter=read32(p+0x44a);data.takeoffCounter=read32(p+0x44e);data.flightCounter=read32(p+0x452);
    data.leftLandingCounter=read32(p+0x46a);data.rightLandingCounter=read32(p+0x46e);data.impactCounter=read32(p+0x472);
    std::copy(p+0x456,p+0x46a,data.takeoffButtons.begin());
    for (size_t i=0;i<data.mouseCounts.size();++i) data.mouseCounts[i]=int8_t(p[0x30+i]);
    if (!valid(data,error)) return false;
    output=std::move(data);
    if (error) error->clear();
    return true;
}
bool encodeOriginalReplay(const OriginalReplayData &data, std::vector<uint8_t> &bytes, std::string *error) {
    if (!valid(data,error)) return false;
    auto record=data.originalRecord;
    auto *p=record.data();
    const auto &w=data.weather;
    size_t offset=0;
    for (float value:{w.x,w.y,w.variationStep,w.speed,w.angle,w.displayAngle,w.angularStep,w.angularCap}) {
        writeFloat(p+offset,value);offset+=4;
    }
    p[0x20]=uint8_t(data.hill+1);writeFloat(p+0x21,data.cameraDistance);write32(p+0x2c,data.seed);
    std::ostringstream distance;distance<<std::fixed<<std::setprecision(2)<<data.recordedDistance;
    if (distance.str().size()>6) return failure(error,"Replay distance exceeds the original six-character field.");
    putString(p,0x25,6,distance.str());
    for(size_t i=0;i<data.mouseCounts.size();++i)p[0x30+i]=uint8_t(data.mouseCounts[i]);
    p[0x449]=w.weatherIntensity;write32(p+0x44a,data.startCounter);write32(p+0x44e,data.takeoffCounter);write32(p+0x452,data.flightCounter);
    std::copy(data.takeoffButtons.begin(),data.takeoffButtons.end(),p+0x456);
    write32(p+0x46a,data.leftLandingCounter);write32(p+0x46e,data.rightLandingCounter);write32(p+0x472,data.impactCounter);
    putString(p,0x476,25,data.athleteName);putString(p,0x490,10,data.date);putString(p,0x49b,25,data.author);
    std::vector<uint8_t> plain(MAGIC,MAGIC+MAGIC_SIZE);
    plain.insert(plain.end(),record.begin(),record.end());
    uint32_t checksum;
    auto encoded=crypt(plain,true,checksum);
    encoded.resize(FILE_SIZE);write32(encoded.data()+FILE_SIZE-4,checksum);
    bytes=std::move(encoded);if(error)error->clear();return true;
}
bool saveOriginalReplay(const std::string &path, const OriginalReplayData &data, std::string *error) {
    std::vector<uint8_t> encoded;
    if(!encodeOriginalReplay(data,encoded,error))return false;
    std::ofstream file(path+".tmp",std::ios::binary);
    if (!file) return failure(error,"Cannot create original replay file.");
    file.write(reinterpret_cast<const char *>(encoded.data()),encoded.size());file.close();
    if (!file) return failure(error,"Cannot write original replay file.");
    std::error_code ec;std::filesystem::rename(path+".tmp",path,ec);
    if (ec) return failure(error,"Cannot replace original replay file.");
    if (error) error->clear();
    return true;
}
Input originalReplayInput(const OriginalReplayData &data, uint32_t counter) {
    Input input;
    int buttons=counter==data.startCounter?1:0;
    if (data.takeoffCounter && counter>=data.takeoffCounter && counter-data.takeoffCounter<20) {
        uint8_t count=data.takeoffButtons[counter-data.takeoffCounter];
        buttons=count==2?3:count;
    }
    if (data.leftLandingCounter && counter==data.leftLandingCounter)buttons|=1;
    if (data.rightLandingCounter && counter==data.rightLandingCounter)buttons|=2;
    input.buttonMask=buttons;
    input.start=counter==data.startCounter;
    if (data.flightCounter && counter>=data.flightCounter && counter<=data.impactCounter &&
        counter-data.flightCounter<data.mouseCounts.size())
        input.mouseDy=data.mouseCounts[counter-data.flightCounter];
    return input;
}
bool reconstructOriginalReplay(const OriginalReplayData &data, Replay &output,
                               OriginalReplayMetadata *metadata, std::string *error) {
    if (!valid(data,error)) return false;
    Simulation simulation(data.hill,0);
    simulation.resetWithWeather(data.hill,data.weather,data.seed);
    while(simulation.phase!=Phase::Finished && simulation.phase!=Phase::Disqualified && simulation.originalCounter<20000)
        simulation.step(ORIGINAL_TICK,originalReplayInput(data,simulation.originalCounter+1));
    if (simulation.phase!=Phase::Finished || simulation.frames.size()<2)
        return failure(error,"Original replay did not reach a complete physical stop.");
    for (const auto &f:simulation.frames)
        if(!std::isfinite(f.x)||!std::isfinite(f.y)||!std::isfinite(f.vx)||!std::isfinite(f.vy))
            return failure(error,"Original replay reconstruction produced invalid physics.");
    output=simulation.replay();
    if(metadata) {
        metadata->athleteName=data.athleteName;metadata->date=data.date;metadata->author=data.author;
        metadata->recordedDistance=data.recordedDistance;metadata->seed=data.seed;
        metadata->impactCounter=data.impactCounter;metadata->stopCounter=simulation.originalCounter;
        metadata->distanceMatches=std::abs(output.result.distance-std::floor(data.recordedDistance*2)/2)<1e-6;
    }
    if(error)error->clear();
    return true;
}
bool loadOriginalReplay(const std::string &path, Replay &output, OriginalReplayMetadata *metadata, std::string *error) {
    OriginalReplayData data;
    return readOriginalReplay(path,data,error)&&reconstructOriginalReplay(data,output,metadata,error);
}
void OriginalReplayRecording::begin(int hill, const OriginalWind &weather, uint32_t seed,
                                    const std::string &athleteName, const std::string &author) {
    data={};data.hill=hill;data.weather=weather;data.seed=seed;
    data.athleteName=athleteName.substr(0,25);data.author=author.substr(0,25);
    ticks.clear();active=true;
}
void OriginalReplayRecording::capture(uint32_t counter,int beforeStage,int afterStage,const Input &input,
                                      bool firstImpact,float distance) {
    if(!active)return;
    ticks.push_back({counter,input});
    if(!data.startCounter&&beforeStage==1&&afterStage>=2)data.startCounter=counter;
    if(!data.takeoffCounter&&beforeStage<=3&&afterStage>=4&&afterStage<=9)data.takeoffCounter=counter;
    if(!data.flightCounter&&beforeStage<=5&&afterStage>=6&&afterStage<=9)data.flightCounter=counter;
    if((beforeStage==7&&afterStage>=8&&afterStage<=11)||(beforeStage==8&&afterStage>=9)) {
        if((input.buttonMask&1)&&!data.leftLandingCounter)data.leftLandingCounter=counter;
        if((input.buttonMask&2)&&!data.rightLandingCounter)data.rightLandingCounter=counter;
    }
    if(firstImpact&&!data.impactCounter){data.impactCounter=counter;data.recordedDistance=distance;}
}
bool OriginalReplayRecording::finish(OriginalReplayData &output,std::string *error) const {
    if(!active||!data.startCounter||!data.impactCounter)return failure(error,"A complete recorded jump is required for original export.");
    auto packed=data;
    for(const auto &tick:ticks) {
        if(tick.input.mouseSensitivity!=1 || tick.input.land!=Landing::None ||
           tick.input.buttonMask<0 || tick.input.pitchDelta!=0)
            return failure(error,"This jump uses controls that the original replay format cannot represent.");
        uint32_t n=tick.counter;
        if(packed.takeoffCounter&&n>=packed.takeoffCounter&&n-packed.takeoffCounter<20)
            packed.takeoffButtons[n-packed.takeoffCounter]=uint8_t(bool(tick.input.buttonMask&1)+bool(tick.input.buttonMask&2));
        if(packed.flightCounter&&n>=packed.flightCounter&&n<=packed.impactCounter) {
            if(n-packed.flightCounter>=packed.mouseCounts.size())return failure(error,"This jump exceeds the original mouse recording capacity.");
            packed.mouseCounts[n-packed.flightCounter]=int8_t(clamp(tick.input.mouseDy,-128,127));
        }
    }
    Replay reconstructed;OriginalReplayMetadata info;
    if(!reconstructOriginalReplay(packed,reconstructed,&info,error))return false;
    if(!info.distanceMatches)return failure(error,"The compact original replay does not reproduce this jump's distance.");
    output=std::move(packed);if(error)error->clear();return true;
}
bool buildOriginalReplay(const Simulation &simulation, OriginalReplayData &output, std::string *error) {
    if (simulation.phase!=Phase::Finished || simulation.result.disqualified)
        return failure(error,"A complete jump is required for original export.");
    OriginalReplayRecording recording;
    recording.begin(simulation.hillIndex,simulation.initialWeather(),simulation.initialRandomSeed());
    bool impacted=false;
    for (const auto &tick:simulation.recordedInputs()) {
        if(!tick.representable)
            return failure(error,"This jump uses controls that the original replay format cannot represent.");
        Input input;input.buttonMask=tick.buttonMask;input.mouseDy=tick.mouseDy;
        bool firstImpact=!impacted&&tick.stageAfter>=11;
        recording.capture(tick.counter,tick.stageBefore,tick.stageAfter,input,firstImpact,simulation.originalRecordedDistance());
        impacted|=firstImpact;
    }
    OriginalReplayData data;
    if(!recording.finish(data,error))return false;
    Replay reconstructed;
    if(!reconstructOriginalReplay(data,reconstructed,nullptr,error))return false;
    if(reconstructed.result.crashed!=simulation.result.crashed)
        return failure(error,"The compact original replay does not reproduce this jump's landing outcome.");
    auto now=std::time(nullptr);auto calendar=std::localtime(&now);char date[11]{};
    if(calendar&&std::strftime(date,sizeof(date),"%Y/%m/%d",calendar))data.date=date;
    output=std::move(data);if(error)error->clear();return true;
}
} // namespace sjp
