#pragma once
#include <algorithm>
#include <array>
#include <vector>

namespace sjp {
struct Resolution {
    int width = 0, height = 0;
};
inline bool operator==(Resolution a, Resolution b) {
    return a.width == b.width && a.height == b.height;
}
inline bool validResolution(Resolution value) {
    return value == Resolution{} ||
           (value.width >= 960 && value.width <= 7680 && value.height >= 540 && value.height <= 4320);
}
inline std::vector<Resolution> resolutionChoices(Resolution desktop, const std::vector<Resolution> &modes) {
    constexpr std::array<Resolution, 5> presets{{{1280, 720}, {1600, 900}, {1920, 1080},
                                                {2560, 1440}, {3840, 2160}}};
    std::vector<Resolution> result{{}};
    for (auto preset : presets)
        if (preset.width <= desktop.width && preset.height <= desktop.height &&
            std::find(modes.begin(), modes.end(), preset) != modes.end())
            result.push_back(preset);
    if (validResolution(desktop) && !(desktop == Resolution{}))
        result.push_back(desktop);
    std::sort(result.begin(), result.end(), [](Resolution a, Resolution b) {
        return a.width != b.width ? a.width < b.width : a.height < b.height;
    });
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}
struct GraphicsSettings {
    bool fullscreen = false;
    int msaaSamples = 4;
    bool vsync = true;
    Resolution resolution;
};
inline bool validMsaaSamples(int samples) {
    return samples == 0 || samples == 2 || samples == 4 || samples == 8;
}
} // namespace sjp
