#include "draw_commands.h"
#include "draw_internal.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <stdexcept>

namespace sjp::gfx {
namespace {
Matrix identity() {
    Matrix m{};
    m[0] = m[5] = m[10] = m[15] = 1;
    return m;
}
Matrix multiply(const Matrix &a, const Matrix &b) {
    Matrix result{};
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            for (int k = 0; k < 4; ++k)
                result[column * 4 + row] += a[k * 4 + row] * b[column * 4 + k];
    return result;
}
std::array<float, 4> transform(const Matrix &m, const std::array<float, 4> &v) {
    std::array<float, 4> result{};
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
            result[row] += m[column * 4 + row] * v[column];
    return result;
}
Matrix normalMatrix(const Matrix &m) {
    const float a = m[0], b = m[4], c = m[8], d = m[1], e = m[5], f = m[9], g = m[2], h = m[6], i = m[10];
    const float determinant = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (std::abs(determinant) < 1e-20f)
        throw std::runtime_error("Singular drawing transform");
    Matrix result = identity();
    result[0] = (e * i - f * h) / determinant;
    result[1] = (c * h - b * i) / determinant;
    result[2] = (b * f - c * e) / determinant;
    result[4] = (f * g - d * i) / determinant;
    result[5] = (a * i - c * g) / determinant;
    result[6] = (c * d - a * f) / determinant;
    result[8] = (d * h - e * g) / determinant;
    result[9] = (b * g - a * h) / determinant;
    result[10] = (a * e - b * d) / determinant;
    return result;
}
struct Light {
    std::array<float, 4> position{0, 0, 1, 0}, ambient{}, diffuse{}, specular{};
};
struct State {
    PipelineState pipeline;
    bool lighting = false, light0 = false, light1 = false, fog = false, texture = false, normalize = false,
         colorMaterial = false;
    Id textureId = 0;
    std::array<float, 4> ambient{.2f, .2f, .2f, 1}, specular{}, emission{}, fogColor{};
    std::array<Light, 2> lights{};
    std::array<std::array<float, 4>, ARENA_LIGHT_COUNT> spotPositionRange{}, spotDirectionCone{};
    std::array<float, 4> arenaAxis{}, arenaParameters{}, arenaColor{}, worldZAxis{};
    std::array<std::array<float, 4>, ARENA_LIGHT_COUNT> inrunPositionRange{}, inrunDirectionCone{};
    std::array<float, 4> inrunParameters{}, inrunColor{};
    std::array<float, 4> signalPositionRange{}, signalDirectionCone{}, signalColor{}, signalParameters{};
    float shininess = 0, fogStart = 0, fogEnd = 1, lineWidth = 1;
};
struct SavedState {
    Flags mask;
    State state;
    Vertex current;
};
struct Context;
using Command = std::function<void(Context &)>;
struct CachedMesh {
    std::shared_ptr<Mesh> mesh = std::make_shared<Mesh>();
    std::vector<Command> commands;
};
struct Context {
    // Resources below are destroyed before the backend and its device.
    VulkanBackend backend;
    State state;
    Vertex current;
    Matrix projection = identity(), modelView = identity();
    Enum matrixTarget = MODELVIEW, primitiveMode = TRIANGLES;
    std::vector<Matrix> projectionStack, modelViewStack;
    std::vector<SavedState> attributes;
    std::vector<Vertex> primitive, dynamic;
    std::vector<Packet> packets;
    std::map<Id, std::shared_ptr<Image>> images;
    std::map<Id, std::shared_ptr<CachedMesh>> meshes;
    std::shared_ptr<CachedMesh> compiling;
    bool inPrimitive = false;
    Id nextImage = 1, nextMesh = 1;
    int width = 1, height = 1, unpackAlignment = 4, packAlignment = 4;
    bool frustumCulling = true;
    uint64_t culledIndices = 0;
    std::array<float, 4> background{};

