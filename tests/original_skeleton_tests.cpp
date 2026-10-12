#include "original_skeleton.h"
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

int main(int argc, char **argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("Expected original instruction-derived skeleton CSV");
        std::ifstream file(argv[1]);
        if (!file)
            throw std::runtime_error("Cannot open skeleton CSV");
        std::string line;
        std::getline(file, line);
        size_t cases = 0;
        size_t bitDifferences = 0;
        double maximumError = 0;
        while (std::getline(file, line)) {
            std::istringstream row(line);
            std::vector<double> values;
            std::string cell;
            while (std::getline(row, cell, ','))
                values.push_back(std::stod(cell));
            if (values.size() != 87)
                throw std::runtime_error("Incomplete skeleton fixture row");
            std::array<float, 22> pose{};
            for (size_t i = 0; i < pose.size(); ++i)
                pose[i] = float(values[2 + i]);
            auto skeleton = sjp::originalSkeleton(pose, int(values[1]));
            for (size_t i = 0; i < skeleton.points.size(); ++i) {
                const auto &point = skeleton.points[i];
                const std::array<double, 3> actual{point.x, point.y, point.z};
                for (size_t axis = 0; axis < 3; ++axis) {
                    float expected = float(values[24 + 3 * i + axis]);
                    float current = float(actual[axis]);
                    double error = std::abs(actual[axis] - expected);
                    maximumError = std::max(maximumError, error);
                    uint32_t expectedBits = 0, actualBits = 0;
                    std::memcpy(&expectedBits, &expected, sizeof(float));
                    std::memcpy(&actualBits, &current, sizeof(float));
                    if (expectedBits != actualBits)
                        ++bitDifferences;
                    // Reconstruct the captured DWORD from its lossless decimal
                    // representation. Cancellation at the selected boot can
                    // expose sub-attometre x87/trig residual differences; keep
                    // the absolute bound explicit instead of claiming every
                    // stored coordinate is bit-identical.
                    if (!std::isfinite(actual[axis]) || error > 1e-12)
                        throw std::runtime_error("Skeleton mismatch at case " + std::to_string(int(values[0])) +
                                                 " point " + std::to_string(i + 1) + " axis " + std::to_string(axis));
                }
            }
            ++cases;
        }
        if (cases < 1000)
            throw std::runtime_error("Missing skeleton coverage");
        std::cout << cases << " original skeleton cases,21 joints each; " << bitDifferences << " float32 bit differences; maximum error " << maximumError << " metres\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
