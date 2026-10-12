#include "original_physics.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

using namespace sjp;
namespace {
void near(double a, double b, const std::string &field) {
    if (std::abs(a - b) > .00001)
        throw std::runtime_error(field + ": " + std::to_string(a) + " vs " + std::to_string(b));
}
} // namespace
int main() {
    try {
        std::ifstream input(SJP_FLIGHT_SAMPLES);
        if (!input)
            throw std::runtime_error("Original flight fixtures unavailable");
        std::string row;
        std::getline(input, row);
        size_t count = 0;
        while (std::getline(input, row)) {
            std::stringstream line(row);
            std::vector<double> c;
            for (std::string value; std::getline(line, value, ',');)
                c.push_back(std::stod(value));
            if (c.size() != 24)
                throw std::runtime_error("Flight fixture shape");
            OriginalAirState s{c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7], c[8]};
            auto next = originalFlightStep(0, 0, s);
            const auto &f = next.forces;
            near(f.relativeX, c[10], "Relative wind X");
            near(f.theta, c[11], "Original atan convention");
            near(f.speed, c[12], "Air speed");
            near(f.body, c[13], "Body force");
            near(f.foot1, c[14], "Left surface force");
            near(f.foot2, c[15], "Right surface force");
            near(f.up, c[16], "Vertical force cap");
            near(f.resistance, c[17], "Resistance force");
            near(f.ax, c[18], "Horizontal acceleration");
            near(f.ay, c[19], "Vertical acceleration");
            near(next.vx, c[20], "70 Hz next horizontal velocity");
            near(next.vyDown, c[21], "70 Hz next down velocity");
            near(next.x, c[22], "70 Hz next X");
            near(next.y, c[23], "70 Hz next Y");
            if (f.up < 0 || f.resistance < 0 || f.ay > .000001)
                throw std::runtime_error("Original force bounds");
            count++;
        }
        if (count != 432)
            throw std::runtime_error("All original flight samples required");
        std::ifstream inrunInput(SJP_INRUN_SAMPLES);
        if (!inrunInput)
            throw std::runtime_error("Original inrun fixtures unavailable");
        std::getline(inrunInput, row);
        size_t inrunCount = 0;
        while (std::getline(inrunInput, row)) {
            std::stringstream line(row);
            std::vector<std::string> c;
            for (std::string value; std::getline(line, value, ',');)
                c.push_back(value);
            if (c.size() != 16)
                throw std::runtime_error("Inrun fixture shape");
            OriginalInrunState s;
            s.x = std::stod(c[3]);
            s.y = std::stod(c[4]);
            s.speed = std::stod(c[5]);
            s.work = std::stod(c[6]);
            s.startY = std::stod(c[7]);
            s.angle = std::stod(c[8]);
            auto next = originalInrunStep(s);
            near(next.x, std::stod(c[9]), "Original inrun X");
            near(next.speed, std::stod(c[11]), "Original energy-derived speed");
            near(next.work, std::stod(c[12]), "Original friction and aerodynamic work");
            near(next.vx, std::stod(c[13]), "Original inrun tangent X");
            near(next.vyDown, std::stod(c[14]), "Original inrun tangent down");
            inrunCount++;
        }
        if (inrunCount != 223)
            throw std::runtime_error("Every original hill's inrun fixtures required");
        std::cout << "Passed 432 original flight states and 223 inrun states across all 32 hills.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
}
