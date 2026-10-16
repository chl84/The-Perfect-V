// Exercise the real recorder/backend against a deterministic GPU queue. Fences
// complete only when waited; writes to resources still in use fail the test.
#include "draw_commands.h"
#include "presentation_cache.h"
#include "renderer.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace audit {
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
struct Resource {
    int uses = 0;
    bool released = false;
    virtual ~Resource() = default;
};
int resources = 0, waits = 0, submits = 0, draws = 0, uploads = 0, blits = 0, acquisitions = 0;
bool available = true;
std::vector<bool> blending;
std::thread::id renderThread;
template <class T> T *make() {
    ++resources;
    return new T;
}
void destroy(Resource *r) {
    --resources;
    delete r;
}
void release(Resource *r) {
    r->released = true;
    if (!r->uses)
        destroy(r);
}
} // namespace audit
struct SDL_GPUDevice {};
struct SDL_GPUBuffer : audit::Resource {};
struct SDL_GPUTexture : audit::Resource {};
struct SDL_GPUSampler : audit::Resource {};
struct SDL_GPUShader : audit::Resource {};
struct SDL_GPUGraphicsPipeline : audit::Resource {
    bool blend = false;
};
struct SDL_GPUTransferBuffer : audit::Resource {
    std::vector<unsigned char> bytes;
};
struct SDL_GPUCommandBuffer {
    std::vector<audit::Resource *> uses;
    bool acquired = false;
};
struct SDL_GPUCopyPass {
    SDL_GPUCommandBuffer *command;
};
struct SDL_GPURenderPass {
    SDL_GPUCommandBuffer *command;
    SDL_GPUGraphicsPipeline *pipeline = nullptr;
};
struct SDL_GPUFence {
    std::vector<audit::Resource *> uses;
    bool complete = false;
};
bool SDLCALL SDL_GetWindowSizeInPixels(SDL_Window *, int *w, int *h) {
    *w = 1440;
    *h = 900;
    return true;
}
namespace audit {
void use(SDL_GPUCommandBuffer *c, Resource *r) {
    require(!r->released, "Command uses a released GPU resource");
    if (std::find(c->uses.begin(), c->uses.end(), r) == c->uses.end()) {
        c->uses.push_back(r);
        ++r->uses;
    }
}
void complete(std::vector<Resource *> &uses) {
    for (auto *r : uses) {
        --r->uses;
        if (!r->uses && r->released)
            destroy(r);
    }
    uses.clear();
}
void write(SDL_GPUCommandBuffer *c, Resource *r) {
    const bool own = std::find(c->uses.begin(), c->uses.end(), r) != c->uses.end();
    require(r->uses == (own ? 1 : 0), "GPU write races a prior frame");
    use(c, r);
}
} // namespace audit
SDL_GPUDevice *SDLCALL SDL_CreateGPUDevice(SDL_GPUShaderFormat, bool, const char *) {
    audit::renderThread = std::this_thread::get_id();
    return new SDL_GPUDevice;
}
void SDLCALL SDL_DestroyGPUDevice(SDL_GPUDevice *d) {
    delete d;
}
const char *SDLCALL SDL_GetGPUDeviceDriver(SDL_GPUDevice *) {
    return "vulkan";
}
SDL_PropertiesID SDLCALL SDL_GetGPUDeviceProperties(SDL_GPUDevice *) {
    return 0;
}
bool SDLCALL SDL_ClaimWindowForGPUDevice(SDL_GPUDevice *, SDL_Window *) {
    return true;
}
void SDLCALL SDL_ReleaseWindowFromGPUDevice(SDL_GPUDevice *, SDL_Window *) {}
bool SDLCALL SDL_SetGPUAllowedFramesInFlight(SDL_GPUDevice *, Uint32 n) {
    audit::require(n == 2, "Backend did not request two frames in flight");
    return true;
}
bool SDLCALL SDL_WindowSupportsGPUPresentMode(SDL_GPUDevice *, SDL_Window *, SDL_GPUPresentMode) {
    return true;
}
bool SDLCALL SDL_SetGPUSwapchainParameters(SDL_GPUDevice *, SDL_Window *, SDL_GPUSwapchainComposition,
                                           SDL_GPUPresentMode) {
    return true;
}
bool SDLCALL SDL_GPUTextureSupportsFormat(SDL_GPUDevice *, SDL_GPUTextureFormat, SDL_GPUTextureType,
                                          SDL_GPUTextureUsageFlags) {
    return true;
}
bool SDLCALL SDL_GPUTextureSupportsSampleCount(SDL_GPUDevice *, SDL_GPUTextureFormat, SDL_GPUSampleCount) {
    return true;
}
SDL_GPUShader *SDLCALL SDL_CreateGPUShader(SDL_GPUDevice *, const SDL_GPUShaderCreateInfo *) {
    return audit::make<SDL_GPUShader>();
}
SDL_GPUBuffer *SDLCALL SDL_CreateGPUBuffer(SDL_GPUDevice *, const SDL_GPUBufferCreateInfo *) {
    return audit::make<SDL_GPUBuffer>();
}
SDL_GPUTexture *SDLCALL SDL_CreateGPUTexture(SDL_GPUDevice *, const SDL_GPUTextureCreateInfo *) {
    return audit::make<SDL_GPUTexture>();
}
SDL_GPUSampler *SDLCALL SDL_CreateGPUSampler(SDL_GPUDevice *, const SDL_GPUSamplerCreateInfo *) {
    return audit::make<SDL_GPUSampler>();
}
SDL_GPUGraphicsPipeline *SDLCALL
SDL_CreateGPUGraphicsPipeline(SDL_GPUDevice *, const SDL_GPUGraphicsPipelineCreateInfo *info) {
    auto *p = audit::make<SDL_GPUGraphicsPipeline>();
    p->blend = info->target_info.color_target_descriptions[0].blend_state.enable_blend;
    return p;
}
SDL_GPUTransferBuffer *SDLCALL SDL_CreateGPUTransferBuffer(SDL_GPUDevice *,
                                                           const SDL_GPUTransferBufferCreateInfo *info) {
    auto *b = audit::make<SDL_GPUTransferBuffer>();
    b->bytes.resize(info->size);
    return b;
}
void *SDLCALL SDL_MapGPUTransferBuffer(SDL_GPUDevice *, SDL_GPUTransferBuffer *b, bool) {
    audit::require(!b->uses, "CPU overwrote transfer data still used by a prior frame");
    return b->bytes.data();
}
void SDLCALL SDL_UnmapGPUTransferBuffer(SDL_GPUDevice *, SDL_GPUTransferBuffer *) {}
#define RELEASE_GPU(Type, Name)                                                                              \
    void SDLCALL SDL_ReleaseGPU##Name(SDL_GPUDevice *, Type *p) {                                            \
        audit::release(p);                                                                                   \
    }
