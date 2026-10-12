#pragma once
#include <GL/gl.h>
#include <array>
#include <vector>

namespace sjp::visual {
// Consolidate static scenery into long primitive streams while compiling its
// existing display list. State changes remain in their original order, and
// dynamic scenery still uses ordinary OpenGL. No persistent CPU mesh is kept.
struct MeshVertex {
    std::array<GLdouble, 3> position{};
    std::array<GLdouble, 3> normal{0, 1, 0};
    std::array<GLfloat, 4> color{1, 1, 1, 1};
    std::array<GLdouble, 2> uv{};
};
class StaticBatch;
inline thread_local StaticBatch *activeBatch = nullptr;
class StaticBatch {
  public:
    MeshVertex current;
    std::vector<MeshVertex> primitive, triangles;
    GLenum mode = GL_TRIANGLES;
    bool passthrough = false;

    StaticBatch() {
        GLfloat color[4], normal[3], uv[4];
        ::glGetFloatv(GL_CURRENT_COLOR, color);
        ::glGetFloatv(GL_CURRENT_NORMAL, normal);
        ::glGetFloatv(GL_CURRENT_TEXTURE_COORDS, uv);
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
        ::glBegin(GL_TRIANGLES);
        for (const auto &v : triangles) {
            ::glColor4fv(v.color.data());
            ::glNormal3dv(v.normal.data());
            ::glTexCoord2dv(v.uv.data());
            ::glVertex3dv(v.position.data());
        }
        ::glEnd();
        triangles.clear();
    }
    void finish() {
        if (activeBatch != this)
            return;
        flush();
        ::glColor4fv(current.color.data());
        ::glNormal3dv(current.normal.data());
        ::glTexCoord2dv(current.uv.data());
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
        case GL_TRIANGLES:
            for (size_t i = 0; i + 2 < count; i += 3)
                emit(i, i + 1, i + 2);
            break;
        case GL_QUADS:
            for (size_t i = 0; i + 3 < count; i += 4) {
                emit(i, i + 1, i + 2);
                emit(i, i + 2, i + 3);
            }
            break;
        case GL_QUAD_STRIP:
            for (size_t i = 0; i + 3 < count; i += 2) {
                emit(i, i + 1, i + 3);
                emit(i, i + 3, i + 2);
            }
            break;
        case GL_TRIANGLE_STRIP:
            for (size_t i = 0; i + 2 < count; ++i)
                i % 2 ? emit(i + 1, i, i + 2) : emit(i, i + 1, i + 2);
            break;
        case GL_TRIANGLE_FAN:
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
inline void begin(GLenum mode) {
    if (!activeBatch) {
        ::glBegin(mode);
        return;
    }
    activeBatch->mode = mode;
    activeBatch->primitive.clear();
    activeBatch->passthrough = mode != GL_TRIANGLES && mode != GL_QUADS && mode != GL_QUAD_STRIP &&
                               mode != GL_TRIANGLE_STRIP && mode != GL_TRIANGLE_FAN;
    if (activeBatch->passthrough) {
        activeBatch->flush();
        ::glBegin(mode);
    }
}
inline void end() {
    if (!activeBatch || activeBatch->passthrough)
        ::glEnd();
    else
        activeBatch->end();
    if (activeBatch)
        activeBatch->passthrough = false;
}
inline void color(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    if (activeBatch)
        activeBatch->current.color = {r, g, b, a};
    if (!activeBatch || activeBatch->passthrough)
        ::glColor4f(r, g, b, a);
}
inline void normal(GLdouble x, GLdouble y, GLdouble z) {
    if (activeBatch)
        activeBatch->current.normal = {x, y, z};
    if (!activeBatch || activeBatch->passthrough)
        ::glNormal3d(x, y, z);
}
inline void texCoord(GLdouble u, GLdouble v) {
    if (activeBatch)
        activeBatch->current.uv = {u, v};
    if (!activeBatch || activeBatch->passthrough)
        ::glTexCoord2d(u, v);
}
inline void vertex(GLdouble x, GLdouble y, GLdouble z) {
    if (activeBatch && !activeBatch->passthrough) {
        activeBatch->current.position = {x, y, z};
        activeBatch->primitive.push_back(activeBatch->current);
    } else
        ::glVertex3d(x, y, z);
}
inline void enable(GLenum cap) {
    flush();
    ::glEnable(cap);
}
inline void disable(GLenum cap) {
    flush();
    ::glDisable(cap);
}
inline void pushAttrib(GLbitfield bits) {
    flush();
    ::glPushAttrib(bits);
}
inline void popAttrib() {
    flush();
    ::glPopAttrib();
    if (activeBatch) {
        GLfloat color[4], normal[3], uv[4];
        ::glGetFloatv(GL_CURRENT_COLOR, color);
        ::glGetFloatv(GL_CURRENT_NORMAL, normal);
        ::glGetFloatv(GL_CURRENT_TEXTURE_COORDS, uv);
        for (int i = 0; i < 4; ++i)
            activeBatch->current.color[i] = color[i];
        for (int i = 0; i < 3; ++i)
            activeBatch->current.normal[i] = normal[i];
        activeBatch->current.uv = {uv[0], uv[1]};
    }
}
inline void materialfv(GLenum face, GLenum pname, const GLfloat *value) {
    flush();
    ::glMaterialfv(face, pname, value);
}
inline void materialf(GLenum face, GLenum pname, GLfloat value) {
    flush();
    ::glMaterialf(face, pname, value);
}
inline void bindTexture(GLenum target, GLuint texture) {
    flush();
    ::glBindTexture(target, texture);
}
inline void polygonOffset(GLfloat factor, GLfloat units) {
    flush();
    ::glPolygonOffset(factor, units);
}
inline void depthMask(GLboolean value) {
    flush();
    ::glDepthMask(value);
}
inline void blendFunc(GLenum source, GLenum destination) {
    flush();
    ::glBlendFunc(source, destination);
}
inline void texEnvi(GLenum target, GLenum pname, GLint value) {
    flush();
    ::glTexEnvi(target, pname, value);
}
} // namespace sjp::visual
