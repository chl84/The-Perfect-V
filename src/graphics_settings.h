#pragma once

namespace sjp {
struct GraphicsSettings {
    bool fullscreen = false;
    int msaaSamples = 4;
    bool vsync = true;
};
inline bool validMsaaSamples(int samples) {
    return samples == 0 || samples == 2 || samples == 4 || samples == 8;
}
} // namespace sjp
