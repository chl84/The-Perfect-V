#pragma once
#include <SDL3/SDL_gpu.h>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sjp::gfx {
struct Vertex {
    std::array<float, 3> position{};
    std::array<float, 3> normal{0, 0, 1};
    std::array<float, 4> color{1, 1, 1, 1};
    std::array<float, 2> uv{};
    std::array<float, 3> lineOther{};
    float lineSide = 0;
};
static_assert(sizeof(Vertex) == 64);
using Matrix = std::array<float, 16>;
struct alignas(16) Uniforms {
    Matrix modelView{}, projection{}, normalMatrix{};
    std::array<float, 4> ambient{}, direction0{}, direction1{}, diffuse0{}, diffuse1{}, specularLight0{},
        materialSpecular{}, emission{}, fogColor{}, parameters{}, flags{}, viewport{};
    std::array<std::array<float, 4>, 10> spotPositionRange{}, spotDirectionCone{};
    std::array<float, 4> arenaAxis{}, arenaParameters{}, arenaColor{}, worldZAxis{};
    std::array<std::array<float, 4>, 10> inrunPositionRange{}, inrunDirectionCone{};
    std::array<float, 4> inrunParameters{}, inrunColor{};
    std::array<float, 4> signalPositionRange{}, signalDirectionCone{}, signalColor{}, signalParameters{};
    bool operator==(const Uniforms &other) const;
};
static_assert(sizeof(Uniforms) == 1184);
struct PipelineState {
    bool depthTest = false, depthWrite = true, blend = false, cull = false, offset = false;
    float offsetFactor = 0, offsetUnits = 0;
    bool operator==(const PipelineState &other) const;
    bool operator<(const PipelineState &other) const;
};
struct Bounds {
    std::array<float, 3> low{}, high{};
    bool screenExpanded = false;
};
struct MeshRange {
    uint32_t first = 0, count = 0;
    Bounds bounds;
};
struct Frustum {
    std::array<std::array<double, 4>, 6> planes{};
    Frustum(const Matrix &projection, const Matrix &modelView);
    bool intersects(const Bounds &) const;
};
struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<MeshRange> ranges;
    uint32_t vertexCount = 0, indexCount = 0;
    bool indexed = false;
    SDL_GPUDevice *device = nullptr;
    SDL_GPUBuffer *buffer = nullptr;
    SDL_GPUBuffer *indexBuffer = nullptr;
    ~Mesh();
};
// Preserve triangle order and exact attributes; do not round or weld positions.
void appendMeshRanges(Mesh &, uint32_t first, uint32_t count);
void indexMesh(Mesh &);
struct MipLevel {
    int width = 0, height = 0;
    std::vector<unsigned char> pixels;
};
struct Image {
    std::vector<MipLevel> levels;
    bool repeatU = true, repeatV = true, mipmapped = false;
    bool opaque = true;
    SDL_GPUDevice *device = nullptr;
    SDL_GPUTexture *texture = nullptr;
    SDL_GPUSampler *sampler = nullptr;
    ~Image();
};
struct Packet {
    std::shared_ptr<Mesh> mesh; // null means the bounded per-frame vertex buffer
    std::shared_ptr<Image> image;
    uint32_t first = 0, count = 0;
    Uniforms uniforms;
    PipelineState pipeline;
};
class VulkanBackend {
  public:
    VulkanBackend(SDL_Window *, int msaaSamples, bool vsync, bool validation);
    ~VulkanBackend();
    VulkanBackend(const VulkanBackend &) = delete;
    VulkanBackend &operator=(const VulkanBackend &) = delete;
    void beginFrame();
    void wait();
    void configureGraphics(int msaaSamples, bool vsync);
    bool vsyncEnabled() const;
    void render(int width, int height, const std::array<float, 4> &clearColor, const std::vector<Packet> &,
                const std::vector<Vertex> &dynamic, bool present, bool synchronous,
                const std::string *capture);
    int samples() const;
    bool supportsSampleCount(int samples) const;
    const std::string &name() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace sjp::gfx
