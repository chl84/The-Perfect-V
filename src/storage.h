#pragma once
#include "physics.h"
#include "jumper_appearance.h"
#include "graphics_settings.h"
#include <string>

namespace sjp {
struct Records {
    std::array<double, HILL_COUNT> best{}; // All-time standing human records, shared per hill.
    int camera = 0;
    bool sound = true;
    bool music = true;
    bool adjustedSuit = false;
    double sensitivity = 1;
    environment::Selection environment;
    JumperAppearance jumper = JumperAppearance::Pro;
    GraphicsSettings graphics;
};
// Import missing regular files once, retaining both originals and existing destinations.
bool importLegacySaves(const std::string &source, const std::string &destination, std::string *error = nullptr);
bool updateHillRecord(Records &, int hill, const Result &, bool human);
bool saveRecords(const std::string &, const Records &);
Records loadRecords(const std::string &);
bool saveReplay(const std::string &, const Replay &);
bool loadReplay(const std::string &, Replay &);
} // namespace sjp
