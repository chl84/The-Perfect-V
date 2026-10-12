#include "original_wind.h"
#include <array>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
using Row = std::unordered_map<std::string, double>;
std::vector<std::string> split(const std::string &line) {
    std::vector<std::string> fields;
    std::stringstream stream(line);
    for (std::string field; std::getline(stream, field, ',');)
        fields.push_back(field);
    return fields;
}
std::vector<Row> read(const std::string &path) {
    std::ifstream input(path);
    if (!input) { std::cerr << "Cannot open " << path << '\n'; std::exit(2); }
    std::string line;
    std::getline(input, line);
    auto names = split(line);
    // csv.writer's CRLF makes the final header carry a carriage return.
    if (!names.empty() && !names.back().empty() && names.back().back() == '\r')
        names.back().pop_back();
    std::vector<Row> rows;
    while (std::getline(input, line)) {
        auto values = split(line);
        Row row;
        for (size_t i = 0; i < names.size(); ++i)
            row[names[i]] = std::stod(values.at(i));
        rows.push_back(std::move(row));
    }
    return rows;
}
constexpr std::array<const char *, 8> names = {
    "x", "y", "variation", "speed", "angle", "display", "angular_step", "angular_cap"};
std::array<float *, 8> members(sjp::OriginalWind &wind) {
    return {&wind.x, &wind.y, &wind.variationStep, &wind.speed, &wind.angle,
            &wind.displayAngle, &wind.angularStep, &wind.angularCap};
}
void verify(sjp::OriginalWind &wind, sjp::OriginalRandom &random, const Row &row) {
    auto fields = members(wind);
    for (size_t i = 0; i < fields.size(); ++i)
        if (*fields[i] != float(row.at(names[i]))) {
            std::cerr << "case " << row.at("case") << ", " << names[i]
                      << " expected " << row.at(names[i]) << " got " << *fields[i] << '\n';
            std::exit(1);
        }
    if (random.state != uint32_t(row.at("final_seed"))) {
        std::cerr << "Random state differs for case " << row.at("case") << '\n';
        std::exit(1);
    }
}
}

int main(int argc, char **argv) {
    std::string directory = argc > 1 ? argv[1] : "captures/fidelity-physics/wind";
    auto resets = read(directory + "/wind-reset-golden.csv");
    for (const auto &row : resets) {
        sjp::OriginalWind wind;
        sjp::OriginalRandom random(uint32_t(row.at("seed")));
        wind.reset(row.at("input_speed"), row.at("input_angle"), random);
        verify(wind, random, row);
    }
    auto transitions = read(directory + "/wind-golden.csv");
    for (const auto &row : transitions) {
        sjp::OriginalWind wind;
        auto fields = members(wind);
        for (size_t i = 0; i < fields.size(); ++i)
            *fields[i] = float(row.at(std::string("input_") + names[i]));
        sjp::OriginalRandom random(uint32_t(row.at("seed")));
        bool updated = wind.advance(uint32_t(row.at("frame")), random, row.at("dt"));
        if (updated != bool(row.at("updated")) || wind.frameCounter != uint32_t(row.at("frame"))) {
            std::cerr << "Update gate differs for case " << row.at("case") << '\n';
            return 1;
        }
        verify(wind, random, row);
    }
    std::string initializationDirectory = argc > 2 ? argv[2] : directory + "/../wind-init";
    auto initializations = read(initializationDirectory + "/initialization-golden.csv");
    for (const auto &row : initializations) {
        if (row.at("clock_valid") &&
            sjp::originalWindSeedFromDosTime(uint8_t(row.at("hours")), uint8_t(row.at("minutes")),
                                            uint8_t(row.at("seconds")), uint8_t(row.at("hundredths"))) !=
                uint32_t(row.at("seed"))) {
            std::cerr << "DOS clock seed differs for case " << row.at("case") << '\n';
            return 1;
        }
        sjp::OriginalWind wind;
        sjp::OriginalRandom random(uint32_t(row.at("seed")));
        wind.initialize(random);
        verify(wind, random, row);
        if (wind.weatherIntensity != uint8_t(row.at("weather")) || wind.frameCounter != 0) {
            std::cerr << "Weather/frame initialization differs for case " << row.at("case") << '\n';
            return 1;
        }
        // These no-update ticks must consume no RNG state; the first actual
        // update must continue the exact stream left by the18 original draws.
        uint32_t before = random.state;
        for (int frame = 1; frame < 10; ++frame)
            if (wind.tick(random) || random.state != before) {
                std::cerr << "Premature initial wind update\n";
                return 1;
            }
        if (!wind.tick(random) || wind.weatherIntensity != uint8_t(row.at("weather"))) {
            std::cerr << "First wind update/weather retention differs\n";
            return 1;
        }
        Row next{{"case",row.at("case")},{"final_seed",row.at("next_seed")}};
        for (const auto *name : names)
            next[name] = row.at(std::string("next_")+name);
        verify(wind, random, next);
    }
    // Original initializer leaves DS4cdc intact; the first eligible update
    // writes.2. Cover both fresh zero storage and retained prior arrow state.
    for (float preservedCap : {0.f, .125f, .2f}) {
        sjp::OriginalWind wind;
        wind.angularCap = preservedCap;
        sjp::OriginalRandom random(1);
        wind.initialize(random);
        if (wind.angularCap != preservedCap) {
            std::cerr << "Original initializer overwrote angular cap\n";
            return 1;
        }
        for (int frame = 1; frame < 10; ++frame) {
            wind.tick(random);
            if (wind.angularCap != preservedCap) {
                std::cerr << "Original cap changed before first eligible update\n";
                return 1;
            }
        }
        wind.tick(random);
        if (wind.angularCap != .2f) {
            std::cerr << "First original update did not set angular cap\n";
            return 1;
        }
    }
    std::cout << resets.size() << " resets and " << transitions.size()
              << " wind transitions plus " << initializations.size()
              << " randomized initializations/first updates match all float32 fields and RNG state exactly.\n";
}
