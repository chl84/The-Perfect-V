#include "draw_internal.h"
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <stdexcept>
#include <tuple>

namespace sjp::gfx {
bool Uniforms::operator==(const Uniforms &o) const {
    return modelView == o.modelView && projection == o.projection && normalMatrix == o.normalMatrix &&
           ambient == o.ambient && direction0 == o.direction0 && direction1 == o.direction1 &&
           diffuse0 == o.diffuse0 && diffuse1 == o.diffuse1 && specularLight0 == o.specularLight0 &&
           materialSpecular == o.materialSpecular && emission == o.emission && fogColor == o.fogColor &&
           parameters == o.parameters && flags == o.flags && viewport == o.viewport &&
           spotPositionRange == o.spotPositionRange && spotDirectionCone == o.spotDirectionCone &&
           arenaAxis == o.arenaAxis && arenaParameters == o.arenaParameters && arenaColor == o.arenaColor &&
           worldZAxis == o.worldZAxis && inrunPositionRange == o.inrunPositionRange &&
           inrunDirectionCone == o.inrunDirectionCone && inrunParameters == o.inrunParameters &&
           inrunColor == o.inrunColor && signalPositionRange == o.signalPositionRange &&
           signalDirectionCone == o.signalDirectionCone && signalColor == o.signalColor &&
           signalParameters == o.signalParameters && backdropHaze == o.backdropHaze &&
           backdropColor == o.backdropColor && valleyEdge == o.valleyEdge;
}
bool PipelineState::operator==(const PipelineState &o) const { return !(*this < o) && !(o < *this); }
bool PipelineState::operator<(const PipelineState &o) const {
    return std::tie(depthTest, depthWrite, blend, cull, offset, offsetFactor, offsetUnits) <
           std::tie(o.depthTest, o.depthWrite, o.blend, o.cull, o.offset, o.offsetFactor, o.offsetUnits);
}
Mesh::~Mesh() {
    if (buffer)
        SDL_ReleaseGPUBuffer(device, buffer);
    if (indexBuffer)
        SDL_ReleaseGPUBuffer(device, indexBuffer);
}
Image::~Image() {
    if (sampler)
        SDL_ReleaseGPUSampler(device, sampler);
    if (texture)
        SDL_ReleaseGPUTexture(device, texture);
}
namespace {
constexpr uint32_t vertexCode[] =
#include "scene.vert.inc"
    ;
constexpr uint32_t fragmentCode[] =
#include "scene.frag.inc"
    ;
[[noreturn]] void fail(const char *operation) {
    throw std::runtime_error(std::string("Vulkan ") + operation + ": " + SDL_GetError());
}
uint32_t bytes(size_t count) {
    if (!count || count > std::numeric_limits<uint32_t>::max())
        throw std::runtime_error("Invalid Vulkan resource size");
    return uint32_t(count);
}
struct Transfer {
    SDL_GPUDevice *device;
    SDL_GPUTransferBuffer *buffer;
    Transfer(SDL_GPUDevice *d, uint32_t size, SDL_GPUTransferBufferUsage usage) : device(d) {
        SDL_GPUTransferBufferCreateInfo info{};
        info.usage = usage;
        info.size = size;
        buffer = SDL_CreateGPUTransferBuffer(device, &info);
        if (!buffer)
            fail("transfer allocation");
    }
    ~Transfer() { SDL_ReleaseGPUTransferBuffer(device, buffer); }
    Transfer(const Transfer &) = delete;
    Transfer &operator=(const Transfer &) = delete;
};
struct CommandBuffer {
    SDL_GPUCommandBuffer *buffer;
    bool acquiredSwapchain = false;
    explicit CommandBuffer(SDL_GPUDevice *device) : buffer(SDL_AcquireGPUCommandBuffer(device)) {
        if (!buffer)
            fail("command buffer acquisition");
    }
    ~CommandBuffer() {
        if (!buffer)
            return;
        // SDL forbids cancelling after acquiring a swapchain texture.
        if (acquiredSwapchain)
            SDL_SubmitGPUCommandBuffer(buffer);
        else
            SDL_CancelGPUCommandBuffer(buffer);
    }
    SDL_GPUFence *submit() {
        auto *submitted = buffer;
        buffer = nullptr;
        auto *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(submitted);
        if (!fence)
            fail("submission");
        return fence;
    }
};
} // namespace

struct VulkanBackend::Impl {
    SDL_Window *window = nullptr;
    SDL_GPUDevice *device = nullptr;
    bool claimed = false, rendered = false;
    SDL_GPUShader *vertexShader = nullptr, *fragmentShader = nullptr;
    struct FrameResources {
        SDL_GPUFence *fence = nullptr;
        SDL_GPUTexture *color = nullptr, *multisample = nullptr, *depth = nullptr;
        SDL_GPUBuffer *dynamicBuffer = nullptr;
        SDL_GPUTransferBuffer *dynamicTransfer = nullptr;
        uint32_t dynamicCapacity = 0;
        int width = 0, height = 0;
        // Retain cache entries evicted while a prior frame is still executing.
        std::vector<Packet> packets;
    };
    std::array<FrameResources, 2> frames;
    size_t frameIndex = frames.size() - 1;
    FrameResources &frame() { return frames[frameIndex]; }
    SDL_GPUSampleCount samples = SDL_GPU_SAMPLECOUNT_1;
    SDL_GPUPresentMode presentMode = SDL_GPU_PRESENTMODE_VSYNC;
    SDL_GPUTextureFormat depthFormat = SDL_GPU_TEXTUREFORMAT_D24_UNORM;
    static constexpr auto colorFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    std::map<PipelineState, SDL_GPUGraphicsPipeline *> pipelines;
    std::shared_ptr<Image> white;
    std::string description;