RELEASE_GPU(SDL_GPUBuffer, Buffer)
RELEASE_GPU(SDL_GPUTexture, Texture)
RELEASE_GPU(SDL_GPUSampler, Sampler)
RELEASE_GPU(SDL_GPUShader, Shader)
RELEASE_GPU(SDL_GPUGraphicsPipeline, GraphicsPipeline)
RELEASE_GPU(SDL_GPUTransferBuffer, TransferBuffer)
#undef RELEASE_GPU
SDL_GPUCommandBuffer *SDLCALL SDL_AcquireGPUCommandBuffer(SDL_GPUDevice *) {
    audit::require(std::this_thread::get_id() == audit::renderThread, "Preparation touched the GPU thread");
    return new SDL_GPUCommandBuffer;
}
bool SDLCALL SDL_CancelGPUCommandBuffer(SDL_GPUCommandBuffer *c) {
    audit::require(!c->acquired, "Cancelled an acquired swapchain");
    audit::complete(c->uses);
    delete c;
    return true;
}
SDL_GPUFence *SDLCALL SDL_SubmitGPUCommandBufferAndAcquireFence(SDL_GPUCommandBuffer *c) {
    auto *f = new SDL_GPUFence;
    f->uses = std::move(c->uses);
    delete c;
    ++audit::submits;
    return f;
}
bool SDLCALL SDL_SubmitGPUCommandBuffer(SDL_GPUCommandBuffer *c) {
    audit::complete(c->uses);
    delete c;
    return true;
}
bool SDLCALL SDL_WaitForGPUFences(SDL_GPUDevice *, bool, SDL_GPUFence *const *f, Uint32 count) {
    ++audit::waits;
    for (Uint32 i = 0; i < count; ++i) {
        audit::complete(f[i]->uses);
        f[i]->complete = true;
    }
    return true;
}
void SDLCALL SDL_ReleaseGPUFence(SDL_GPUDevice *, SDL_GPUFence *f) {
    audit::require(f->complete, "Released an incomplete frame fence");
    delete f;
}
bool SDLCALL SDL_WaitAndAcquireGPUSwapchainTexture(SDL_GPUCommandBuffer *c, SDL_Window *, SDL_GPUTexture **t,
                                                   Uint32 *w, Uint32 *h) {
    static SDL_GPUTexture swapchain;
    ++audit::acquisitions;
    *t = audit::available ? &swapchain : nullptr;
    *w = 1440;
    *h = 900;
    c->acquired = *t != nullptr;
    return true;
}
SDL_GPUCopyPass *SDLCALL SDL_BeginGPUCopyPass(SDL_GPUCommandBuffer *c) {
    return new SDL_GPUCopyPass{c};
}
void SDLCALL SDL_EndGPUCopyPass(SDL_GPUCopyPass *p) {
    delete p;
}
void SDLCALL SDL_UploadToGPUBuffer(SDL_GPUCopyPass *p, const SDL_GPUTransferBufferLocation *src,
                                   const SDL_GPUBufferRegion *dst, bool) {
    audit::use(p->command, src->transfer_buffer);
    audit::write(p->command, dst->buffer);
    ++audit::uploads;
}
void SDLCALL SDL_UploadToGPUTexture(SDL_GPUCopyPass *p, const SDL_GPUTextureTransferInfo *src,
                                    const SDL_GPUTextureRegion *dst, bool) {
    audit::use(p->command, src->transfer_buffer);
    audit::write(p->command, dst->texture);
    ++audit::uploads;
}
void SDLCALL SDL_DownloadFromGPUTexture(SDL_GPUCopyPass *p, const SDL_GPUTextureRegion *src,
                                        const SDL_GPUTextureTransferInfo *dst) {
    audit::use(p->command, src->texture);
    audit::write(p->command, dst->transfer_buffer);
}
SDL_GPURenderPass *SDLCALL SDL_BeginGPURenderPass(SDL_GPUCommandBuffer *c,
                                                  const SDL_GPUColorTargetInfo *target, Uint32,
                                                  const SDL_GPUDepthStencilTargetInfo *depth) {
    audit::write(c, target->texture);
    if (target->resolve_texture)
        audit::write(c, target->resolve_texture);
    audit::write(c, depth->texture);
    return new SDL_GPURenderPass{c};
}
void SDLCALL SDL_EndGPURenderPass(SDL_GPURenderPass *p) {
    delete p;
}
void SDLCALL SDL_SetGPUViewport(SDL_GPURenderPass *, const SDL_GPUViewport *) {}
void SDLCALL SDL_BindGPUGraphicsPipeline(SDL_GPURenderPass *p, SDL_GPUGraphicsPipeline *pipeline) {
    p->pipeline = pipeline;
    audit::use(p->command, pipeline);
}
void SDLCALL SDL_BindGPUVertexBuffers(SDL_GPURenderPass *p, Uint32, const SDL_GPUBufferBinding *b, Uint32 n) {
    for (Uint32 i = 0; i < n; ++i)
        audit::use(p->command, b[i].buffer);
}
void SDLCALL SDL_BindGPUIndexBuffer(SDL_GPURenderPass *p, const SDL_GPUBufferBinding *b,
                                    SDL_GPUIndexElementSize) {
    audit::use(p->command, b->buffer);
}
void SDLCALL SDL_BindGPUFragmentSamplers(SDL_GPURenderPass *p, Uint32, const SDL_GPUTextureSamplerBinding *b,
                                         Uint32 n) {
    for (Uint32 i = 0; i < n; ++i) {
        audit::use(p->command, b[i].texture);
        audit::use(p->command, b[i].sampler);
    }
}
void SDLCALL SDL_PushGPUVertexUniformData(SDL_GPUCommandBuffer *, Uint32, const void *, Uint32) {}
void SDLCALL SDL_DrawGPUPrimitives(SDL_GPURenderPass *p, Uint32, Uint32, Uint32, Uint32) {
    ++audit::draws;
    audit::blending.push_back(p->pipeline->blend);
}
void SDLCALL SDL_DrawGPUIndexedPrimitives(SDL_GPURenderPass *p, Uint32, Uint32, Uint32, Sint32, Uint32) {
    ++audit::draws;
    audit::blending.push_back(p->pipeline->blend);
}
void SDLCALL SDL_BlitGPUTexture(SDL_GPUCommandBuffer *c, const SDL_GPUBlitInfo *b) {
    audit::use(c, b->source.texture);
    ++audit::blits;
}

