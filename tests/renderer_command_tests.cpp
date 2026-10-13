// CPU-only regression test. Every renderer GL entry point is supplied below;
// this executable does not link to OpenGL or create a window/context.
// The model checks finite arguments, primitive/stack balance, resource lifetime
// and current-attribute writes after a display list's outer restoration. It does
// not emulate rasterization, lighting, or the driver's complete display list.
#include "renderer.h"
#include "visual_weather.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>
namespace audit {
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
struct Current {
    std::array<GLfloat, 4> color{1, 1, 1, 1};
    std::array<GLfloat, 3> normal{0, 0, 1};
    std::array<GLfloat, 4> uv{0, 0, 0, 1};
};
Current current;
struct Saved {
    GLbitfield mask;
    Current value;
    bool depth;
};
std::vector<Saved> attributes;
std::map<GLenum, int> matrices{{GL_MODELVIEW, 1}, {GL_PROJECTION, 1}, {GL_TEXTURE, 1}};
GLenum matrix = GL_MODELVIEW, primitiveMode = GL_TRIANGLES;
bool primitive = false, depth = true;
GLuint compiling = 0, nextList = 1, nextTexture = 1;
struct List {
    size_t vertices = 0, commands = 0;
    std::vector<std::function<void()>> tail;
};
std::map<GLuint, List> lists;
std::set<GLuint> textures;
size_t vertices = 0, commands = 0, maximumDraw = 0, drawVertices = 0, maximumLists = 0,
       maximumCachedVertices = 0, textureBytes = 0;
int drawableWidth = 960, drawableHeight = 600;
void finite(double x) {
    require(std::isfinite(x), "Non-finite value submitted to OpenGL");
}
void command(const char *name, bool allowed = false) {
    ++commands;
    if (compiling)
        ++lists.at(compiling).commands;
    require(!primitive || allowed, name);
}
uint64_t positionHash = 1469598103934665603ULL, particlePositionHash = 1469598103934665603ULL;
size_t particleVertices = 0;
size_t completedFrames = 0;
void position(double x, double y, double z) {
    for (double v : {x, y, z}) {
        uint64_t b;
        std::memcpy(&b, &v, sizeof(b));
        positionHash = (positionHash ^ b) * 1099511628211ULL;
        if (!compiling && primitiveMode == GL_TRIANGLE_FAN)
            particlePositionHash = (particlePositionHash ^ b) * 1099511628211ULL;
    }
}
void vertex() {
    require(primitive, "Vertex outside glBegin/glEnd");
    ++vertices;
    ++drawVertices;
    if (!compiling && primitiveMode == GL_TRIANGLE_FAN)
        ++particleVertices;
    if (compiling)
        ++lists.at(compiling).vertices;
}
void tail(std::function<void()> op) {
    if (compiling && attributes.empty())
        lists.at(compiling).tail.push_back(std::move(op));
}
void balanced() {
    require(!primitive && !compiling && attributes.empty(), "Unbalanced primitive/list/attribute stack");
    for (auto [mode, size] : matrices)
        require(size == 1, "Unbalanced matrix stack");
}
size_t cached() {
    size_t n = 0;
    for (const auto &[id, list] : lists)
        n += list.vertices;
    return n;
}
} // namespace audit
extern "C" {
void SDL_GL_GetDrawableSize(SDL_Window *, int *w, int *h) {
    *w = audit::drawableWidth;
    *h = audit::drawableHeight;
}
void SDL_GL_SwapWindow(SDL_Window *) {}
void GLAPIENTRY glFinish(void) {
    audit::command("glFinish inside glBegin", false);
    audit::require(!audit::compiling && audit::attributes.empty(),
                   "Frame completed before display list/attribute restoration");
    ++audit::completedFrames;
}
void GLAPIENTRY glBegin(GLenum mode) {
    audit::command("glBegin inside glBegin", false);
    audit::require(!audit::primitive, "Nested glBegin");
    audit::primitive = true;
    audit::primitiveMode = mode;
    audit::drawVertices = 0;
}
void GLAPIENTRY glBindTexture(GLenum target, GLuint texture) {
    audit::command("glBindTexture inside glBegin", false);
    audit::require(!texture || audit::textures.count(texture), "Bind unknown texture");
}
void GLAPIENTRY glBlendFunc(GLenum sfactor, GLenum dfactor) {
    audit::command("glBlendFunc inside glBegin", false);
}
void GLAPIENTRY glCallList(GLuint list) {
    audit::command("glCallList inside glBegin", false);
    audit::require(audit::lists.count(list), "Use of missing display list");
    for (auto &op : audit::lists.at(list).tail)
        op();
}
void GLAPIENTRY glClear(GLbitfield mask) {
    audit::command("glClear inside glBegin", false);
}
void GLAPIENTRY glClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha) {
    audit::command("glClearColor inside glBegin", false);
    audit::finite(red);
    audit::finite(green);
    audit::finite(blue);
    audit::finite(alpha);
}
void GLAPIENTRY glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) {
    audit::command("glColor4f inside glBegin", true);
    audit::finite(red);
    audit::finite(green);
    audit::finite(blue);
    audit::finite(alpha);
    audit::current.color = {red, green, blue, alpha};
    auto value = audit::current.color;
    audit::tail([value] { audit::current.color = value; });
}
void GLAPIENTRY glColor4fv(const GLfloat *v) {
    audit::command("glColor4fv inside glBegin", true);
    for (int i = 0; i < 4; ++i)
        audit::finite(v[i]);
    std::copy(v, v + 4, audit::current.color.begin());
    auto value = audit::current.color;
    audit::tail([value] { audit::current.color = value; });
}
void GLAPIENTRY glColorMaterial(GLenum face, GLenum mode) {
    audit::command("glColorMaterial inside glBegin", false);
}
void GLAPIENTRY glDeleteLists(GLuint list, GLsizei range) {
    audit::command("glDeleteLists inside glBegin", false);
    for (int i = 0; i < range; ++i)
        audit::require(audit::lists.erase(list + i) == 1, "Invalid display list deletion");
}
void GLAPIENTRY glDeleteTextures(GLsizei n, const GLuint *textures) {
    audit::command("glDeleteTextures inside glBegin", false);
    for (int i = 0; i < n; ++i)
        if (textures[i])
            audit::require(audit::textures.erase(textures[i]) == 1, "Invalid texture deletion");
}
void GLAPIENTRY glDepthMask(GLboolean flag) {
    audit::command("glDepthMask inside glBegin", false);
    audit::depth = flag;
}
void GLAPIENTRY glDisable(GLenum cap) {
    audit::command("glDisable inside glBegin", false);
}
void GLAPIENTRY glEnable(GLenum cap) {
    audit::command("glEnable inside glBegin", false);
}
void GLAPIENTRY glEnd(void) {
    audit::command("glEnd inside glBegin", true);
    audit::require(audit::primitive, "Unmatched glEnd");
    audit::primitive = false;
    audit::maximumDraw = std::max(audit::maximumDraw, audit::drawVertices);
}
void GLAPIENTRY glEndList(void) {
    audit::command("glEndList inside glBegin", false);
    audit::require(audit::compiling && !audit::primitive, "Unmatched glEndList");
    audit::maximumCachedVertices = std::max(audit::maximumCachedVertices, audit::cached());
    audit::compiling = 0;
}
void GLAPIENTRY glFogf(GLenum pname, GLfloat param) {
    audit::command("glFogf inside glBegin", false);
    audit::finite(param);
}
void GLAPIENTRY glFogfv(GLenum pname, const GLfloat *params) {
    audit::command("glFogfv inside glBegin", false);
    for (int i = 0; i < 4; ++i)
        audit::finite(params[i]);
}
void GLAPIENTRY glFogi(GLenum pname, GLint param) {
    audit::command("glFogi inside glBegin", false);
}
void GLAPIENTRY glFrustum(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble near_val,
                          GLdouble far_val) {
    audit::command("glFrustum inside glBegin", false);
    audit::finite(left);
    audit::finite(right);
    audit::finite(bottom);
    audit::finite(top);
    audit::finite(near_val);
    audit::finite(far_val);
    audit::require(left != right && bottom != top && near_val > 0 && far_val > near_val,
                   "Invalid perspective projection");
}
GLuint GLAPIENTRY glGenLists(GLsizei range) {
    audit::command("glGenLists inside glBegin", false);
    audit::require(range > 0, "Invalid list range");
    auto id = audit::nextList;
    audit::nextList += range;
    return id;
}
void GLAPIENTRY glGenTextures(GLsizei n, GLuint *textures) {
    audit::command("glGenTextures inside glBegin", false);
    for (int i = 0; i < n; ++i) {
        textures[i] = audit::nextTexture++;
        audit::textures.insert(textures[i]);
    }
}
void GLAPIENTRY glGetFloatv(GLenum pname, GLfloat *params) {
    audit::command("glGetFloatv inside glBegin", false);
    if (pname == GL_CURRENT_COLOR)
        std::copy(audit::current.color.begin(), audit::current.color.end(), params);
    else if (pname == GL_CURRENT_NORMAL)
        std::copy(audit::current.normal.begin(), audit::current.normal.end(), params);
    else if (pname == GL_CURRENT_TEXTURE_COORDS)
        std::copy(audit::current.uv.begin(), audit::current.uv.end(), params);
    else
        throw std::runtime_error("Unsupported audit glGetFloatv");
}
void GLAPIENTRY glGetIntegerv(GLenum pname, GLint *params) {
    audit::command("glGetIntegerv inside glBegin", false);
    audit::require(pname == GL_UNPACK_ALIGNMENT, "Unsupported audit glGetIntegerv");
    *params = 1;
}
const GLubyte *GLAPIENTRY glGetString(GLenum name) {
    audit::command("glGetString inside glBegin", false);
    return reinterpret_cast<const GLubyte *>("CPU command audit; no graphics context");
}
void GLAPIENTRY glHint(GLenum target, GLenum mode) {
    audit::command("glHint inside glBegin", false);
}
void GLAPIENTRY glLightModelfv(GLenum pname, const GLfloat *params) {
    audit::command("glLightModelfv inside glBegin", false);
    for (int i = 0; i < 4; ++i)
        audit::finite(params[i]);
}
void GLAPIENTRY glLightfv(GLenum light, GLenum pname, const GLfloat *params) {
    audit::command("glLightfv inside glBegin", false);
    for (int i = 0; i < 4; ++i)
        audit::finite(params[i]);
}
void GLAPIENTRY glLineWidth(GLfloat width) {
    audit::command("glLineWidth inside glBegin", false);
    audit::finite(width);
}
void GLAPIENTRY glLoadIdentity(void) {
    audit::command("glLoadIdentity inside glBegin", false);
}
void GLAPIENTRY glMaterialf(GLenum face, GLenum pname, GLfloat param) {
    audit::command("glMaterialf inside glBegin", false);
    audit::finite(param);
}
void GLAPIENTRY glMaterialfv(GLenum face, GLenum pname, const GLfloat *params) {
    audit::command("glMaterialfv inside glBegin", false);
    for (int i = 0; i < 4; ++i)
        audit::finite(params[i]);
}
void GLAPIENTRY glMatrixMode(GLenum mode) {
    audit::command("glMatrixMode inside glBegin", false);
    audit::require(audit::matrices.count(mode), "Invalid matrix mode");
    audit::matrix = mode;
}
void GLAPIENTRY glMultMatrixf(const GLfloat *m) {
    audit::command("glMultMatrixf inside glBegin", false);
    for (int i = 0; i < 16; ++i)
        audit::finite(m[i]);
}
void GLAPIENTRY glNewList(GLuint list, GLenum mode) {
    audit::command("glNewList inside glBegin", false);
    audit::require(!audit::compiling && list != 0 && mode == GL_COMPILE_AND_EXECUTE,
                   "Invalid list compilation");
    audit::compiling = list;
    audit::lists[list] = {};
    audit::maximumLists = std::max(audit::maximumLists, audit::lists.size());
}
void GLAPIENTRY glNormal3d(GLdouble nx, GLdouble ny, GLdouble nz) {
    audit::command("glNormal3d inside glBegin", true);
    audit::finite(nx);
    audit::finite(ny);
    audit::finite(nz);
    audit::current.normal = {GLfloat(nx), GLfloat(ny), GLfloat(nz)};
    auto value = audit::current.normal;
    audit::tail([value] { audit::current.normal = value; });
}
void GLAPIENTRY glNormal3dv(const GLdouble *v) {
    audit::command("glNormal3dv inside glBegin", true);
    for (int i = 0; i < 3; ++i) {
        audit::finite(v[i]);
        audit::current.normal[i] = v[i];
    }
    auto value = audit::current.normal;
    audit::tail([value] { audit::current.normal = value; });
}
void GLAPIENTRY glOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble near_val,
                        GLdouble far_val) {
    audit::command("glOrtho inside glBegin", false);
    audit::finite(left);
    audit::finite(right);
    audit::finite(bottom);
    audit::finite(top);
    audit::finite(near_val);
    audit::finite(far_val);
    audit::require(left != right && bottom != top && near_val != far_val, "Invalid orthographic projection");
}
void GLAPIENTRY glPixelStorei(GLenum pname, GLint param) {
    audit::command("glPixelStorei inside glBegin", false);
}
void GLAPIENTRY glPolygonOffset(GLfloat factor, GLfloat units) {
    audit::command("glPolygonOffset inside glBegin", false);
    audit::finite(factor);
    audit::finite(units);
}
void GLAPIENTRY glPopAttrib(void) {
    audit::command("glPopAttrib inside glBegin", false);
    audit::require(!audit::attributes.empty(), "Attribute stack underflow");
    auto saved = audit::attributes.back();
    audit::attributes.pop_back();
    if (saved.mask & GL_CURRENT_BIT)
        audit::current = saved.value;
    if (saved.mask & GL_DEPTH_BUFFER_BIT)
        audit::depth = saved.depth;
}
void GLAPIENTRY glPopMatrix(void) {
    audit::command("glPopMatrix inside glBegin", false);
    audit::require(--audit::matrices[audit::matrix] >= 1, "Matrix stack underflow");
}
void GLAPIENTRY glPushAttrib(GLbitfield mask) {
    audit::command("glPushAttrib inside glBegin", false);
    audit::attributes.push_back({mask, audit::current, audit::depth});
    audit::require(audit::attributes.size() <= 16, "Attribute stack exceeds guaranteed capacity");
}
void GLAPIENTRY glPushMatrix(void) {
    audit::command("glPushMatrix inside glBegin", false);
    ++audit::matrices[audit::matrix];
    audit::require(audit::matrices[audit::matrix] <= (audit::matrix == GL_MODELVIEW ? 32 : 2),
                   "Matrix stack overflow");
}
void GLAPIENTRY glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type,
                             GLvoid *pixels) {
    audit::command("glReadPixels inside glBegin", false);
    throw std::runtime_error("Pixel readback is unavailable in CPU command test");
}
void GLAPIENTRY glScalef(GLfloat x, GLfloat y, GLfloat z) {
    audit::command("glScalef inside glBegin", false);
    audit::finite(x);
    audit::finite(y);
    audit::finite(z);
}
void GLAPIENTRY glShadeModel(GLenum mode) {
    audit::command("glShadeModel inside glBegin", false);
}
void GLAPIENTRY glTexCoord2d(GLdouble s, GLdouble t) {
    audit::command("glTexCoord2d inside glBegin", true);
    audit::finite(s);
    audit::finite(t);
    audit::current.uv = {GLfloat(s), GLfloat(t), 0, 1};
    auto value = audit::current.uv;
    audit::tail([value] { audit::current.uv = value; });
}
void GLAPIENTRY glTexCoord2dv(const GLdouble *v) {
    audit::command("glTexCoord2dv inside glBegin", true);
    for (int i = 0; i < 2; ++i)
        audit::finite(v[i]);
    audit::current.uv = {GLfloat(v[0]), GLfloat(v[1]), 0, 1};
    auto value = audit::current.uv;
    audit::tail([value] { audit::current.uv = value; });
}
void GLAPIENTRY glTexCoord2f(GLfloat s, GLfloat t) {
    audit::command("glTexCoord2f inside glBegin", true);
    audit::finite(s);
    audit::finite(t);
    audit::current.uv = {s, t, 0, 1};
    auto value = audit::current.uv;
    audit::tail([value] { audit::current.uv = value; });
}
void GLAPIENTRY glTexEnvi(GLenum target, GLenum pname, GLint param) {
    audit::command("glTexEnvi inside glBegin", false);
}
void GLAPIENTRY glTexImage2D(GLenum target, GLint level, GLint internalFormat, GLsizei width, GLsizei height,
                             GLint border, GLenum format, GLenum type, const GLvoid *pixels) {
    audit::command("glTexImage2D inside glBegin", false);
    audit::require(width > 0 && height > 0 && pixels, "Invalid texture upload");
    audit::textureBytes += size_t(width) * height;
}
void GLAPIENTRY glTexParameteri(GLenum target, GLenum pname, GLint param) {
    audit::command("glTexParameteri inside glBegin", false);
}
void GLAPIENTRY glTranslated(GLdouble x, GLdouble y, GLdouble z) {
    audit::command("glTranslated inside glBegin", false);
    audit::finite(x);
    audit::finite(y);
    audit::finite(z);
}
void GLAPIENTRY glTranslatef(GLfloat x, GLfloat y, GLfloat z) {
    audit::command("glTranslatef inside glBegin", false);
    audit::finite(x);
    audit::finite(y);
    audit::finite(z);
}
void GLAPIENTRY glVertex2f(GLfloat x, GLfloat y) {
    audit::position(x, y, 0);
    audit::command("glVertex2f inside glBegin", true);
    audit::finite(x);
    audit::finite(y);
    audit::vertex();
}
void GLAPIENTRY glVertex3d(GLdouble x, GLdouble y, GLdouble z) {
    audit::position(x, y, z);
    audit::command("glVertex3d inside glBegin", true);
    audit::finite(x);
    audit::finite(y);
    audit::finite(z);
    audit::vertex();
}
void GLAPIENTRY glVertex3dv(const GLdouble *v) {
    audit::position(v[0], v[1], v[2]);
    audit::command("glVertex3dv inside glBegin", true);
    for (int i = 0; i < 3; ++i)
        audit::finite(v[i]);
    audit::vertex();
}
void GLAPIENTRY glViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    audit::command("glViewport inside glBegin", false);
}
}