    ~Impl() {
        if (!device)
            return;
        for (auto &f : frames)
            waitFrame(f, false);
        white.reset();
        for (const auto &[key, pipeline] : pipelines)
            SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
        for (auto &f : frames) {
            if (f.dynamicBuffer)
                SDL_ReleaseGPUBuffer(device, f.dynamicBuffer);
            if (f.dynamicTransfer)
                SDL_ReleaseGPUTransferBuffer(device, f.dynamicTransfer);
            releaseTargets(f);
        }
        if (vertexShader)
            SDL_ReleaseGPUShader(device, vertexShader);
        if (fragmentShader)
            SDL_ReleaseGPUShader(device, fragmentShader);
        if (claimed)
            SDL_ReleaseWindowFromGPUDevice(device, window);
        SDL_DestroyGPUDevice(device);
    }
    static SDL_GPUSampleCount sampleEnum(int count) {
        switch (count) {
        case 2: return SDL_GPU_SAMPLECOUNT_2;
        case 4: return SDL_GPU_SAMPLECOUNT_4;
        case 8: return SDL_GPU_SAMPLECOUNT_8;
        default: return SDL_GPU_SAMPLECOUNT_1;
        }
    }
    bool supportsSamples(int count) const {
        if (count == 0)
            return true;
        if (count != 2 && count != 4 && count != 8)
            return false;
        const auto value = sampleEnum(count);
        return SDL_GPUTextureSupportsSampleCount(device, colorFormat, value) &&
               SDL_GPUTextureSupportsSampleCount(device, depthFormat, value);
    }
    SDL_GPUSampleCount selectSamples(int requested) const {
        for (int count : {8, 4, 2})
            if (count <= requested && supportsSamples(count))
                return sampleEnum(count);
        return SDL_GPU_SAMPLECOUNT_1;
    }
    void setup(SDL_Window *w, int msaaSamples, bool vsync, bool validation) {
        window = w;
        // This requests the actual Vulkan backend, including on systems with
        // several SDL GPU drivers. It cannot silently select another API.
        device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, validation, "vulkan");
        if (!device)
            fail("device creation");
        const char *driver = SDL_GetGPUDeviceDriver(device);
        if (!driver || std::strcmp(driver, "vulkan") != 0)
            throw std::runtime_error("SDL did not select the requested Vulkan backend");
        if (!SDL_ClaimWindowForGPUDevice(device, window))
            fail("window claim");
        claimed = true;
        if (!SDL_SetGPUAllowedFramesInFlight(device, 2))
            fail("frame queue configuration");
        const auto mode =
            !vsync && SDL_WindowSupportsGPUPresentMode(device, window, SDL_GPU_PRESENTMODE_IMMEDIATE)
                ? SDL_GPU_PRESENTMODE_IMMEDIATE
                : SDL_GPU_PRESENTMODE_VSYNC;
        if (!SDL_SetGPUSwapchainParameters(device, window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode))
            fail("swapchain configuration");
        presentMode = mode;
        if (!SDL_GPUTextureSupportsFormat(device, depthFormat, SDL_GPU_TEXTURETYPE_2D,
                                          SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET))
            depthFormat = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
        samples = selectSamples(msaaSamples);
        SDL_GPUShaderCreateInfo shader{};
        shader.format = SDL_GPU_SHADERFORMAT_SPIRV;
        shader.entrypoint = "main";
        shader.code = reinterpret_cast<const Uint8 *>(vertexCode);
        shader.code_size = sizeof(vertexCode);
        shader.stage = SDL_GPU_SHADERSTAGE_VERTEX;
        shader.num_uniform_buffers = 1;
        vertexShader = SDL_CreateGPUShader(device, &shader);
        if (!vertexShader)
            fail("vertex shader creation");
        shader.code = reinterpret_cast<const Uint8 *>(fragmentCode);
        shader.code_size = sizeof(fragmentCode);
        shader.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
        shader.num_uniform_buffers = 0;
        shader.num_samplers = 1;
        fragmentShader = SDL_CreateGPUShader(device, &shader);
        if (!fragmentShader)
            fail("fragment shader creation");
        const auto properties = SDL_GetGPUDeviceProperties(device);
        description = std::string("Vulkan | ") +
                      SDL_GetStringProperty(properties, SDL_PROP_GPU_DEVICE_NAME_STRING, "unknown GPU");
        white = std::make_shared<Image>();
        white->levels.push_back({1, 1, {255, 255, 255, 255}});
        white->repeatU = white->repeatV = false;
    }
    void waitFrame(FrameResources &f, bool reportFailure = true) {
        if (f.fence) {
            if (!SDL_WaitForGPUFences(device, true, &f.fence, 1) && reportFailure)
                fail("frame fence wait");
            SDL_ReleaseGPUFence(device, f.fence);
            f.fence = nullptr;
        }
        f.packets.clear();
    }
    void wait() {
        for (auto &f : frames)
            waitFrame(f);
    }
    void releaseTargets(FrameResources &f) {
        if (f.color)
            SDL_ReleaseGPUTexture(device, f.color);
        if (f.multisample)
            SDL_ReleaseGPUTexture(device, f.multisample);
        if (f.depth)
            SDL_ReleaseGPUTexture(device, f.depth);
        f.color = f.multisample = f.depth = nullptr;
    }
    SDL_GPUTexture *target(SDL_GPUTextureFormat format, SDL_GPUTextureUsageFlags usage,
                           SDL_GPUSampleCount count, int w, int h) {
        SDL_GPUTextureCreateInfo info{};
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = format;
        info.usage = usage;
        info.width = uint32_t(w);
        info.height = uint32_t(h);
        info.layer_count_or_depth = 1;
        info.num_levels = 1;
        info.sample_count = count;
        auto *texture = SDL_CreateGPUTexture(device, &info);
        if (!texture)
            fail("frame target allocation");
        return texture;
    }
    void resize(int w, int h) {
        if (w == frame().width && h == frame().height)
            return;
        if (w < 1 || h < 1 || w > 16384 || h > 16384)
            throw std::runtime_error("Invalid Vulkan frame size");
        releaseTargets(frame());
        frame().color = target(colorFormat, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
                               SDL_GPU_SAMPLECOUNT_1, w, h);
        if (samples != SDL_GPU_SAMPLECOUNT_1)
            frame().multisample = target(colorFormat, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET, samples, w, h);
        frame().depth = target(depthFormat, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET, samples, w, h);
        frame().width = w;
        frame().height = h;
    }
    void dynamicStorage(uint32_t required) {
        if (required <= frame().dynamicCapacity)
            return;
        if (required > 128 * 1024 * 1024)
            throw std::runtime_error("Dynamic scene exceeded its 128 MiB limit");
        uint32_t capacity = 65536;
        while (capacity < required)
            capacity *= 2;
        if (frame().dynamicBuffer)
            SDL_ReleaseGPUBuffer(device, frame().dynamicBuffer);
        if (frame().dynamicTransfer)
            SDL_ReleaseGPUTransferBuffer(device, frame().dynamicTransfer);
        frame().dynamicBuffer = nullptr;
        frame().dynamicTransfer = nullptr;
        SDL_GPUBufferCreateInfo buffer{};
        buffer.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
        buffer.size = capacity;
        frame().dynamicBuffer = SDL_CreateGPUBuffer(device, &buffer);
        if (!frame().dynamicBuffer)
            fail("dynamic vertex buffer allocation");
        SDL_GPUTransferBufferCreateInfo transfer{};
        transfer.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transfer.size = capacity;
        frame().dynamicTransfer = SDL_CreateGPUTransferBuffer(device, &transfer);
        if (!frame().dynamicTransfer)
            fail("dynamic transfer allocation");
        frame().dynamicCapacity = capacity;
    }
    SDL_GPUGraphicsPipeline *pipeline(const PipelineState &state) {
        const auto found = pipelines.find(state);
        if (found != pipelines.end())
            return found->second;
        SDL_GPUVertexBufferDescription buffer{};
        buffer.slot = 0;
        buffer.pitch = sizeof(Vertex);
        buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
        const std::array<SDL_GPUVertexAttribute, 6> attributes{
            {{0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(Vertex, position)},
             {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(Vertex, normal)},
             {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(Vertex, color)},
             {3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(Vertex, uv)},
             {4, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(Vertex, lineOther)},
             {5, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT, offsetof(Vertex, lineSide)}}};
        SDL_GPUColorTargetDescription colorTarget{};
        colorTarget.format = colorFormat;
        auto &blend = colorTarget.blend_state;
        blend.enable_blend = state.blend;
        // The game window is opaque. Fragment alpha still controls RGB
        // blending, while the cleared target alpha remains one for Wayland.
        blend.enable_color_write_mask = true;
        blend.color_write_mask = SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G | SDL_GPU_COLORCOMPONENT_B;
        blend.src_color_blendfactor = blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        blend.dst_color_blendfactor = blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        blend.color_blend_op = blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
        SDL_GPUGraphicsPipelineCreateInfo info{};
        info.vertex_shader = vertexShader;
        info.fragment_shader = fragmentShader;
        info.vertex_input_state.vertex_buffer_descriptions = &buffer;
        info.vertex_input_state.num_vertex_buffers = 1;
        info.vertex_input_state.vertex_attributes = attributes.data();
        info.vertex_input_state.num_vertex_attributes = uint32_t(attributes.size());
        info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        info.rasterizer_state.cull_mode = state.cull ? SDL_GPU_CULLMODE_BACK : SDL_GPU_CULLMODE_NONE;
        info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        info.rasterizer_state.enable_depth_clip = true;
        info.rasterizer_state.enable_depth_bias = state.offset;
        info.rasterizer_state.depth_bias_constant_factor = state.offsetUnits;
        info.rasterizer_state.depth_bias_slope_factor = state.offsetFactor;
        info.multisample_state.sample_count = samples;
        info.depth_stencil_state.enable_depth_test = state.depthTest;
        info.depth_stencil_state.enable_depth_write = state.depthTest && state.depthWrite;
        info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
        info.target_info.color_target_descriptions = &colorTarget;
        info.target_info.num_color_targets = 1;
        info.target_info.has_depth_stencil_target = true;
        info.target_info.depth_stencil_format = depthFormat;
        auto *created = SDL_CreateGPUGraphicsPipeline(device, &info);
        if (!created)
            fail("graphics pipeline creation");
        pipelines.emplace(state, created);
        return created;
    }
    void uploadImage(SDL_GPUCopyPass *copy, const std::shared_ptr<Image> &image,
                     std::vector<std::unique_ptr<Transfer>> &transfers) {
        if (image->texture)
            return;
        if (image->levels.empty() || image->levels[0].pixels.empty())
            throw std::runtime_error("Scene texture has no image data");
        const auto &base = image->levels[0];
        size_t total = 0;
        for (size_t i = 0; i < image->levels.size(); ++i) {
            const auto &level = image->levels[i];
            if (level.width != std::max(1, base.width >> i) ||
                level.height != std::max(1, base.height >> i) ||
                level.pixels.size() != size_t(level.width) * level.height * 4)
                throw std::runtime_error("Incomplete scene texture mip chain");
            total += level.pixels.size();
        }
        SDL_GPUTextureCreateInfo info{};
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = colorFormat;
        info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        info.width = uint32_t(base.width);
        info.height = uint32_t(base.height);
        info.layer_count_or_depth = 1;
        info.num_levels = uint32_t(image->levels.size());
        info.sample_count = SDL_GPU_SAMPLECOUNT_1;
        image->device = device;
        image->texture = SDL_CreateGPUTexture(device, &info);
        if (!image->texture)
            fail("scene texture allocation");
        SDL_GPUSamplerCreateInfo sampler{};
        sampler.min_filter = sampler.mag_filter = SDL_GPU_FILTER_LINEAR;
        sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
        sampler.address_mode_u =
            image->repeatU ? SDL_GPU_SAMPLERADDRESSMODE_REPEAT : SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        sampler.address_mode_v =
            image->repeatV ? SDL_GPU_SAMPLERADDRESSMODE_REPEAT : SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        sampler.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        sampler.max_lod = image->mipmapped ? float(image->levels.size() - 1) : 0;
        image->sampler = SDL_CreateGPUSampler(device, &sampler);
        if (!image->sampler)
            fail("scene sampler creation");
        auto transfer = std::make_unique<Transfer>(device, bytes(total), SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD);
        auto *mapped =
            static_cast<unsigned char *>(SDL_MapGPUTransferBuffer(device, transfer->buffer, false));
        if (!mapped)
            fail("texture transfer mapping");
        size_t offset = 0;
        for (const auto &level : image->levels) {
            std::memcpy(mapped + offset, level.pixels.data(), level.pixels.size());
            offset += level.pixels.size();
        }
        SDL_UnmapGPUTransferBuffer(device, transfer->buffer);
        offset = 0;
        for (size_t i = 0; i < image->levels.size(); ++i) {
            const auto &level = image->levels[i];
            SDL_GPUTextureTransferInfo source{};
            source.transfer_buffer = transfer->buffer;
            source.offset = uint32_t(offset);
            SDL_GPUTextureRegion destination{};
            destination.texture = image->texture;
            destination.mip_level = uint32_t(i);
            destination.w = uint32_t(level.width);
            destination.h = uint32_t(level.height);
            destination.d = 1;
            SDL_UploadToGPUTexture(copy, &source, &destination, false);
            offset += level.pixels.size();
        }
        // The transfer and the command buffer retain the GPU upload data.
        for (auto &level : image->levels)
            std::vector<unsigned char>().swap(level.pixels);
        transfers.push_back(std::move(transfer));
    }
    void uploadMesh(SDL_GPUCopyPass *copy, const std::shared_ptr<Mesh> &mesh,
                    std::vector<std::unique_ptr<Transfer>> &transfers) {
        if (!mesh || mesh->buffer)
            return;
        const uint32_t vertexSize = bytes(mesh->vertices.size() * sizeof(Vertex));
        const uint32_t indexSize = bytes(mesh->indices.size() * sizeof(uint32_t));
        const uint32_t size = bytes(size_t(vertexSize) + indexSize);
        SDL_GPUBufferCreateInfo info{};
        info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
        info.size = vertexSize;
        mesh->device = device;
        mesh->buffer = SDL_CreateGPUBuffer(device, &info);
        if (!mesh->buffer)
            fail("cached vertex buffer allocation");
        info.usage = SDL_GPU_BUFFERUSAGE_INDEX;
        info.size = indexSize;
        mesh->indexBuffer = SDL_CreateGPUBuffer(device, &info);
        if (!mesh->indexBuffer)
            fail("cached index buffer allocation");
        auto transfer = std::make_unique<Transfer>(device, size, SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD);
        auto *mapped = SDL_MapGPUTransferBuffer(device, transfer->buffer, false);
        if (!mapped)
            fail("cached vertex transfer mapping");
        std::memcpy(mapped, mesh->vertices.data(), vertexSize);
        std::memcpy(static_cast<unsigned char *>(mapped) + vertexSize, mesh->indices.data(), indexSize);
        SDL_UnmapGPUTransferBuffer(device, transfer->buffer);
        SDL_GPUTransferBufferLocation source{transfer->buffer, 0};
        SDL_GPUBufferRegion destination{mesh->buffer, 0, vertexSize};
        SDL_UploadToGPUBuffer(copy, &source, &destination, false);
        source.offset = vertexSize;
        destination = {mesh->indexBuffer, 0, indexSize};
        SDL_UploadToGPUBuffer(copy, &source, &destination, false);
        std::vector<Vertex>().swap(mesh->vertices);
        std::vector<uint32_t>().swap(mesh->indices);
        transfers.push_back(std::move(transfer));
    }
    void draw(SDL_GPUCommandBuffer *command, const std::array<float, 4> &background,
              const std::vector<Packet> &packets, const std::vector<Vertex> &dynamic) {
        // Allocate pipelines before opening a pass so failures can cancel the
        // unsubmitted command buffer without leaving an active render pass.
        for (const auto &packet : packets)
            pipeline(packet.pipeline);
        const uint32_t dynamicSize = dynamic.empty() ? 0 : bytes(dynamic.size() * sizeof(Vertex));
        if (dynamicSize)
            dynamicStorage(dynamicSize);
        std::vector<std::unique_ptr<Transfer>> transfers;
        auto *copy = SDL_BeginGPUCopyPass(command);
        if (!copy)
            fail("copy pass creation");
        try {
            uploadImage(copy, white, transfers);
            for (const auto &packet : packets) {
                if (packet.image)
                    uploadImage(copy, packet.image, transfers);
                uploadMesh(copy, packet.mesh, transfers);
            }
            if (dynamicSize) {
                auto *mapped = SDL_MapGPUTransferBuffer(device, frame().dynamicTransfer, false);
                if (!mapped)
                    fail("dynamic transfer mapping");
                std::memcpy(mapped, dynamic.data(), dynamicSize);
                SDL_UnmapGPUTransferBuffer(device, frame().dynamicTransfer);
                SDL_GPUTransferBufferLocation source{frame().dynamicTransfer, 0};
                SDL_GPUBufferRegion destination{frame().dynamicBuffer, 0, dynamicSize};
                SDL_UploadToGPUBuffer(copy, &source, &destination, false);
            }
        } catch (...) {
            SDL_EndGPUCopyPass(copy);
            throw;
        }
        SDL_EndGPUCopyPass(copy);
        SDL_GPUColorTargetInfo target{};
        target.texture = frame().multisample ? frame().multisample : frame().color;
        target.clear_color = {background[0], background[1], background[2], background[3]};
        target.load_op = SDL_GPU_LOADOP_CLEAR;
        target.store_op = frame().multisample ? SDL_GPU_STOREOP_RESOLVE : SDL_GPU_STOREOP_STORE;
        target.resolve_texture = frame().multisample ? frame().color : nullptr;
        SDL_GPUDepthStencilTargetInfo depthTarget{};
        depthTarget.texture = frame().depth;
        depthTarget.clear_depth = 1;
        depthTarget.load_op = SDL_GPU_LOADOP_CLEAR;
        depthTarget.store_op = SDL_GPU_STOREOP_DONT_CARE;
        depthTarget.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        depthTarget.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        auto *pass = SDL_BeginGPURenderPass(command, &target, 1, &depthTarget);
        if (!pass)
            fail("render pass creation");
        SDL_GPUViewport viewport{0, 0, float(frame().width), float(frame().height), 0, 1};
        SDL_SetGPUViewport(pass, &viewport);
        SDL_GPUGraphicsPipeline *lastPipeline = nullptr;
        SDL_GPUBuffer *lastBuffer = nullptr;
        SDL_GPUBuffer *lastIndexBuffer = nullptr;
        Image *lastImage = nullptr;
        for (const auto &packet : packets) {
            auto *selected = pipelines.at(packet.pipeline);
            if (selected != lastPipeline) {
                SDL_BindGPUGraphicsPipeline(pass, selected);
                lastPipeline = selected;
            }
            auto *buffer = packet.mesh ? packet.mesh->buffer : frame().dynamicBuffer;
            if (buffer != lastBuffer) {
                SDL_GPUBufferBinding binding{buffer, 0};
                SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);
                lastBuffer = buffer;
            }
            const auto &image = packet.image ? packet.image : white;
            if (image.get() != lastImage) {
                SDL_GPUTextureSamplerBinding binding{image->texture, image->sampler};
                SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
                lastImage = image.get();
            }
            SDL_PushGPUVertexUniformData(command, 0, &packet.uniforms, sizeof(Uniforms));
            if (packet.mesh) {
                if (packet.mesh->indexBuffer != lastIndexBuffer) {
                    SDL_GPUBufferBinding binding{packet.mesh->indexBuffer, 0};
                    SDL_BindGPUIndexBuffer(pass, &binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
                    lastIndexBuffer = packet.mesh->indexBuffer;
                }
                SDL_DrawGPUIndexedPrimitives(pass, packet.count, 1, packet.first, 0, 0);
            } else
                SDL_DrawGPUPrimitives(pass, packet.count, 1, packet.first, 0);
        }
        SDL_EndGPURenderPass(pass);
        rendered = true;
    }
    void screenshot(SDL_GPUCommandBuffer *command, Transfer &readback) {
        auto *copy = SDL_BeginGPUCopyPass(command);
        if (!copy)
            fail("screenshot copy pass creation");
        SDL_GPUTextureRegion source{};
        source.texture = frame().color;
        source.w = uint32_t(frame().width);
        source.h = uint32_t(frame().height);
        source.d = 1;
        SDL_GPUTextureTransferInfo destination{};
        destination.transfer_buffer = readback.buffer;
        SDL_DownloadFromGPUTexture(copy, &source, &destination);
        SDL_EndGPUCopyPass(copy);
    }
    void saveImage(Transfer &readback, const std::string &path) {
        auto *mapped = static_cast<unsigned char *>(SDL_MapGPUTransferBuffer(device, readback.buffer, false));
        if (!mapped)
            fail("screenshot transfer mapping");
        try {
            std::ofstream output(path, std::ios::binary);
            output.exceptions(std::ios::badbit | std::ios::failbit);
            output << "P6\n" << frame().width << ' ' << frame().height << "\n255\n";
            std::vector<unsigned char> row(size_t(frame().width) * 3);
            for (int y = 0; y < frame().height; ++y) {
                for (int x = 0; x < frame().width; ++x)
                    std::memcpy(row.data() + size_t(x) * 3, mapped + (size_t(y) * frame().width + x) * 4, 3);
                output.write(reinterpret_cast<const char *>(row.data()), std::streamsize(row.size()));
            }
        } catch (...) {
            SDL_UnmapGPUTransferBuffer(device, readback.buffer);
            throw;
        }
        SDL_UnmapGPUTransferBuffer(device, readback.buffer);
    }
};