namespace {
void triangle(float alpha = 1) {
    using namespace sjp::gfx;
    color4f(1, 1, 1, alpha);
    begin(TRIANGLES);
    vertex3d(-.5, -.5, 0);
    vertex3d(.5, -.5, 0);
    vertex3d(0, .5, 0);
    end();
}
void cacheKeys() {
    using namespace sjp;
    Frame a, b;
    audit::require(visual::samePresentationFrame(a, b), "Identical frame key missed");
    b.elapsed = 1;
    audit::require(!visual::samePresentationFrame(a, b), "Animation clock omitted from cache key");
    b = a;
    b.windSpeed = 2;
    audit::require(!visual::samePresentationFrame(a, b), "Wind omitted from cache key");
    b = a;
    b.pose[3] = .1f;
    audit::require(!visual::samePresentationFrame(a, b), "Pose omitted from cache key");
    b = a;
    a.hasRenderSkeleton = b.hasRenderSkeleton = true;
    b.renderSkeleton[2].z = .2;
    audit::require(!visual::samePresentationFrame(a, b), "Interpolated skeleton omitted from cache key");
}
void rendererCaches() {
    using namespace sjp;
    Renderer r(nullptr, SJP_TEST_ASSET_DIR, true, false, false);
    Simulation s(0, 0);
    // Real asynchronous recording, rapid selection changes and installation.
    r.begin(hills()[0], s.frame(), 0, 0, false);
    r.finish();
    r.begin(hills()[1], s.frame(), 0, 0, false);
    r.finish();
    r.waitForScene();
    r.begin(hills()[1], s.frame(), 0, 0, false);
    r.finish();
    r.waitForScene();
    auto draw = [&](const Frame &f, double time, bool previews = false) {
        r.begin(hills()[1], f, time, 0, false);
        if (!r.sceneReady()) {
            r.waitForScene();
            r.begin(hills()[1], f, time, 0, false);
        }
        audit::require(r.sceneReady(), "Prepared hill never became available");
        audit::require(!r.sceneReadyFor(hills()[2]), "Readiness from another hill leaked into simulation");
        r.overlay();
        if (previews)
            for (int a = 0; a < 4; ++a)
                r.jumperPreview(JumperAppearance(a), 250 + a * 300, 500, 100);
        const auto stats = gfx::frameStats();
        r.finish();
        return stats;
    };
    auto f = s.frame();
    f.elapsed = 10;
    const auto moving = draw(f, 1);
    draw(f, 2);
    const auto paused = draw(f, 3);
    audit::require(paused.dynamicVertices < moving.dynamicVertices &&
                       paused.cachedIndices > moving.cachedIndices,
                   "Repeated shadow was not promoted to GPU cache");
    f.pose[0] += .1f;
    const auto changed = draw(f, 4);
    audit::require(changed.dynamicVertices > paused.dynamicVertices, "Pose change retained a stale shadow");
    draw(f, 5);
    draw(f, 6);
    const auto preview = draw(f, 7, true), repeat = draw(f, 8, true);
    audit::require(preview.cachedIndices == repeat.cachedIndices,
                   "Identical preview rebuilt its fixed geometry");
    audit::require(repeat.dynamicVertices < changed.dynamicVertices + 20000,
                   "Four previews still regenerate complete figures");
    r.setEnvironment({environment::TimeOfDay::Night, environment::Weather::Snowfall, 123}, true);
    const auto night = draw(f, 9);
    audit::require(night.dynamicVertices > paused.dynamicVertices, "Lighting change retained a stale shadow");
    // A cache miss must still submit the last complete venue, rather than
    // replacing it with a terrain-free sky while the worker runs.
    for (int venue : {31, 30, 29}) {
        Simulation next(venue, 0);
        r.resetCamera();
        r.begin(hills()[venue], next.frame(), 10, 0, true);
        if (!r.sceneReady()) {
            audit::require(gfx::frameStats().submittedIndices > 0,
                           "Loading a new hill discarded the previous complete scene");
            audit::require(!r.sceneReadyFor(hills()[venue]),
                           "Fallback scenery advanced the new hill's simulation");
        }
        r.overlay();
        r.finish();
    }
    r.waitForScene();
    Simulation next(29, 0);
    r.begin(hills()[29], next.frame(), 11, 0, true);
    if (!r.sceneReady()) {
        r.waitForScene();
        r.begin(hills()[29], next.frame(), 11, 0, true);
    }
    audit::require(r.sceneReadyFor(hills()[29]), "Rapid hill changes never reached the last selection");
    r.finish();
}
} // namespace
int main() {
    using namespace sjp::gfx;
    try {
        cacheKeys();
        for (bool noMsaa : {false, true}) {
            initialize(nullptr, noMsaa, false, false);
            enable(BLEND);
            const auto baseline = audit::waits;
            for (int i = 0; i < 3; ++i) {
                beginFrame(1440, 900);
                triangle();
                audit::require(audit::waits == baseline, "CPU scene construction waited for the GPU");
                present(false);
                audit::require(audit::waits == baseline + (i == 2 ? 1 : 0),
                               "Did not wait only on reused frame slot");
            }
            audit::require(!audit::blending.back(), "Opaque geometry retained blending");
            beginFrame(640, 480);
            triangle(.5f);
            present(false);
            audit::require(audit::blending.back(), "Transparent geometry lost blending");
            Id texture;
            genTextures(1, &texture);
            bindTexture(TEXTURE_2D, texture);
            const unsigned char pixel[4]{255, 255, 255, 127};
            texImage2D(TEXTURE_2D, 0, RGBA, 1, 1, 0, RGBA, UNSIGNED_BYTE, pixel);
            enable(TEXTURE_2D);
            beginFrame(640, 480);
            triangle();
            present(false);
            audit::require(audit::blending.back(), "Texture alpha incorrectly disabled blending");
            disable(TEXTURE_2D);
            auto prepared = prepareList([] {
                pushAttrib(ENABLE_BIT | CURRENT_BIT);
                triangle();
                popAttrib();
            });
            const Id mesh = installList(prepared.get());
            audit::require(listBytes(mesh) > 0, "Prepared geometry has no accounted memory");
            auto failed = prepareList([] { throw std::runtime_error("preparation failure"); });
            bool rejected = false;
            try {
                failed.get();
            } catch (const std::runtime_error &) {
                rejected = true;
            }
            audit::require(rejected, "Preparation failure was lost");
            beginFrame(800, 600);
            callList(mesh);
            present(false);
            deleteLists(mesh, 1); // Prior frame must retain its buffers until completion.
            audit::available = false;
            const auto draws = audit::draws, uploads = audit::uploads, submits = audit::submits;
            beginFrame(800, 600);
            triangle();
            present(false);
            audit::require(audit::draws == draws && audit::uploads == uploads && audit::submits == submits,
                           "Unavailable window still drew or uploaded offscreen");
            audit::available = true;
            beginFrame(64, 48);
            triangle();
            const auto file = std::filesystem::temp_directory_path() / "tpv-frame-test.ppm";
            captureImage(file.string());
            const auto drawn = audit::draws;
            present(false);
            audit::require(audit::draws == drawn, "Capture then present rendered the same frame twice");
            audit::require(std::filesystem::file_size(file) == size_t(64 * 48 * 3 + 13),
                           "Invalid captured frame");
            std::filesystem::remove(file);
            beginFrame(128, 96);
            triangle();
            present(true);
            deleteTextures(1, &texture);
            shutdown();
            audit::require(audit::resources == 0, "Frame/cache GPU resources leaked at shutdown");
        }
        rendererCaches();
        audit::require(audit::resources == 0, "Renderer cache resources leaked at shutdown");
        std::cout << "GPU frame reuse, resize, unavailable window, capture, cache handoff and alpha passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
