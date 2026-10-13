#pragma once
#include "draw_commands.h"
#include <array>
#include <vector>

namespace sjp::visual {
// Consolidate static scenery into long primitive streams while compiling its
// existing cached mesh. State changes remain in their original order, and
// dynamic scenery uses the same Vulkan command recorder. Uploaded meshes
// release their temporary CPU vertex data.
struct MeshVertex {
    std::array<double, 3> position{};
    std::array<double, 3> normal{0, 1, 0};
    std::array<float, 4> color{1, 1, 1, 1};
    std::array<double, 2> uv{};
};
class StaticBatch;
inline thread_local StaticBatch *activeBatch = nullptr;
class StaticBatch {
  public:
    MeshVertex current;
    std::vector<MeshVertex> primitive, triangles;
    sjp::gfx::Enum mode = sjp::gfx::TRIANGLES;
    bool passthrough = false;

    StaticBatch() {
        float color[4], normal[3], uv[4];
        ::sjp::gfx::getFloatv(sjp::gfx::CURRENT_COLOR, color);
        ::sjp::gfx::getFloatv(sjp::gfx::CURRENT_NORMAL, normal);
        ::sjp::gfx::getFloatv(sjp::gfx::CURRENT_TEXTURE_COORDS, uv);
        for (int i = 0; i < 4; ++i)
            current.color[i] = color[i];
        for (int i = 0; i < 3; ++i)
            current.normal[i] = normal[i];
        current.uv = {uv[0], uv[1]};
        primitive.reserve(96);
        triangles.reserve(65536);
        activeBatch = this;
    }
    StaticBatch(const StaticBatch &) = delete;
    StaticBatch &operator=(const StaticBatch &) = delete;
    ~StaticBatch() { finish(); }
    void flush() {
        if (triangles.empty())
            return;
        ::sjp::gfx::begin(sjp::gfx::TRIANGLES);
        for (const auto &v : triangles) {
            ::sjp::gfx::color4fv(v.color.data());
            ::sjp::gfx::normal3dv(v.normal.data());
            ::sjp::gfx::texCoord2dv(v.uv.data());
            ::sjp::gfx::vertex3dv(v.position.data());
        }
        ::sjp::gfx::end();
        triangles.clear();
    }
    void finish() {
        if (activeBatch != this)
            return;
        flush();
        ::sjp::gfx::color4fv(current.color.data());
        ::sjp::gfx::normal3dv(current.normal.data());
        ::sjp::gfx::texCoord2dv(current.uv.data());
        activeBatch = nullptr;
    }
    void emit(size_t a, size_t b, size_t c) {
        triangles.push_back(primitive[a]);
        triangles.push_back(primitive[b]);
        triangles.push_back(primitive[c]);
    }
    void end() {
        const size_t count = primitive.size();
        switch (mode) {
        case sjp::gfx::TRIANGLES:
            for (size_t i = 0; i + 2 < count; i += 3)
                emit(i, i + 1, i + 2);
            break;
        case sjp::gfx::QUADS:
            for (size_t i = 0; i + 3 < count; i += 4) {
                emit(i, i + 1, i + 2);
                emit(i, i + 2, i + 3);
            }
            break;
        case sjp::gfx::QUAD_STRIP:
            for (size_t i = 0; i + 3 < count; i += 2) {
                emit(i, i + 1, i + 3);
                emit(i, i + 3, i + 2);
            }
            break;
        case sjp::gfx::TRIANGLE_STRIP:
            for (size_t i = 0; i + 2 < count; ++i)
                i % 2 ? emit(i + 1, i, i + 2) : emit(i, i + 1, i + 2);
            break;
        case sjp::gfx::TRIANGLE_FAN:
            for (size_t i = 1; i + 1 < count; ++i)
                emit(0, i, i + 1);
            break;
        default:
            break;
        }
        if (triangles.size() >= 65536)
            flush();
    }
};
inline void flush() {
    if (activeBatch)
        activeBatch->flush();
}
inline void begin(sjp::gfx::Enum mode) {
    if (!activeBatch) {
        ::sjp::gfx::begin(mode);
        return;
    }
    activeBatch->mode = mode;
    activeBatch->primitive.clear();
    activeBatch->passthrough = mode != sjp::gfx::TRIANGLES && mode != sjp::gfx::QUADS && mode != sjp::gfx::QUAD_STRIP &&
                               mode != sjp::gfx::TRIANGLE_STRIP && mode != sjp::gfx::TRIANGLE_FAN;
    if (activeBatch->passthrough) {
        activeBatch->flush();
        ::sjp::gfx::begin(mode);
    }
}
inline void end() {
    if (!activeBatch || activeBatch->passthrough)
        ::sjp::gfx::end();
    else
        activeBatch->end();
    if (activeBatch)
        activeBatch->passthrough = false;
}
inline void color(float r, float g, float b, float a) {
    if (activeBatch)
        activeBatch->current.color = {r, g, b, a};
    if (!activeBatch || activeBatch->passthrough)
        ::sjp::gfx::color4f(r, g, b, a);
}
inline void normal(double x, double y, double z) {
    if (activeBatch)
        activeBatch->current.normal = {x, y, z};
    if (!activeBatch || activeBatch->passthrough)
        ::sjp::gfx::normal3d(x, y, z);
}
inline void texCoord(double u, double v) {
    if (activeBatch)
        activeBatch->current.uv = {u, v};
    if (!activeBatch || activeBatch->passthrough)
        ::sjp::gfx::texCoord2d(u, v);
}
inline void vertex(double x, double y, double z) {
    if (activeBatch && !activeBatch->passthrough) {
        activeBatch->current.position = {x, y, z};
        activeBatch->primitive.push_back(activeBatch->current);
    } else
        ::sjp::gfx::vertex3d(x, y, z);
}
inline void enable(sjp::gfx::Enum cap) {
    flush();
    ::sjp::gfx::enable(cap);
}
inline void disable(sjp::gfx::Enum cap) {
    flush();
    ::sjp::gfx::disable(cap);
}
inline void pushAttrib(sjp::gfx::Flags bits) {
    flush();
    ::sjp::gfx::pushAttrib(bits);
}
inline void popAttrib() {
    flush();
    ::sjp::gfx::popAttrib();
    if (activeBatch) {
        float color[4], normal[3], uv[4];
        ::sjp::gfx::getFloatv(sjp::gfx::CURRENT_COLOR, color);
        ::sjp::gfx::getFloatv(sjp::gfx::CURRENT_NORMAL, normal);
        ::sjp::gfx::getFloatv(sjp::gfx::CURRENT_TEXTURE_COORDS, uv);
        for (int i = 0; i < 4; ++i)
            activeBatch->current.color[i] = color[i];
        for (int i = 0; i < 3; ++i)
            activeBatch->current.normal[i] = normal[i];
        activeBatch->current.uv = {uv[0], uv[1]};
    }
}
inline void materialfv(sjp::gfx::Enum face, sjp::gfx::Enum pname, const float *value) {
    flush();
    ::sjp::gfx::materialfv(face, pname, value);
}
inline void materialf(sjp::gfx::Enum face, sjp::gfx::Enum pname, float value) {
    flush();
    ::sjp::gfx::materialf(face, pname, value);
}
inline void bindTexture(sjp::gfx::Enum target, sjp::gfx::Id texture) {
    flush();
    ::sjp::gfx::bindTexture(target, texture);
}
inline void polygonOffset(float factor, float units) {
    flush();
    ::sjp::gfx::polygonOffset(factor, units);
}
inline void depthMask(bool value) {
    flush();
    ::sjp::gfx::depthMask(value);
}
inline void blendFunc(sjp::gfx::Enum source, sjp::gfx::Enum destination) {
    flush();
    ::sjp::gfx::blendFunc(source, destination);
}
inline void texEnvi(sjp::gfx::Enum target, sjp::gfx::Enum pname, int value) {
    flush();
    ::sjp::gfx::texEnvi(target, pname, value);
}
} // namespace sjp::visual