VulkanBackend::VulkanBackend(SDL_Window *window, int msaaSamples, bool vsync, bool validation)
    : impl(std::make_unique<Impl>()) {
    impl->setup(window, msaaSamples, vsync, validation);
}
VulkanBackend::~VulkanBackend() = default;
void VulkanBackend::wait() { impl->wait(); }
void VulkanBackend::beginFrame() {
    impl->frameIndex = (impl->frameIndex + 1) % impl->frames.size();
    impl->rendered = false;
}
int VulkanBackend::samples() const {
    switch (impl->samples) {
    case SDL_GPU_SAMPLECOUNT_2: return 2;
    case SDL_GPU_SAMPLECOUNT_4: return 4;
    case SDL_GPU_SAMPLECOUNT_8: return 8;
    default: return 0;
    }
}
bool VulkanBackend::supportsSampleCount(int samples) const { return impl->supportsSamples(samples); }
bool VulkanBackend::vsyncEnabled() const { return impl->presentMode == SDL_GPU_PRESENTMODE_VSYNC; }
void VulkanBackend::configureGraphics(int msaaSamples, bool vsync) {
    auto &b = *impl;
    const auto mode = !vsync && SDL_WindowSupportsGPUPresentMode(b.device, b.window, SDL_GPU_PRESENTMODE_IMMEDIATE)
                          ? SDL_GPU_PRESENTMODE_IMMEDIATE : SDL_GPU_PRESENTMODE_VSYNC;
    const auto samples = b.selectSamples(msaaSamples);
    if (mode == b.presentMode && samples == b.samples)
        return;
    // Changes happen between submissions. Both slots must finish before their
    // attachments or sample-count-specific pipelines can be replaced.
    b.wait();
    if (mode != b.presentMode) {
        if (!SDL_SetGPUSwapchainParameters(b.device, b.window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode))
            fail("swapchain configuration");
        b.presentMode = mode;
    }
    if (samples != b.samples) {
        for (const auto &entry : b.pipelines)
            SDL_ReleaseGPUGraphicsPipeline(b.device, entry.second);
        b.pipelines.clear();
        for (auto &frame : b.frames) {
            b.releaseTargets(frame);
            frame.width = frame.height = 0;
        }
        b.samples = samples;
        b.rendered = false;
    }
}
const std::string &VulkanBackend::name() const { return impl->description; }
void VulkanBackend::render(int width, int height, const std::array<float, 4> &background,
                           const std::vector<Packet> &packets, const std::vector<Vertex> &dynamic,
                           bool present, bool synchronous, const std::string *capture) {
    auto &b = *impl;
    auto &f = b.frame();
    // CPU scene construction has already overlapped the previous submission.
    // Only wait when reusing this slot's transfer buffers and attachments.
    b.waitFrame(f);
    CommandBuffer command(b.device);
    SDL_GPUTexture *swapchain = nullptr;
    uint32_t swapWidth = 0, swapHeight = 0;
    if (present) {
        if (!SDL_WaitAndAcquireGPUSwapchainTexture(command.buffer, b.window, &swapchain, &swapWidth,
                                                   &swapHeight))
            fail("swapchain acquisition");
        if (!swapchain)
            return; // No offscreen drawing/upload when the window is unavailable.
        command.acquiredSwapchain = true;
    }
    b.resize(width, height);
    std::unique_ptr<Transfer> readback;
    if (!b.rendered)
        b.draw(command.buffer, background, packets, dynamic);
    if (capture) {
        readback = std::make_unique<Transfer>(b.device, bytes(size_t(width) * height * 4),
                                              SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD);
        b.screenshot(command.buffer, *readback);
    }
    if (swapchain) {
        SDL_GPUBlitInfo blit{};
        blit.source.texture = f.color;
        blit.source.w = uint32_t(width);
        blit.source.h = uint32_t(height);
        blit.destination.texture = swapchain;
        blit.destination.w = swapWidth;
        blit.destination.h = swapHeight;
        blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
        blit.filter = SDL_GPU_FILTER_NEAREST;
        SDL_BlitGPUTexture(command.buffer, &blit);
    }
    f.packets = packets;
    f.fence = command.submit();
    if (synchronous || capture)
        b.wait();
    if (capture)
        b.saveImage(*readback, *capture);
}
} // namespace sjp::gfx