    Context(SDL_Window *window, bool noMsaa, bool vsync, bool validation)
        : backend(window, noMsaa, vsync, validation) {
        state.lights[0].diffuse = state.lights[0].specular = {1, 1, 1, 1};
    }
    Matrix &matrix() { return matrixTarget == PROJECTION ? projection : modelView; }
    std::vector<Matrix> &matrixStack() {
        return matrixTarget == PROJECTION ? projectionStack : modelViewStack;
    }
    Uniforms uniforms() const {
        Uniforms u{};
        u.modelView = modelView;
        u.projection = projection;
        u.normalMatrix = normalMatrix(modelView);
        u.ambient = state.ambient;
        for (int component = 0; component < 3; ++component)
            u.ambient[component] += (state.light0 ? state.lights[0].ambient[component] : 0) +
                                    (state.light1 ? state.lights[1].ambient[component] : 0);
        u.direction0 = state.lights[0].position;
        u.direction1 = state.lights[1].position;
        u.direction0[3] = state.light0;
        u.direction1[3] = state.light1;
        u.diffuse0 = state.lights[0].diffuse;
        u.diffuse1 = state.lights[1].diffuse;
        u.specularLight0 = state.light0 ? state.lights[0].specular : std::array<float, 4>{};
        u.materialSpecular = state.specular;
        u.emission = state.emission;
        u.fogColor = state.fogColor;
        u.parameters = {state.shininess, state.fogStart, state.fogEnd, state.lineWidth};
        u.flags = {float(state.lighting), float(state.fog), float(state.texture), 0};
        u.viewport = {float(width), float(height), 1.f / width, 1.f / height};
        u.spotPositionRange = state.spotPositionRange;
        u.spotDirectionCone = state.spotDirectionCone;
        u.arenaAxis = state.arenaAxis;
        u.arenaParameters = state.arenaParameters;
        u.arenaColor = state.arenaColor;
        u.worldZAxis = state.worldZAxis;
        u.inrunPositionRange = state.inrunPositionRange;
        u.inrunDirectionCone = state.inrunDirectionCone;
        u.inrunParameters = state.inrunParameters;
        u.inrunColor = state.inrunColor;
        u.signalPositionRange = state.signalPositionRange;
        u.signalDirectionCone = state.signalDirectionCone;
        u.signalColor = state.signalColor;
        u.signalParameters = state.signalParameters;
        return u;
    }
    void packet(const std::shared_ptr<Mesh> &mesh, uint32_t first, uint32_t count) {
        if (!count)
            return;
        Packet packet;
        packet.mesh = mesh;
        packet.first = first;
        packet.count = count;
        packet.uniforms = uniforms();
        packet.pipeline = state.pipeline;
        if (state.texture) {
            const auto image = images.find(state.textureId);
            if (image == images.end())
                throw std::runtime_error("Drawing with an unavailable texture");
            packet.image = image->second;
        }
        if (!packets.empty()) {
            auto &prior = packets.back();
            if (prior.mesh == packet.mesh && prior.image == packet.image &&
                prior.first + prior.count == first && prior.uniforms == packet.uniforms &&
                prior.pipeline == packet.pipeline) {
                prior.count += count;
                return;
            }
        }
        packets.push_back(std::move(packet));
    }
    void emit(const std::shared_ptr<Mesh> &mesh, uint32_t first, uint32_t count) {
        if (!mesh || !mesh->indexed || !frustumCulling || !count) {
            packet(mesh, first, count);
            return;
        }
        const Frustum view(projection, modelView);
        auto range =
            std::lower_bound(mesh->ranges.begin(), mesh->ranges.end(), first,
                             [](const MeshRange &r, uint32_t at) { return r.first + r.count <= at; });
        const uint32_t end = first + count;
        uint32_t runFirst = first, runCount = 0;
        for (; range != mesh->ranges.end() && range->first < end; ++range) {
            const uint32_t a = std::max(first, range->first), b = std::min(end, range->first + range->count);
            if (view.intersects(range->bounds)) {
                if (!runCount)
                    runFirst = a;
                runCount += b - a;
            } else {
                if (runCount)
                    packet(mesh, runFirst, runCount);
                runCount = 0;
                culledIndices += b - a;
            }
        }
        if (runCount)
            packet(mesh, runFirst, runCount);
    }
};
thread_local std::unique_ptr<Context> context;
Context &ctx() {
    if (!context)
        throw std::runtime_error("Drawing context is unavailable");
    return *context;
}
void apply(Command command) {
    auto &c = ctx();
    command(c);
    if (c.compiling)
        c.compiling->commands.push_back(std::move(command));
}
void setCapability(State &s, Enum cap, bool enabled) {
    switch (cap) {
    case DEPTH_TEST:
        s.pipeline.depthTest = enabled;
        break;
    case BLEND:
        s.pipeline.blend = enabled;
        break;
    case CULL_FACE:
        s.pipeline.cull = enabled;
        break;
    case POLYGON_OFFSET_FILL:
        s.pipeline.offset = enabled;
        break;
    case LIGHTING:
        s.lighting = enabled;
        break;
    case LIGHT0:
        s.light0 = enabled;
        break;
    case LIGHT1:
        s.light1 = enabled;
        break;
    case FOG:
        s.fog = enabled;
        break;
    case TEXTURE_2D:
        s.texture = enabled;
        break;
    case NORMALIZE:
        s.normalize = enabled;
        break;
    case COLOR_MATERIAL:
        s.colorMaterial = enabled;
        break;
    default:
        throw std::runtime_error("Unsupported scene capability");
    }
}
std::array<float, 4> array4(const float *v) {
    return {v[0], v[1], v[2], v[3]};
}
float finite(double value) {
    if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
        throw std::runtime_error("Non-finite scene coordinate");
    return float(value);
}
template <typename Operation> void changeCurrent(Operation command) {
    auto &c = ctx();
    if (c.inPrimitive)
        command(c.current);
    else
        apply([command](Context &c) { command(c.current); });
}
} // namespace

void initialize(SDL_Window *window, bool noMsaa, bool vsync, bool validation) {
    if (context)
        throw std::runtime_error("A drawing context already exists on this thread");
    context = std::make_unique<Context>(window, noMsaa, vsync, validation);
}
void shutdown() noexcept {
    context.reset();
}
void beginFrame(int width, int height) {
    auto &c = ctx();
    if (c.inPrimitive || c.compiling || !c.attributes.empty() || !c.modelViewStack.empty() ||
        !c.projectionStack.empty())
        throw std::runtime_error("Unbalanced scene state at frame boundary");
    c.backend.beginFrame();
    c.packets.clear();
    c.dynamic.clear();
    c.culledIndices = 0;
    c.width = std::max(1, width);
    c.height = std::max(1, height);
}
void present(bool synchronous) {
    auto &c = ctx();
    if (c.inPrimitive || c.compiling || !c.attributes.empty())
        throw std::runtime_error("Unbalanced scene state before presentation");
    c.backend.render(c.width, c.height, c.background, c.packets, c.dynamic, true, synchronous, nullptr);
}
void captureImage(const std::string &path) {
    auto &c = ctx();
    c.backend.render(c.width, c.height, c.background, c.packets, c.dynamic, false, true, &path);
}
int sampleCount() {
    return ctx().backend.samples();
}
void setFrustumCulling(bool enabled) {
    ctx().frustumCulling = enabled;
}
bool boundsVisible(const std::array<float, 3> &low, const std::array<float, 3> &high) {
    const auto &c = ctx();
    return !c.frustumCulling || Frustum(c.projection, c.modelView).intersects({low, high, false});
}
FrameStats frameStats() {
    const auto &c = ctx();
    FrameStats stats;
    stats.culledIndices = c.culledIndices;
    stats.dynamicVertices = c.dynamic.size();
    stats.drawCalls = c.packets.size();
    for (const auto &entry : c.meshes) {
        stats.cachedVertices += entry.second->mesh->vertexCount;
        stats.cachedIndices += entry.second->mesh->indexCount;
    }
    for (const auto &packet : c.packets)
        if (packet.mesh)
            stats.submittedIndices += packet.count;
    return stats;
}
void finish() {
    ctx().backend.wait();
}

void begin(Enum mode) {
    auto &c = ctx();
    if (c.inPrimitive)
        throw std::runtime_error("Nested scene primitive");
    c.inPrimitive = true;
    c.primitiveMode = mode;
    c.primitive.clear();
}
void end() {
    auto &c = ctx();
    if (!c.inPrimitive)
        throw std::runtime_error("Unmatched scene primitive end");
    c.inPrimitive = false;
    const auto mesh = c.compiling ? c.compiling->mesh : nullptr;
    auto &target = mesh ? mesh->vertices : c.dynamic;
    if (target.size() > std::numeric_limits<uint32_t>::max() - c.primitive.size() * 6)
        throw std::runtime_error("Scene vertex buffer is too large");
    const uint32_t first = uint32_t(target.size());
    auto triangle = [&](size_t a, size_t b, size_t d) {
        target.push_back(c.primitive[a]);
        target.push_back(c.primitive[b]);
        target.push_back(c.primitive[d]);
    };
    const size_t count = c.primitive.size();
    switch (c.primitiveMode) {
    case TRIANGLES:
        for (size_t i = 0; i + 2 < count; i += 3)
            triangle(i, i + 1, i + 2);
        break;
    case QUADS:
        for (size_t i = 0; i + 3 < count; i += 4) {
            triangle(i, i + 1, i + 2);
            triangle(i, i + 2, i + 3);
        }
        break;
    case QUAD_STRIP:
        for (size_t i = 0; i + 3 < count; i += 2) {
            triangle(i, i + 1, i + 3);
            triangle(i, i + 3, i + 2);
        }
        break;
    case TRIANGLE_FAN:
        for (size_t i = 1; i + 1 < count; ++i)
            triangle(0, i, i + 1);
        break;
    case TRIANGLE_STRIP:
        for (size_t i = 0; i + 2 < count; ++i)
            i % 2 ? triangle(i + 1, i, i + 2) : triangle(i, i + 1, i + 2);
        break;
    case LINES:
        // Expand wide lines in the vertex shader; Vulkan wideLines is optional.
        for (size_t i = 0; i + 1 < count; i += 2) {
            Vertex a = c.primitive[i], b = c.primitive[i + 1];
            a.lineOther = b.position;
            b.lineOther = a.position;
            Vertex aa = a, bb = b;
            a.lineSide = 1;
            aa.lineSide = -1;
            b.lineSide = -1;
            bb.lineSide = 1;
            for (const auto &v : {a, b, bb, a, bb, aa})
                target.push_back(v);
        }
        break;
    default:
        throw std::runtime_error("Unsupported scene primitive topology");
    }
    const uint32_t emitted = uint32_t(target.size()) - first;
    if (mesh)
        appendMeshRanges(*mesh, first, emitted);
    const Vertex current = c.current;
    apply([mesh, first, emitted, current](Context &c) {
        c.current = current;
        c.emit(mesh, first, emitted);
    });
}
void color4f(float r, float g, float b, float a) {
    changeCurrent([=](Vertex &v) { v.color = {r, g, b, a}; });
}
void color4fv(const float *v) {
    color4f(v[0], v[1], v[2], v[3]);
}
void normal3d(double x, double y, double z) {
    const std::array<float, 3> n{finite(x), finite(y), finite(z)};
    changeCurrent([=](Vertex &v) { v.normal = n; });
}
void normal3dv(const double *v) {
    normal3d(v[0], v[1], v[2]);
}
void texCoord2d(double x, double y) {
    const std::array<float, 2> uv{finite(x), finite(y)};
    changeCurrent([=](Vertex &v) { v.uv = uv; });
}
void texCoord2f(float x, float y) {
    texCoord2d(x, y);
}
void texCoord2dv(const double *v) {
    texCoord2d(v[0], v[1]);
}
void vertex3d(double x, double y, double z) {
    auto &c = ctx();
    if (!c.inPrimitive)
        throw std::runtime_error("Scene vertex outside a primitive");
    Vertex v = c.current;
    v.position = {finite(x), finite(y), finite(z)};
    c.primitive.push_back(v);
}
void vertex3dv(const double *v) {
    vertex3d(v[0], v[1], v[2]);
}
void vertex2f(float x, float y) {
    vertex3d(x, y, 0);
}
void enable(Enum cap) {
    apply([=](Context &c) { setCapability(c.state, cap, true); });
}
void disable(Enum cap) {
    apply([=](Context &c) { setCapability(c.state, cap, false); });
}
void pushAttrib(Flags mask) {
    apply([=](Context &c) { c.attributes.push_back({mask, c.state, c.current}); });
}
void popAttrib() {
    apply([](Context &c) {
        if (c.attributes.empty())
            throw std::runtime_error("Scene attribute stack underflow");
        const auto saved = c.attributes.back();
        c.attributes.pop_back();
        auto &s = c.state;
        const auto &old = saved.state;
        if (saved.mask & ENABLE_BIT) {
            s.pipeline.depthTest = old.pipeline.depthTest;
            s.pipeline.blend = old.pipeline.blend;
            s.pipeline.cull = old.pipeline.cull;
            s.pipeline.offset = old.pipeline.offset;
            s.lighting = old.lighting;
            s.light0 = old.light0;
            s.light1 = old.light1;
            s.fog = old.fog;
            s.texture = old.texture;
            s.normalize = old.normalize;
            s.colorMaterial = old.colorMaterial;
        }
        if (saved.mask & CURRENT_BIT)
            c.current = saved.current;
        if (saved.mask & LIGHTING_BIT) {
            s.ambient = old.ambient;
            s.lights = old.lights;
            s.spotPositionRange = old.spotPositionRange;
            s.spotDirectionCone = old.spotDirectionCone;
            s.arenaAxis = old.arenaAxis;
            s.arenaParameters = old.arenaParameters;
            s.arenaColor = old.arenaColor;
            s.worldZAxis = old.worldZAxis;
            s.inrunPositionRange = old.inrunPositionRange;
            s.inrunDirectionCone = old.inrunDirectionCone;
            s.inrunParameters = old.inrunParameters;
            s.inrunColor = old.inrunColor;
            s.signalPositionRange = old.signalPositionRange;
            s.signalDirectionCone = old.signalDirectionCone;
            s.signalColor = old.signalColor;
            s.signalParameters = old.signalParameters;
            s.specular = old.specular;
            s.emission = old.emission;
            s.shininess = old.shininess;
            s.lighting = old.lighting;
            s.light0 = old.light0;
            s.light1 = old.light1;
            s.colorMaterial = old.colorMaterial;
        }
        if (saved.mask & TEXTURE_BIT) {
            s.textureId = old.textureId;
            s.texture = old.texture;
        }
        if (saved.mask & DEPTH_BUFFER_BIT) {
            s.pipeline.depthTest = old.pipeline.depthTest;
            s.pipeline.depthWrite = old.pipeline.depthWrite;
        }
        if (saved.mask & COLOR_BUFFER_BIT)
            s.pipeline.blend = old.pipeline.blend;
        if (saved.mask & POLYGON_BIT) {
            s.pipeline.cull = old.pipeline.cull;
            s.pipeline.offset = old.pipeline.offset;
            s.pipeline.offsetFactor = old.pipeline.offsetFactor;
            s.pipeline.offsetUnits = old.pipeline.offsetUnits;
        }
        if (saved.mask & LINE_BIT)
            s.lineWidth = old.lineWidth;
    });
}
void matrixMode(Enum target) {
    if (target != MODELVIEW && target != PROJECTION)
        throw std::runtime_error("Unknown scene matrix");
    apply([=](Context &c) { c.matrixTarget = target; });
}
void loadIdentity() {
    apply([](Context &c) { c.matrix() = identity(); });
}
void pushMatrix() {
    apply([](Context &c) { c.matrixStack().push_back(c.matrix()); });
}
void popMatrix() {
    apply([](Context &c) {
        auto &stack = c.matrixStack();
        if (stack.empty())
            throw std::runtime_error("Scene matrix stack underflow");
        c.matrix() = stack.back();
        stack.pop_back();
    });
}
void multMatrixf(const float *m) {
    Matrix value{};
    std::copy_n(m, 16, value.begin());
    apply([=](Context &c) { c.matrix() = multiply(c.matrix(), value); });
}
void translated(double x, double y, double z) {
    Matrix m = identity();
    m[12] = finite(x);
    m[13] = finite(y);
    m[14] = finite(z);
    multMatrixf(m.data());
}
void translatef(float x, float y, float z) {
    translated(x, y, z);
}
void scalef(float x, float y, float z) {
    Matrix m{};
    m[0] = x;
    m[5] = y;
    m[10] = z;
    m[15] = 1;
    multMatrixf(m.data());
}
void ortho(double left, double right, double bottom, double top, double near, double far) {
    Matrix m = identity();
    m[0] = finite(2 / (right - left));
    m[5] = finite(2 / (top - bottom));
    m[10] = finite(-2 / (far - near));
    m[12] = finite(-(right + left) / (right - left));
    m[13] = finite(-(top + bottom) / (top - bottom));
    m[14] = finite(-(far + near) / (far - near));
    multMatrixf(m.data());
}
void frustum(double left, double right, double bottom, double top, double near, double far) {
    Matrix m{};
    m[0] = finite(2 * near / (right - left));
    m[5] = finite(2 * near / (top - bottom));
    m[8] = finite((right + left) / (right - left));
    m[9] = finite((top + bottom) / (top - bottom));
    m[10] = finite(-(far + near) / (far - near));
    m[11] = -1;
    m[14] = finite(-2 * far * near / (far - near));
    multMatrixf(m.data());
}
void lightModelfv(Enum name, const float *v) {
    if (name != LIGHT_MODEL_AMBIENT)
        throw std::runtime_error("Unsupported ambient light");
    const auto value = array4(v);
    apply([=](Context &c) { c.state.ambient = value; });
}
void arenaLights(const std::array<SpotLight, ARENA_LIGHT_COUNT> &lights, float firstX, float spacing,
                 float strength, float attenuationDistance) {
    apply([=](Context &c) {
        auto &s = c.state;
        // Express world X as an eye-space plane once. This remains valid for
        // translated/scaled local athlete geometry as well as cached terrain.
        s.arenaAxis = transform(normalMatrix(c.modelView), {1, 0, 0, 0});
        const auto origin = transform(c.modelView, {0, 0, 0, 1});
        s.arenaAxis[3] =
            -(s.arenaAxis[0] * origin[0] + s.arenaAxis[1] * origin[1] + s.arenaAxis[2] * origin[2]);
        s.worldZAxis = transform(normalMatrix(c.modelView), {0, 0, 1, 0});
        s.worldZAxis[3] =
            -(s.worldZAxis[0] * origin[0] + s.worldZAxis[1] * origin[1] + s.worldZAxis[2] * origin[2]);
        s.arenaParameters = {firstX, 1 / spacing, 1 / (attenuationDistance * attenuationDistance), strength};
        s.arenaColor = {1, .95f, .84f, 0};
        for (size_t i = 0; i < lights.size(); ++i) {
            const auto &light = lights[i];
            auto position = light.positionRange, direction = light.directionCone;
            position[3] = 1;
            direction[3] = 0;
            s.spotPositionRange[i] = transform(c.modelView, position);
            s.spotDirectionCone[i] = transform(c.modelView, direction);
            s.spotPositionRange[i][3] = light.positionRange[3];
            s.spotDirectionCone[i][3] = light.directionCone[3];
        }
    });
}
void inrunLights(const std::array<SpotLight, ARENA_LIGHT_COUNT> &lights, float firstX, float spacing,
                 int columns, float strength, float attenuationDistance) {
    apply([=](Context &c) {
        auto &s = c.state;
        s.inrunParameters = {firstX, 1 / spacing, 1 / (attenuationDistance * attenuationDistance),
                             float(columns)};
        s.inrunColor = {1, .94f, .84f, strength};
        for (size_t i = 0; i < lights.size(); ++i) {
            auto position = lights[i].positionRange, direction = lights[i].directionCone;
            position[3] = 1;
            direction[3] = 0;
            s.inrunPositionRange[i] = transform(c.modelView, position);
            s.inrunDirectionCone[i] = transform(c.modelView, direction);
            s.inrunPositionRange[i][3] = lights[i].positionRange[3];
            s.inrunDirectionCone[i][3] = lights[i].directionCone[3];
        }
    });
}
void signalLight(const SpotLight &light, const std::array<float, 4> &color, float attenuationDistance) {
    apply([=](Context &c) {
        auto &s = c.state;
        auto position = light.positionRange, direction = light.directionCone;
        position[3] = 1;
        direction[3] = 0;
        s.signalPositionRange = transform(c.modelView, position);
        s.signalDirectionCone = transform(c.modelView, direction);
        s.signalPositionRange[3] = light.positionRange[3];
        s.signalDirectionCone[3] = light.directionCone[3];
        s.signalColor = color;
        s.signalParameters = {light.positionRange[0], 1 / (attenuationDistance * attenuationDistance), 0, 0};
    });
}
void lightfv(Enum light, Enum property, const float *v) {
    if (light != LIGHT0 && light != LIGHT1)
        throw std::runtime_error("Unsupported scene light");
    const auto value = array4(v);
    apply([=](Context &c) {
        auto &l = c.state.lights[light == LIGHT0 ? 0 : 1];
        if (property == POSITION) {
            if (value[3] != 0)
                throw std::runtime_error("Scene light must be directional");
            l.position = transform(c.modelView, value);
        } else if (property == DIFFUSE)
            l.diffuse = value;
        else if (property == AMBIENT)
            l.ambient = value;
        else if (property == SPECULAR)
            l.specular = value;
        else
            throw std::runtime_error("Unsupported light property");
    });
}
void materialfv(Enum, Enum property, const float *v) {
    const auto value = array4(v);
    apply([=](Context &c) {
        if (property == SPECULAR)
            c.state.specular = value;
        else if (property == EMISSION)
            c.state.emission = value;
        else
            throw std::runtime_error("Unsupported scene material");
    });
}
void materialf(Enum, Enum property, float value) {
    if (property != SHININESS)
        throw std::runtime_error("Unsupported material scalar");
    apply([=](Context &c) { c.state.shininess = value; });
}
void colorMaterial(Enum, Enum) {}
void fogfv(Enum property, const float *v) {
    if (property != FOG_COLOR)
        throw std::runtime_error("Unsupported fog color");
    const auto value = array4(v);
    apply([=](Context &c) { c.state.fogColor = value; });
}
void fogf(Enum property, float value) {
    apply([=](Context &c) {
        if (property == FOG_START)
            c.state.fogStart = value;
        else if (property == FOG_END)
            c.state.fogEnd = value;
        else
            throw std::runtime_error("Unsupported fog range");
    });
}
void fogi(Enum property, int mode) {
    if (property != FOG_MODE || mode != int(LINEAR))
        throw std::runtime_error("Unsupported scene fog mode");
}
void blendFunc(Enum source, Enum destination) {
    if (source != SRC_ALPHA || destination != ONE_MINUS_SRC_ALPHA)
        throw std::runtime_error("Unsupported scene blend mode");
}
void depthMask(bool value) {
    apply([=](Context &c) { c.state.pipeline.depthWrite = value; });
}
void polygonOffset(float factor, float units) {
    apply([=](Context &c) {
        c.state.pipeline.offsetFactor = factor;
        c.state.pipeline.offsetUnits = units;
    });
}
void lineWidth(float value) {
    apply([=](Context &c) { c.state.lineWidth = value; });
}
void viewport(int, int, int, int) {}
void clearColor(float r, float g, float b, float a) {
    ctx().background = {r, g, b, a};
}
void clear(Flags) {}
void hint(Enum, Enum) {}
void shadeModel(Enum mode) {
    if (mode != SMOOTH)
        throw std::runtime_error("Unsupported scene shading mode");
}
void pixelStorei(Enum property, int value) {
    if (value != 1 && value != 2 && value != 4 && value != 8)
        throw std::runtime_error("Invalid pixel alignment");
    if (property == UNPACK_ALIGNMENT)
        ctx().unpackAlignment = value;
    else if (property == PACK_ALIGNMENT)
        ctx().packAlignment = value;
    else
        throw std::runtime_error("Unsupported pixel storage property");
}
void getFloatv(Enum property, float *v) {
    const auto &c = ctx();
    if (property == CURRENT_COLOR)
        std::copy(c.current.color.begin(), c.current.color.end(), v);
    else if (property == CURRENT_NORMAL)
        std::copy(c.current.normal.begin(), c.current.normal.end(), v);
    else if (property == CURRENT_TEXTURE_COORDS) {
        v[0] = c.current.uv[0];
        v[1] = c.current.uv[1];
        v[2] = 0;
        v[3] = 1;
    } else
        throw std::runtime_error("Unsupported scene attribute query");
}
void getIntegerv(Enum property, int *v) {
    if (property == UNPACK_ALIGNMENT)
        *v = ctx().unpackAlignment;
    else if (property == SAMPLES)
        *v = ctx().backend.samples();
    else
        throw std::runtime_error("Unsupported scene integer query");
}
const unsigned char *getString(Enum property) {
    if (property != RENDERER)
        throw std::runtime_error("Unsupported renderer query");
    return reinterpret_cast<const unsigned char *>(ctx().backend.name().c_str());
}
void genTextures(int count, Id *ids) {
    auto &c = ctx();
    for (int i = 0; i < count; ++i) {
        ids[i] = c.nextImage++;
        c.images.emplace(ids[i], std::make_shared<Image>());
    }
}
void deleteTextures(int count, const Id *ids) {
    for (int i = 0; i < count; ++i)
        ctx().images.erase(ids[i]);
}
void bindTexture(Enum, Id id) {
    if (id && !ctx().images.count(id))
        throw std::runtime_error("Unknown scene texture");
    apply([=](Context &c) { c.state.textureId = id; });
}
void texImage2D(Enum, int level, int, int width, int height, int, Enum format, Enum type, const void *data) {
    auto &c = ctx();
    if (type != UNSIGNED_BYTE || level < 0 || level > 16 || width < 1 || height < 1 || width > 16384 ||
        height > 16384)
        throw std::runtime_error("Invalid scene texture dimensions/type");
    auto image = c.images.at(c.state.textureId);
    if (image->texture)
        throw std::runtime_error("Replacing an uploaded scene texture is unsupported");
    if (image->levels.size() <= size_t(level))
        image->levels.resize(size_t(level) + 1);
    auto &mip = image->levels[size_t(level)];
    mip.width = width;
    mip.height = height;
    mip.pixels.resize(size_t(width) * height * 4);
    const int channels = format == RGBA ? 4 : (format == RGB ? 3 : (format == ALPHA || format == LUMINANCE ? 1 : 0));
    if (!channels)
        throw std::runtime_error("Unsupported scene texture format");
    const size_t stride =
        (size_t(width) * channels + c.unpackAlignment - 1) / c.unpackAlignment * c.unpackAlignment;
    const auto *pixels = static_cast<const unsigned char *>(data);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            auto *out = mip.pixels.data() + (size_t(y) * width + x) * 4;
            const auto *in = pixels ? pixels + size_t(y) * stride + x * channels : nullptr;
            out[0] = format == ALPHA ? 255 : in ? in[0] : 0;
            out[1] = (format == RGB || format == RGBA) && in ? in[1] : out[0];
            out[2] = (format == RGB || format == RGBA) && in ? in[2] : out[0];
            out[3] = format == RGBA && in ? in[3] : (format == ALPHA && in ? in[0] : 255);
        }
}
void texParameteri(Enum, Enum property, int value) {
    auto image = ctx().images.at(ctx().state.textureId);
    if (image->sampler)
        throw std::runtime_error("Changing an uploaded scene sampler is unsupported");
    if (property == TEXTURE_WRAP_S)
        image->repeatU = value == int(REPEAT);
    else if (property == TEXTURE_WRAP_T)
        image->repeatV = value == int(REPEAT);
    else if (property == TEXTURE_MIN_FILTER)
        image->mipmapped = value == int(LINEAR_MIPMAP_LINEAR);
    else if (property != TEXTURE_MAG_FILTER)
        throw std::runtime_error("Unsupported scene sampler property");
}
void texEnvi(Enum, Enum, int value) {
    if (value != int(MODULATE))
        throw std::runtime_error("Unsupported scene texture combination");
}
Id genLists(int count) {
    auto &c = ctx();
    const Id first = c.nextMesh;
    for (int i = 0; i < count; ++i)
        c.meshes.emplace(c.nextMesh++, std::make_shared<CachedMesh>());
    return first;
}
void deleteLists(Id first, int count) {
    for (int i = 0; i < count; ++i)
        ctx().meshes.erase(first + i);
}
void newList(Id id, Enum mode) {
    auto &c = ctx();
    if (c.compiling || c.inPrimitive || mode != COMPILE_AND_EXECUTE)
        throw std::runtime_error("Invalid cached mesh begin");
    c.compiling = c.meshes.at(id);
}
void endList() {
    auto &c = ctx();
    if (!c.compiling || c.inPrimitive)
        throw std::runtime_error("Invalid cached mesh end");
    indexMesh(*c.compiling->mesh);
    c.compiling.reset();
}
void callList(Id id) {
    auto &c = ctx();
    if (c.compiling || c.inPrimitive)
        throw std::runtime_error("Invalid cached mesh replay");
    for (const auto &command : c.meshes.at(id)->commands)
        command(c);
}
} // namespace sjp::gfx
