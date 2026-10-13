#pragma once
#include "physics.h"
#include <string>

namespace sjp {
struct Records {
    std::array<double, HILL_COUNT> best{};
    int camera = 0;
    bool sound = true;
    double sensitivity = 1;
    environment::Selection environment;
};
bool saveRecords(const std::string &, const Records &);
Records loadRecords(const std::string &);
bool saveReplay(const std::string &, const Replay &);
bool loadReplay(const std::string &, Replay &);
} // namespace sjp
