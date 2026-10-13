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
    std::array<float, 4> color{1, 1, 1, 1};
    std::array<float, 3> normal{0, 0, 1};
    std::array<float, 4> uv{0, 0, 0, 1};
};
Current current;
struct Saved {
    sjp::gfx::Flags mask;
    Current value;
    bool depth;
};
std::vector<Saved> attributes;
std::map<sjp::gfx::Enum, int> matrices{{sjp::gfx::MODELVIEW, 1}, {sjp::gfx::PROJECTION, 1}, {sjp::gfx::TEXTURE, 1}};
sjp::gfx::Enum matrix = sjp::gfx::MODELVIEW, primitiveMode = sjp::gfx::TRIANGLES;
bool primitive = false, depth = true;
sjp::gfx::Id compiling = 0, nextList = 1, nextTexture = 1;
struct List {
    size_t vertices = 0, commands = 0;
    std::vector<std::function<void()>> tail;
};
std::map<sjp::gfx::Id, List> lists;
std::set<sjp::gfx::Id> textures;
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
        if (!compiling && primitiveMode == sjp::gfx::TRIANGLE_FAN)
            particlePositionHash = (particlePositionHash ^ b) * 1099511628211ULL;
    }
}
void vertex() {
    require(primitive, "Vertex outside sjp::gfx::begin/sjp::gfx::end");
    ++vertices;
    ++drawVertices;
    if (!compiling && primitiveMode == sjp::gfx::TRIANGLE_FAN)
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
bool SDL_GetWindowSizeInPixels(SDL_Window *, int *w, int *h) {
    *w = audit::drawableWidth;
    *h = audit::drawableHeight;
    return true;
}
void sjp::gfx::initialize(SDL_Window *, bool, bool, bool) {}
void sjp::gfx::shutdown() noexcept {}
void sjp::gfx::beginFrame(int, int) {}
void sjp::gfx::captureImage(const std::string &) {}
int sjp::gfx::sampleCount() { return 4; }
void sjp::gfx::present(bool synchronous) { if (synchronous) sjp::gfx::finish(); }
void sjp::gfx::finish(void) {
    audit::command("sjp::gfx::finish inside sjp::gfx::begin", false);
    audit::require(!audit::compiling && audit::attributes.empty(),
                   "Frame completed before display list/attribute restoration");
    ++audit::completedFrames;
}
void sjp::gfx::begin(sjp::gfx::Enum mode) {
    audit::command("sjp::gfx::begin inside sjp::gfx::begin", false);
    audit::require(!audit::primitive, "Nested sjp::gfx::begin");
    audit::primitive = true;
    audit::primitiveMode = mode;
    audit::drawVertices = 0;
}
void sjp::gfx::bindTexture(sjp::gfx::Enum target, sjp::gfx::Id texture) {
    audit::command("sjp::gfx::bindTexture inside sjp::gfx::begin", false);
    audit::require(!texture || audit::textures.count(texture), "Bind unknown texture");
}
void sjp::gfx::blendFunc(sjp::gfx::Enum sfactor, sjp::gfx::Enum dfactor) {
    audit::command("sjp::gfx::blendFunc inside sjp::gfx::begin", false);
}
void sjp::gfx::callList(sjp::gfx::Id list) {
    audit::command("sjp::gfx::callList inside sjp::gfx::begin", false);
    audit::require(audit::lists.count(list), "Use of missing display list");
    for (auto &op : audit::lists.at(list).tail)
        op();
}
void sjp::gfx::clear(sjp::gfx::Flags mask) {
    audit::command("sjp::gfx::clear inside sjp::gfx::begin", false);
}
void sjp::gfx::clearColor(float red, float green, float blue, float alpha) {
    audit::command("sjp::gfx::clearColor inside sjp::gfx::begin", false);
    audit::finite(red);
    audit::finite(green);
    audit::finite(blue);
    audit::finite(alpha);
}
void sjp::gfx::color4f(float red, float green, float blue, float alpha) {
    audit::command("sjp::gfx::color4f inside sjp::gfx::begin", true);
    audit::finite(red);
    audit::finite(green);
    audit::finite(blue);
    audit::finite(alpha);
    audit::current.color = {red, green, blue, alpha};
    auto value = audit::current.color;
    audit::tail([value] { audit::current.color = value; });
}
void sjp::gfx::color4fv(const float *v) {
    audit::command("sjp::gfx::color4fv inside sjp::gfx::begin", true);
    for (int i = 0; i < 4; ++i)
        audit::finite(v[i]);
    std::copy(v, v + 4, audit::current.color.begin());
    auto value = audit::current.color;
    audit::tail([value] { audit::current.color = value; });
}
void sjp::gfx::colorMaterial(sjp::gfx::Enum face, sjp::gfx::Enum mode) {
    audit::command("sjp::gfx::colorMaterial inside sjp::gfx::begin", false);
}
void sjp::gfx::deleteLists(sjp::gfx::Id list, int range) {
    audit::command("sjp::gfx::deleteLists inside sjp::gfx::begin", false);
    for (int i = 0; i < range; ++i)
        audit::require(audit::lists.erase(list + i) == 1, "Invalid display list deletion");
}
void sjp::gfx::deleteTextures(int n, const sjp::gfx::Id *textures) {
    audit::command("sjp::gfx::deleteTextures inside sjp::gfx::begin", false);
    for (int i = 0; i < n; ++i)
        if (textures[i])
            audit::require(audit::textures.erase(textures[i]) == 1, "Invalid texture deletion");
}
void sjp::gfx::depthMask(bool flag) {
    audit::command("sjp::gfx::depthMask inside sjp::gfx::begin", false);
    audit::depth = flag;
}
void sjp::gfx::disable(sjp::gfx::Enum cap) {
    audit::command("sjp::gfx::disable inside sjp::gfx::begin", false);
}
void sjp::gfx::enable(sjp::gfx::Enum cap) {
    audit::command("sjp::gfx::enable inside sjp::gfx::begin", false);
}
void sjp::gfx::end(void) {
    audit::command("sjp::gfx::end inside sjp::gfx::begin", true);
    audit::require(audit::primitive, "Unmatched sjp::gfx::end");
    audit::primitive = false;
    audit::maximumDraw = std::max(audit::maximumDraw, audit::drawVertices);
}
void sjp::gfx::endList(void) {
    audit::command("sjp::gfx::endList inside sjp::gfx::begin", false);
    audit::require(audit::compiling && !audit::primitive, "Unmatched sjp::gfx::endList");
    audit::maximumCachedVertices = std::max(audit::maximumCachedVertices, audit::cached());
    audit::compiling = 0;
}
void sjp::gfx::fogf(sjp::gfx::Enum pname, float param) {
    audit::command("sjp::gfx::fogf inside sjp::gfx::begin", false);
    audit::finite(param);
}
void sjp::gfx::fogfv(sjp::gfx::Enum pname, const float *params) {
    audit::command("sjp::gfx::fogfv inside sjp::gfx::begin", false);
    for (int i = 0; i < 4; ++i)
        audit::finite(params[i]);
}
void sjp::gfx::fogi(sjp::gfx::Enum pname, int param) {
    audit::command("sjp::gfx::fogi inside sjp::gfx::begin", false);
}
void sjp::gfx::frustum(double left, double right, double bottom, double top, double near_val,
                          double far_val) {
    audit::command("sjp::gfx::frustum inside sjp::gfx::begin", false);
    audit::finite(left);
    audit::finite(right);
    audit::finite(bottom);
    audit::finite(top);
    audit::finite(near_val);
    audit::finite(far_val);
    audit::require(left != right && bottom != top && near_val > 0 && far_val > near_val,
                   "Invalid perspective projection");
}
sjp::gfx::Id sjp::gfx::genLists(int range) {
    audit::command("sjp::gfx::genLists inside sjp::gfx::begin", false);
    audit::require(range > 0, "Invalid list range");
    auto id = audit::nextList;
    audit::nextList += range;
    return id;
}
void sjp::gfx::genTextures(int n, sjp::gfx::Id *textures) {
    audit::command("sjp::gfx::genTextures inside sjp::gfx::begin", false);
    for (int i = 0; i < n; ++i) {
        textures[i] = audit::nextTexture++;
        audit::textures.insert(textures[i]);
    }
}
void sjp::gfx::getFloatv(sjp::gfx::Enum pname, float *params) {
    audit::command("sjp::gfx::getFloatv inside sjp::gfx::begin", false);
    if (pname == sjp::gfx::CURRENT_COLOR)
        std::copy(audit::current.color.begin(), audit::current.color.end(), params);
    else if (pname == sjp::gfx::CURRENT_NORMAL)
        std::copy(audit::current.normal.begin(), audit::current.normal.end(), params);
    else if (pname == sjp::gfx::CURRENT_TEXTURE_COORDS)
        std::copy(audit::current.uv.begin(), audit::current.uv.end(), params);
    else
        throw std::runtime_error("Unsupported audit sjp::gfx::getFloatv");
}
void sjp::gfx::getIntegerv(sjp::gfx::Enum pname, int *params) {
    audit::command("sjp::gfx::getIntegerv inside sjp::gfx::begin", false);
    audit::require(pname == sjp::gfx::UNPACK_ALIGNMENT, "Unsupported audit sjp::gfx::getIntegerv");
    *params = 1;
}
const unsigned char *sjp::gfx::getString(sjp::gfx::Enum name) {
    audit::command("sjp::gfx::getString inside sjp::gfx::begin", false);
    return reinterpret_cast<const unsigned char *>("CPU command audit; no graphics context");
}
void sjp::gfx::hint(sjp::gfx::Enum target, sjp::gfx::Enum mode) {
    audit::command("sjp::gfx::hint inside sjp::gfx::begin", false);
}
void sjp::gfx::lightModelfv(sjp::gfx::Enum pname, const float *params) {
    audit::command("sjp::gfx::lightModelfv inside sjp::gfx::begin", false);
    for (int i = 0; i < 4; ++i)
        audit::finite(params[i]);
}
void sjp::gfx::lightfv(sjp::gfx::Enum light, sjp::gfx::Enum pname, const float *params) {
    audit::command("sjp::gfx::lightfv inside sjp::gfx::begin", false);
    for (int i = 0; i < 4; ++i)
        audit::finite(params[i]);
}
void sjp::gfx::lineWidth(float width) {
    audit::command("sjp::gfx::lineWidth inside sjp::gfx::begin", false);
    audit::finite(width);
}
void sjp::gfx::loadIdentity(void) {
    audit::command("sjp::gfx::loadIdentity inside sjp::gfx::begin", false);
}
void sjp::gfx::materialf(sjp::gfx::Enum face, sjp::gfx::Enum pname, float param) {
    audit::command("sjp::gfx::materialf inside sjp::gfx::begin", false);
    audit::finite(param);
}
void sjp::gfx::materialfv(sjp::gfx::Enum face, sjp::gfx::Enum pname, const float *params) {
    audit::command("sjp::gfx::materialfv inside sjp::gfx::begin", false);
    for (int i = 0; i < 4; ++i)
        audit::finite(params[i]);
}
void sjp::gfx::matrixMode(sjp::gfx::Enum mode) {
    audit::command("sjp::gfx::matrixMode inside sjp::gfx::begin", false);
    audit::require(audit::matrices.count(mode), "Invalid matrix mode");
    audit::matrix = mode;
}
void sjp::gfx::multMatrixf(const float *m) {
    audit::command("sjp::gfx::multMatrixf inside sjp::gfx::begin", false);
    for (int i = 0; i < 16; ++i)
        audit::finite(m[i]);
}
void sjp::gfx::newList(sjp::gfx::Id list, sjp::gfx::Enum mode) {
    audit::command("sjp::gfx::newList inside sjp::gfx::begin", false);
    audit::require(!audit::compiling && list != 0 && mode == sjp::gfx::COMPILE_AND_EXECUTE,
                   "Invalid list compilation");
    audit::compiling = list;
    audit::lists[list] = {};
    audit::maximumLists = std::max(audit::maximumLists, audit::lists.size());
}
void sjp::gfx::normal3d(double nx, double ny, double nz) {
    audit::command("sjp::gfx::normal3d inside sjp::gfx::begin", true);
    audit::finite(nx);
    audit::finite(ny);
    audit::finite(nz);
    audit::current.normal = {float(nx), float(ny), float(nz)};
    auto value = audit::current.normal;
    audit::tail([value] { audit::current.normal = value; });
}
void sjp::gfx::normal3dv(const double *v) {
    audit::command("sjp::gfx::normal3dv inside sjp::gfx::begin", true);
    for (int i = 0; i < 3; ++i) {
        audit::finite(v[i]);
        audit::current.normal[i] = v[i];
    }
    auto value = audit::current.normal;
    audit::tail([value] { audit::current.normal = value; });
}
void sjp::gfx::ortho(double left, double right, double bottom, double top, double near_val,
                        double far_val) {
    audit::command("sjp::gfx::ortho inside sjp::gfx::begin", false);
    audit::finite(left);
    audit::finite(right);
    audit::finite(bottom);
    audit::finite(top);
    audit::finite(near_val);
    audit::finite(far_val);
    audit::require(left != right && bottom != top && near_val != far_val, "Invalid orthographic projection");
}
void sjp::gfx::pixelStorei(sjp::gfx::Enum pname, int param) {
    audit::command("sjp::gfx::pixelStorei inside sjp::gfx::begin", false);
}
void sjp::gfx::polygonOffset(float factor, float units) {
    audit::command("sjp::gfx::polygonOffset inside sjp::gfx::begin", false);
    audit::finite(factor);
    audit::finite(units);
}
void sjp::gfx::popAttrib(void) {
    audit::command("sjp::gfx::popAttrib inside sjp::gfx::begin", false);
    audit::require(!audit::attributes.empty(), "Attribute stack underflow");
    auto saved = audit::attributes.back();
    audit::attributes.pop_back();
    if (saved.mask & sjp::gfx::CURRENT_BIT)
        audit::current = saved.value;
    if (saved.mask & sjp::gfx::DEPTH_BUFFER_BIT)
        audit::depth = saved.depth;
}
void sjp::gfx::popMatrix(void) {
    audit::command("sjp::gfx::popMatrix inside sjp::gfx::begin", false);
    audit::require(--audit::matrices[audit::matrix] >= 1, "Matrix stack underflow");
}
void sjp::gfx::pushAttrib(sjp::gfx::Flags mask) {
    audit::command("sjp::gfx::pushAttrib inside sjp::gfx::begin", false);
    audit::attributes.push_back({mask, audit::current, audit::depth});
    audit::require(audit::attributes.size() <= 16, "Attribute stack exceeds guaranteed capacity");
}
void sjp::gfx::pushMatrix(void) {
    audit::command("sjp::gfx::pushMatrix inside sjp::gfx::begin", false);
    ++audit::matrices[audit::matrix];
    audit::require(audit::matrices[audit::matrix] <= (audit::matrix == sjp::gfx::MODELVIEW ? 32 : 2),
                   "Matrix stack overflow");
}

void sjp::gfx::scalef(float x, float y, float z) {
    audit::command("sjp::gfx::scalef inside sjp::gfx::begin", false);
    audit::finite(x);
    audit::finite(y);
    audit::finite(z);
}
void sjp::gfx::shadeModel(sjp::gfx::Enum mode) {
    audit::command("sjp::gfx::shadeModel inside sjp::gfx::begin", false);
}
void sjp::gfx::texCoord2d(double s, double t) {
    audit::command("sjp::gfx::texCoord2d inside sjp::gfx::begin", true);
    audit::finite(s);
    audit::finite(t);
    audit::current.uv = {float(s), float(t), 0, 1};
    auto value = audit::current.uv;
    audit::tail([value] { audit::current.uv = value; });
}
void sjp::gfx::texCoord2dv(const double *v) {
    audit::command("sjp::gfx::texCoord2dv inside sjp::gfx::begin", true);
    for (int i = 0; i < 2; ++i)
        audit::finite(v[i]);
    audit::current.uv = {float(v[0]), float(v[1]), 0, 1};
    auto value = audit::current.uv;
    audit::tail([value] { audit::current.uv = value; });
}
void sjp::gfx::texCoord2f(float s, float t) {
    audit::command("sjp::gfx::texCoord2f inside sjp::gfx::begin", true);
    audit::finite(s);
    audit::finite(t);
    audit::current.uv = {s, t, 0, 1};
    auto value = audit::current.uv;
    audit::tail([value] { audit::current.uv = value; });
}
void sjp::gfx::texEnvi(sjp::gfx::Enum target, sjp::gfx::Enum pname, int param) {
    audit::command("sjp::gfx::texEnvi inside sjp::gfx::begin", false);
}
void sjp::gfx::texImage2D(sjp::gfx::Enum target, int level, int internalFormat, int width, int height,
                             int border, sjp::gfx::Enum format, sjp::gfx::Enum type, const void *pixels) {
    audit::command("sjp::gfx::texImage2D inside sjp::gfx::begin", false);
    audit::require(width > 0 && height > 0 && pixels, "Invalid texture upload");
    audit::textureBytes += size_t(width) * height;
}
void sjp::gfx::texParameteri(sjp::gfx::Enum target, sjp::gfx::Enum pname, int param) {
    audit::command("sjp::gfx::texParameteri inside sjp::gfx::begin", false);
}
void sjp::gfx::translated(double x, double y, double z) {
    audit::command("sjp::gfx::translated inside sjp::gfx::begin", false);
    audit::finite(x);
    audit::finite(y);
    audit::finite(z);
}
void sjp::gfx::translatef(float x, float y, float z) {
    audit::command("sjp::gfx::translatef inside sjp::gfx::begin", false);
    audit::finite(x);
    audit::finite(y);
    audit::finite(z);
}
void sjp::gfx::vertex2f(float x, float y) {
    audit::position(x, y, 0);
    audit::command("sjp::gfx::vertex2f inside sjp::gfx::begin", true);
    audit::finite(x);
    audit::finite(y);
    audit::vertex();
}
void sjp::gfx::vertex3d(double x, double y, double z) {
    audit::position(x, y, z);
    audit::command("sjp::gfx::vertex3d inside sjp::gfx::begin", true);
    audit::finite(x);
    audit::finite(y);
    audit::finite(z);
    audit::vertex();
}
void sjp::gfx::vertex3dv(const double *v) {
    audit::position(v[0], v[1], v[2]);
    audit::command("sjp::gfx::vertex3dv inside sjp::gfx::begin", true);
    for (int i = 0; i < 3; ++i)
        audit::finite(v[i]);
    audit::vertex();
}
void sjp::gfx::viewport(int x, int y, int width, int height) {
    audit::command("sjp::gfx::viewport inside sjp::gfx::begin", false);
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
                sjp::gfx::callList(id);
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