int main() {
    try {
        size_t snowy = 0;
        constexpr size_t weatherSamples = 100000;
        for (uint32_t i = 0; i < weatherSamples; ++i) {
            const uint32_t seed = sjp::originalWindSeedFromDosTime(
                uint8_t(i / 360000), uint8_t(i / 6000 % 60), uint8_t(i / 100 % 60), uint8_t(i % 100));
            snowy += sjp::visual::snowForJump(seed);
        }
        audit::require(snowy > 14500 && snowy < 15500, "Snowfall probability differs from 15 percent");
        for (double angle : {0.0, sjp::PI / 4, sjp::PI / 2, sjp::PI, -sjp::PI / 2, -sjp::PI / 4}) {
            sjp::Frame f;
            f.windAngle = float(angle);
            f.windSpeed = 3;
            const auto wind = sjp::visual::windDirection(f);
            const auto tail = sjp::visual::windsockPoint(f, 2, 1);
            audit::require(sjp::dot(tail.normalized(), wind) > .98,
                           "Windsock disagrees with displayed wind direction");
            const auto drift = sjp::visual::snowDrift(wind * f.windSpeed, 10);
            audit::require(sjp::dot(drift, wind) >= 0 && drift.z == 0, "Snow drifts against displayed wind");
            f.windSpeed = 0;
            const auto calm = sjp::visual::windsockPoint(f, 2, 1);
            audit::require(calm.x == 0 && calm.y < 0 && calm.z == 0, "Calm windsock does not hang down");
        }
        std::cout << "weather_samples=" << weatherSamples << " snowy_jumps=" << snowy << '\n';
        size_t frames = 0, leaks = 0;
        {
            sjp::Renderer r(nullptr, SJP_TEST_ASSET_DIR);
            for (int hi = 0; hi < int(sjp::HILL_COUNT); ++hi) {
                sjp::Simulation s(hi, 0);
                s.resetOriginal(hi, 12345, 98765);
                s.setWind(0, 0);
                r.setJumpWeather(12345 + uint32_t(hi));
                std::vector<sjp::Frame> samples{s.frame()};
                for (int t = 0; t < 14000 && s.phase != sjp::Phase::Finished; ++t) {
                    sjp::Input in;
                    in.start = s.phase == sjp::Phase::Ready;
                    s.step(sjp::ORIGINAL_TICK, in);
                    if (t % 173 == 0 || s.phase == sjp::Phase::Finished)
                        samples.push_back(s.frame());
                }
                sjp::Simulation healthy(hi, 0);
                healthy.resetOriginal(hi, 12345, 98765);
                healthy.setWind(0, 0);
                for (int t = 0; t < 14000 && healthy.phase != sjp::Phase::Finished; ++t) {
                    healthy.step(sjp::ORIGINAL_TICK, sjp::autopilot(healthy));
                    if (t % 173 == 0 || healthy.phase == sjp::Phase::Finished)
                        samples.push_back(healthy.frame());
                }
                r.resetCamera();
                r.begin(s.hill(), s.frame(), 4, 0, true);
                r.overlay();
                r.finish();
                audit::balanced();
                ++frames;
                for (const auto &f : samples)
                    for (int camera = 0; camera < 3; ++camera) {
                        r.resetCamera();
                        r.begin(s.hill(), f, f.elapsed, camera, false, true, &f);
                        r.overlay();
                        r.glassPanel(20, 20, 280, 100);
                        r.text(s.hill().name, 35, 35, 20);
                        r.finish();
                        audit::balanced();
                        audit::require(audit::depth, "Depth writes leaked across frame");
                        ++frames;
                    }
                auto id = audit::lists.rbegin()->first;
                audit::current.color = {.2f, .3f, .4f, .5f};
                audit::current.normal = {.6f, .7f, .8f};
                audit::current.uv = {.9f, .8f, 0, 1};
                auto before = audit::current;
                glCallList(id);
                if (before.color != audit::current.color || before.normal != audit::current.normal ||
                    before.uv != audit::current.uv)
                    ++leaks;
            }
            std::cout << "frames=" << frames << " commands=" << audit::commands
                      << " compiled_vertices=" << audit::vertices
                      << " maximum_draw_vertices=" << audit::maximumDraw
                      << " maximum_cached_lists=" << audit::maximumLists
                      << " maximum_cached_vertices=" << audit::maximumCachedVertices
                      << " uploaded_texture_bytes=" << audit::textureBytes
                      << " position_hash=" << audit::positionHash << " list_state_leaks=" << leaks << '\n';
            sjp::Simulation s(0, 0);
            for (const auto &size :
                 std::array<std::array<int, 2>, 4>{{{0, 0}, {960, 0}, {0, 600}, {960, 600}}}) {
                audit::drawableWidth = size[0];
                audit::drawableHeight = size[1];
                r.begin(s.hill(), s.frame(), 0, 0, false);
                r.overlay();
                r.text("minimized", 10, 10, 20);
                r.finish();
                audit::balanced();
                audit::require(r.width > 0 && r.height > 0 && std::isfinite(r.uiScale) && r.uiScale > 0,
                               "Invalid drawable/UI size");
            }
        }
        audit::require(audit::lists.empty() && audit::textures.empty(),
                       "Renderer leaked graphics resources at destruction");
        {
            sjp::Renderer r(nullptr, SJP_TEST_ASSET_DIR);
            auto visit = [&](int hi) {
                sjp::Simulation s(hi, 0);
                r.begin(s.hill(), s.frame(), 0, 0, false);
                r.overlay();
                r.finish();
                audit::balanced();
                audit::require(audit::lists.size() <= 2, "Cache limit exceeded");
            };
            visit(0);
            const auto first = audit::nextList - 1;
            visit(1);
            const auto second = audit::nextList - 1;
            visit(0);
            audit::require(audit::nextList == second + 1, "Cache hit rebuilt the current venue");
            visit(2);
            const auto third = audit::nextList - 1;
            audit::require(audit::lists.count(first) && !audit::lists.count(second) &&
                               audit::lists.count(third),
                           "Cache did not evict least recently used venue");
            visit(1);
            audit::require(audit::nextList == third + 2 && !audit::lists.count(first) &&
                               audit::lists.count(third),
                           "Evicted venue was not rebuilt correctly");
            const auto beforeCompletion = audit::completedFrames;
            r.setSynchronousRendering(true);
            visit(0);
            visit(1);
            audit::require(audit::completedFrames == beforeCompletion + 2,
                           "Synchronous frames left graphics work pending");
            r.setSynchronousRendering(false);
            visit(2);
            audit::require(audit::completedFrames == beforeCompletion + 2,
                           "Normal rendering unexpectedly forced completion");
        }
        audit::require(audit::lists.empty() && audit::textures.empty(),
                       "Repeated renderer lifetime leaked resources");
        {
            sjp::Renderer r(nullptr, SJP_TEST_ASSET_DIR);
            sjp::Simulation sim(2, 0);
            uint32_t wet = 0, dry = 0;
            while (!sjp::visual::snowForJump(wet))
                ++wet;
            while (sjp::visual::snowForJump(dry))
                ++dry;
            const auto input = sim.frame();
            const auto hashAt = [&](uint32_t seed, float windAngle, float elapsed, double wallTime) {
                r.setJumpWeather(seed);
                auto f = input;
                f.windSpeed = 3;
                f.windAngle = windAngle;
                f.elapsed = elapsed;
                r.resetCamera();
                r.begin(sim.hill(), f, wallTime, 0, false); // Ensure the terrain is cached.
                r.resetCamera();
                const auto vertices = audit::particleVertices;
                audit::particlePositionHash = 1469598103934665603ULL;
                r.begin(sim.hill(), f, wallTime, 0, false);
                audit::balanced();
                // Fan meshes contain snow and the unchanged jumper shadow;
                // skip trees, flags and crowd animation driven by wall time.
                return std::pair<size_t, uint64_t>{audit::particleVertices - vertices,
                                                   audit::particlePositionHash};
            };
            const auto noSnow = hashAt(dry, 0, 0, 0);
            const auto snow = hashAt(wet, 0, 0, 0);
            audit::require(snow.first > noSnow.first && snow.first - noSnow.first <= 800,
                           "Light snowfall missing or particle budget exceeded");
            audit::require(hashAt(wet, 0, 0, 90) == snow, "Paused snowfall changes with wall clock");
            audit::require(hashAt(wet, float(sjp::PI), 0, 0).second != snow.second,
                           "Snowfall ignores wind direction");
            for (float seconds : {1.f, 30.f, 80.f, 150.f}) {
                const auto snowyFrame = hashAt(wet, 0, seconds, seconds);
                const auto clearFrame = hashAt(dry, 0, seconds, seconds);
                audit::require(snowyFrame.first > clearFrame.first, "Snowfall stops during a selected jump");
            }
            const auto after = sim.frame();
            audit::require(input.x == after.x && input.y == after.y && input.pose == after.pose &&
                               input.windAngle == after.windAngle && input.windSpeed == after.windSpeed &&
                               sim.originalCounter == 1,
                           "Visual weather mutated the simulation");
        }
        audit::require(leaks == 0, "Cached terrain freezes the caller's current color/normal/UV");
        audit::require(audit::maximumLists <= 2, "Terrain cache retains more than two venues");
        std::cout << "PASS: finite commands, balanced stacks, bounded cache, live current attributes, "
                     "zero-size window, resource cleanup; no GPU used\n";
    } catch (const std::exception &e) {
        std::cerr << "Command audit: " << e.what() << '\n';
        return 1;
    }
}
