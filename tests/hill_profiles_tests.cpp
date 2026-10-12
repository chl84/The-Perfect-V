#include "physics.h"
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>

using namespace sjp;
namespace {
void require(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error(message);
}
void near(double actual, double expected, double tolerance, const std::string &message) {
    require(std::abs(actual - expected) <= tolerance,
            message + ": " + std::to_string(actual) + " vs " + std::to_string(expected));
}
} // namespace
int main() {
    try {
        // Independently corroborated original K labels, in original country order.
        const std::array<int, 32> ks{105, 170, 135, 220, 70, 130, 165, 80, 210, 95, 190, 50,
                                     120, 155, 90, 240, 125, 60, 180, 140, 230, 75, 110, 185,
                                     145, 85, 205, 160, 100, 200, 150, 250};
        std::set<std::string> ids;
        for (size_t i = 0; i < hills().size(); i++) {
            const auto &h = hills()[i];
            require(h.k == ks[i], "Original country/K order");
            require(ids.insert(h.id).second, "Stable unique hill IDs");
            near(h.inrun(0), 0, 1e-8, "Coordinate origin at takeoff");
            require(h.start() < 0 && h.inrunStart() < h.start(), "Original gate within inrun");
            require(h.ground(0) < 0, "Snow below takeoff board");
            for (double x : {h.inrunTransition(), h.landingTransition()})
                near(h.ground(x - .00001), h.ground(x + .00001), .001, "Continuous landing curve");
            near(h.inrun(h.inrunTransition() - .00001), h.inrun(h.inrunTransition() + .00001),
                 .001, "Continuous inrun quadratic join");
            near(h.slope(h.landingTransition() - .00001), h.slope(h.landingTransition() + .00001),
                 .00001, "Continuous tanh/quadratic slope");
            near(h.slope(h.outrunBottom()), 0, .000001, "Original outrun vertex");
            for (double d : {10.0, double(h.k), h.k + 50.0})
                near(h.distance(h.atDistance(d)), d, .0001, "Arc-length inverse");
        }
        // Golden coordinates come from a separate transcription of the DOS
        // evaluators, not from the native implementation under test.
        std::ifstream input(SJP_PROFILE_SAMPLES);
        require(bool(input), "Reference curve samples available");
        std::string row;
        std::getline(input, row);
        size_t samples = 0;
        std::array<int, HILL_COUNT> seen{};
        while (std::getline(input, row)) {
            std::stringstream line(row);
            std::vector<std::string> cells;
            for (std::string cell; std::getline(line, cell, ',');)
                cells.push_back(cell);
            require(cells.size() == 10, "Reference CSV shape");
            size_t id = size_t(std::stoi(cells[0]) - 1);
            require(id < hills().size(), "Reference country ID");
            const auto &h = hills()[id];
            double x = std::stod(cells[3]);
            near(h.inrun(x), std::stod(cells[5]), .0002, h.name + " original inrun height");
            near(h.inrunSlope(x), std::stod(cells[6]), .000002, h.name + " original inrun slope");
            near(h.ground(x), std::stod(cells[8]), .0002, h.name + " original landing height");
            near(h.slope(x), std::stod(cells[9]), .000002, h.name + " original landing slope");
            if (seen[id] == 12)
                near(h.kMarkerX, x, .0001, h.name + " original K marker");
            seen[id]++;
            samples++;
        }
        require(samples == 544, "All original reference points tested");
        for (int count : seen)
            require(count == 17, "Every original hill tested");
        std::ifstream distanceInput(SJP_DISTANCE_SAMPLES);
        require(bool(distanceInput), "Original distance fixtures available");
        std::getline(distanceInput, row);
        size_t distanceSamples = 0;
        while (std::getline(distanceInput, row)) {
            std::stringstream line(row);
            std::string id, x, distance;
            require(bool(std::getline(line, id, ',') && std::getline(line, x, ',') &&
                         std::getline(line, distance)), "Original distance CSV shape");
            size_t index = size_t(std::stoi(id) - 1);
            require(index < hills().size(), "Distance fixture hill ID");
            near(hills()[index].distance(std::stod(x)), std::stod(distance), .0001,
                 hills()[index].name + " original float32 distance integration");
            distanceSamples++;
        }
        require(distanceSamples == 128, "All original distance fixtures tested");
        std::cout << "Passed 32 original hills, 544 decoded coordinates and 128 original distances.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
}
