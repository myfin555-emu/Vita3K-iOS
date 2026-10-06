#pragma once

// Apple\'s legacy MacTypes.h exposes a global `Ptr` typedef. Vita3K has a
// global Ptr<T> guest-memory template, so rename the legacy Apple alias while
// importing Metal/SDL headers. The alias is private to the Apple headers.
#define Ptr MacTypesPtr
#import <Metal/Metal.h>
#import <SDL3/SDL_metal.h>
#undef Ptr
@class CAMetalLayer;

#include <display/state.h>
#include <renderer/texture_cache.h>
#include <renderer/types.h>

#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct Config;
struct DisplayState;
struct GxmState;
struct MemState;

namespace renderer::metal {

struct MetalState;

struct MetalVertexProgram final : renderer::VertexProgram {
    std::vector<SceGxmVertexAttribute> attributes;
    std::string msl;
    std::string entry;
    id<MTLFunction> function = nil;
};

struct MetalFragmentProgram final : renderer::FragmentProgram {
    SceGxmBlendInfo blend{};
    bool has_blend = false;
    std::string msl;
    std::string entry;
    id<MTLFunction> function = nil;
};

struct MetalRenderTarget final : renderer::RenderTarget {
    id<MTLTexture> color = nil;
    id<MTLTexture> depth = nil;
    uint32_t width = 0;
    uint32_t height = 0;
};

struct MetalTextureCache final : renderer::TextureCache {
    MetalState &state;
    std::array<id<MTLTexture>, TextureCacheSize> textures{};
    std::array<id<MTLSamplerState>, TextureCacheSize> samplers{};

    explicit MetalTextureCache(MetalState &state);
    bool init(const std::string_view game_id);
    void select(size_t index, const SceGxmTexture &texture) override;
    void configure_texture(const SceGxmTexture &texture) override;
    void upload_texture_impl(SceGxmTextureBaseFormat base_format, uint32_t width,
        uint32_t height, uint32_t mip_index, const void *pixels, int face,
        uint32_t pixels_per_stride) override;
    void configure_sampler(size_t index, const SceGxmTexture &texture, bool no_linear) override;
    void import_configure_impl(SceGxmTextureBaseFormat base_format, uint32_t width,
        uint32_t height, bool is_srgb, uint16_t nb_components, uint16_t mipcount,
        bool swap_rb) override;

    id<MTLTexture> texture_at(size_t index) const { return textures[index]; }
    id<MTLSamplerState> sampler_at(size_t index) const { return samplers[index]; }
    size_t bound_index() const { return current_info ? static_cast<size_t>(current_info->index) : 0; }
};

struct MetalContext final : renderer::Context {
    MetalState &state;
    MemState &mem;

    std::array<id<MTLBuffer>, SCE_GXM_MAX_TEXTURE_UNITS> vertex_uniforms{};
    std::array<id<MTLBuffer>, SCE_GXM_MAX_TEXTURE_UNITS> fragment_uniforms{};
    std::array<id<MTLBuffer>, SCE_GXM_MAX_VERTEX_STREAMS> vertex_streams{};
    std::array<NSUInteger, SCE_GXM_MAX_VERTEX_STREAMS> vertex_stream_sizes{};
    std::array<id<MTLTexture>, SCE_GXM_MAX_TEXTURE_UNITS> vertex_textures{};
    std::array<id<MTLSamplerState>, SCE_GXM_MAX_TEXTURE_UNITS> vertex_samplers{};
    std::array<id<MTLTexture>, SCE_GXM_MAX_TEXTURE_UNITS> fragment_textures{};
    std::array<id<MTLSamplerState>, SCE_GXM_MAX_TEXTURE_UNITS> fragment_samplers{};
    std::vector<uint8_t> vertex_ubo_blob;
    std::vector<uint8_t> fragment_ubo_blob;

    id<MTLBuffer> index_buffer = nil;
    NSUInteger index_offset = 0;

    std::map<std::string, id<MTLRenderPipelineState>> pipelines;
    std::map<std::string, id<MTLDepthStencilState>> depth_states;

    MetalRenderTarget *render_target = nullptr;
    id<MTLCommandBuffer> command_buffer = nil;
    bool first_render_pass = true;

    explicit MetalContext(MetalState &state, MemState &mem);
    ~MetalContext() override;

    void set_texture(uint32_t index, const SceGxmTexture &texture, bool vertex);
    void set_uniform(bool vertex, int block, uint32_t size, const Ptr<const void> data);
    void set_vertex_stream(size_t index, size_t size, Ptr<const uint8_t> data);
    void set_context(MetalRenderTarget *target);
    void draw(SceGxmPrimitiveType type, SceGxmIndexFormat index_type,
        Ptr<const void> indices, uint32_t count, uint32_t instance_count);
    void sync_surface(const SceGxmNotification &vertex, const SceGxmNotification &fragment);

private:
    id<MTLRenderPipelineState> pipeline_for_draw();
    id<MTLDepthStencilState> depth_state_for_draw();
};

struct MetalState final : renderer::State {
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> command_queue = nil;
    SDL_MetalView metal_view = nil;
    CAMetalLayer *layer = nil;

    MetalTextureCache texture_cache;
    MetalContext *active_context = nullptr;

    id<MTLRenderPipelineState> present_pipeline = nil;
    id<MTLSamplerState> present_sampler = nil;
    id<MTLTexture> present_texture = nil;

    explicit MetalState();
    ~MetalState() override;

    bool init() override;
    void cleanup() override;
    void late_init(const Config &cfg, const std::string_view game_id, MemState &mem) override;
    TextureCache *get_texture_cache() override { return &texture_cache; }
    void render_frame(DisplayState &display, const GxmState &gxm, MemState &mem) override;
    void swap_window() override;
    void request_screen_rebuild() override;
    bool set_current() override { return true; }
    void done_current() override {}
    std::vector<uint32_t> dump_frame(DisplayState &display, uint32_t &width, uint32_t &height) override;
    uint32_t get_features_mask() override;
    int get_supported_filters() override;
    void set_screen_filter(const std::string_view &filter) override;
    int get_max_anisotropic_filtering() override;
    void set_anisotropic_filtering(int value) override;
    int get_max_2d_texture_width() override;
    void set_async_compilation(bool enable) override;
    uint32_t get_gpu_version() override;
    std::string_view get_gpu_name() override;
    void precompile_shader(const ShadersHash &hash) override;
    void preclose_action() override;

    bool map_memory(MemState &mem, Ptr<void> address, uint32_t size) override;
    void unmap_memory(MemState &mem, Ptr<void> address) override;

    void present_frame(const DisplayFrameInfo &frame, MemState &mem);
};

bool create(MetalState &state, std::unique_ptr<Context> &context, MemState &mem);
bool create(MetalState &state, std::unique_ptr<RenderTarget> &target,
    const SceGxmRenderTargetParams &params);
void destroy(MetalState &state, std::unique_ptr<RenderTarget> &target);

bool create(std::unique_ptr<VertexProgram> &program, MetalState &state,
    const SceGxmProgram &gxm, const std::vector<SceGxmVertexAttribute> &attributes);
bool create(std::unique_ptr<FragmentProgram> &program, MetalState &state,
    const SceGxmProgram &gxm, const SceGxmBlendInfo *blend);

void sync_texture(MetalContext &context, MemState &mem, size_t index,
    SceGxmTexture texture, const Config &config);
void sync_viewport_real(MetalContext &context, float xOffset, float yOffset,
    float zOffset, float xScale, float yScale, float zScale);

void sync_viewport_flat(MetalContext &context);

} // namespace renderer::metal
