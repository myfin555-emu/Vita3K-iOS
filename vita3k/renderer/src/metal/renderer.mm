#import <SDL3/SDL_metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <renderer/metal/state.h>

#include <SPIRV-Cross/spirv_msl.hpp>

#include <config/state.h>
#include <display/state.h>
#include <gxm/functions.h>
#include <gxm/types.h>
#include <renderer/functions.h>
#include <renderer/texture_cache.h>
#include <shader/spirv_recompiler.h>
#include <shader/uniform_block.h>
#include <util/log.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <memory>
#include <sstream>

namespace renderer::metal {

namespace {

static MTLCompareFunction compare_func(SceGxmDepthFunc f) {
    switch (f) {
    case SCE_GXM_DEPTH_FUNC_NEVER: return MTLCompareFunctionNever;
    case SCE_GXM_DEPTH_FUNC_LESS: return MTLCompareFunctionLess;
    case SCE_GXM_DEPTH_FUNC_EQUAL: return MTLCompareFunctionEqual;
    case SCE_GXM_DEPTH_FUNC_LESS_EQUAL: return MTLCompareFunctionLessEqual;
    case SCE_GXM_DEPTH_FUNC_GREATER: return MTLCompareFunctionGreater;
    case SCE_GXM_DEPTH_FUNC_NOT_EQUAL: return MTLCompareFunctionNotEqual;
    case SCE_GXM_DEPTH_FUNC_GREATER_EQUAL: return MTLCompareFunctionGreaterEqual;
    case SCE_GXM_DEPTH_FUNC_ALWAYS: return MTLCompareFunctionAlways;
    }
    return MTLCompareFunctionAlways;
}

static MTLCompareFunction stencil_func(SceGxmStencilFunc f) {
    switch (f) {
    case SCE_GXM_STENCIL_FUNC_NEVER: return MTLCompareFunctionNever;
    case SCE_GXM_STENCIL_FUNC_LESS: return MTLCompareFunctionLess;
    case SCE_GXM_STENCIL_FUNC_EQUAL: return MTLCompareFunctionEqual;
    case SCE_GXM_STENCIL_FUNC_LESS_EQUAL: return MTLCompareFunctionLessEqual;
    case SCE_GXM_STENCIL_FUNC_GREATER: return MTLCompareFunctionGreater;
    case SCE_GXM_STENCIL_FUNC_NOT_EQUAL: return MTLCompareFunctionNotEqual;
    case SCE_GXM_STENCIL_FUNC_GREATER_EQUAL: return MTLCompareFunctionGreaterEqual;
    case SCE_GXM_STENCIL_FUNC_ALWAYS: return MTLCompareFunctionAlways;
    }
    return MTLCompareFunctionAlways;
}

static MTLStencilOperation stencil_op(SceGxmStencilOp op) {
    switch (op) {
    case SCE_GXM_STENCIL_OP_KEEP: return MTLStencilOperationKeep;
    case SCE_GXM_STENCIL_OP_ZERO: return MTLStencilOperationZero;
    case SCE_GXM_STENCIL_OP_REPLACE: return MTLStencilOperationReplace;
    case SCE_GXM_STENCIL_OP_INCR: return MTLStencilOperationIncrementClamp;
    case SCE_GXM_STENCIL_OP_DECR: return MTLStencilOperationDecrementClamp;
    case SCE_GXM_STENCIL_OP_INVERT: return MTLStencilOperationInvert;
    case SCE_GXM_STENCIL_OP_INCR_WRAP: return MTLStencilOperationIncrementWrap;
    case SCE_GXM_STENCIL_OP_DECR_WRAP: return MTLStencilOperationDecrementWrap;
    }
    return MTLStencilOperationKeep;
}

static MTLBlendFactor blend_factor(SceGxmBlendFactor f) {
    switch (f) {
    case SCE_GXM_BLEND_FACTOR_ZERO: return MTLBlendFactorZero;
    case SCE_GXM_BLEND_FACTOR_ONE: return MTLBlendFactorOne;
    case SCE_GXM_BLEND_FACTOR_SRC_COLOR: return MTLBlendFactorSourceColor;
    case SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_COLOR: return MTLBlendFactorOneMinusSourceColor;
    case SCE_GXM_BLEND_FACTOR_SRC_ALPHA: return MTLBlendFactorSourceAlpha;
    case SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA: return MTLBlendFactorOneMinusSourceAlpha;
    case SCE_GXM_BLEND_FACTOR_DST_COLOR: return MTLBlendFactorDestinationColor;
    case SCE_GXM_BLEND_FACTOR_ONE_MINUS_DST_COLOR: return MTLBlendFactorOneMinusDestinationColor;
    case SCE_GXM_BLEND_FACTOR_DST_ALPHA: return MTLBlendFactorDestinationAlpha;
    case SCE_GXM_BLEND_FACTOR_ONE_MINUS_DST_ALPHA: return MTLBlendFactorOneMinusDestinationAlpha;
    case SCE_GXM_BLEND_FACTOR_SRC_ALPHA_SATURATE: return MTLBlendFactorSourceAlphaSaturated;
    case SCE_GXM_BLEND_FACTOR_DST_ALPHA_SATURATE: return MTLBlendFactorDestinationAlpha;
    }
    return MTLBlendFactorOne;
}

static MTLBlendOperation blend_op(SceGxmBlendFunc f) {
    switch (f) {
    case SCE_GXM_BLEND_FUNC_SUBTRACT: return MTLBlendOperationSubtract;
    case SCE_GXM_BLEND_FUNC_REVERSE_SUBTRACT: return MTLBlendOperationReverseSubtract;
    case SCE_GXM_BLEND_FUNC_MIN: return MTLBlendOperationMin;
    case SCE_GXM_BLEND_FUNC_MAX: return MTLBlendOperationMax;
    case SCE_GXM_BLEND_FUNC_NONE:
    case SCE_GXM_BLEND_FUNC_ADD:
    default: return MTLBlendOperationAdd;
    }
}

static MTLTriangleFillMode fill_mode(SceGxmPolygonMode mode) {
    switch (mode) {
    case SCE_GXM_POLYGON_MODE_LINE:
    case SCE_GXM_POLYGON_MODE_TRIANGLE_LINE:
        return MTLTriangleFillModeLines;
    default:
        return MTLTriangleFillModeFill;
    }
}

static MTLCullMode cull_mode(SceGxmCullMode mode) {
    switch (mode) {
    case SCE_GXM_CULL_CW: return MTLCullModeFront;
    case SCE_GXM_CULL_CCW: return MTLCullModeBack;
    default: return MTLCullModeNone;
    }
}

static MTLPrimitiveType primitive_type(SceGxmPrimitiveType type) {
    switch (type) {
    case SCE_GXM_PRIMITIVE_LINES: return MTLPrimitiveTypeLine;
    case SCE_GXM_PRIMITIVE_POINTS: return MTLPrimitiveTypePoint;
    case SCE_GXM_PRIMITIVE_TRIANGLE_STRIP: return MTLPrimitiveTypeTriangleStrip;
    case SCE_GXM_PRIMITIVE_TRIANGLE_FAN: return MTLPrimitiveTypeTriangle;
    case SCE_GXM_PRIMITIVE_TRIANGLES:
    default: return MTLPrimitiveTypeTriangle;
    }
}

static MTLVertexFormat vertex_format(SceGxmAttributeFormat format, uint8_t count) {
    const bool normalized =
        format == SCE_GXM_ATTRIBUTE_FORMAT_U8N ||
        format == SCE_GXM_ATTRIBUTE_FORMAT_S8N ||
        format == SCE_GXM_ATTRIBUTE_FORMAT_U16N ||
        format == SCE_GXM_ATTRIBUTE_FORMAT_S16N;
    switch (format) {
    case SCE_GXM_ATTRIBUTE_FORMAT_F16:
        return count == 1 ? MTLVertexFormatHalf : count == 2 ? MTLVertexFormatHalf2 : count == 3 ? MTLVertexFormatHalf3 : MTLVertexFormatHalf4;
    case SCE_GXM_ATTRIBUTE_FORMAT_F32:
        return count == 1 ? MTLVertexFormatFloat : count == 2 ? MTLVertexFormatFloat2 : count == 3 ? MTLVertexFormatFloat3 : MTLVertexFormatFloat4;
    case SCE_GXM_ATTRIBUTE_FORMAT_U8:
    case SCE_GXM_ATTRIBUTE_FORMAT_U8N:
        return normalized ? (count == 1 ? MTLVertexFormatUCharNormalized : count == 2 ? MTLVertexFormatUChar2Normalized : count == 3 ? MTLVertexFormatUChar3Normalized : MTLVertexFormatUChar4Normalized)
                          : (count == 1 ? MTLVertexFormatUChar : count == 2 ? MTLVertexFormatUChar2 : count == 3 ? MTLVertexFormatUChar3 : MTLVertexFormatUChar4);
    case SCE_GXM_ATTRIBUTE_FORMAT_S8:
    case SCE_GXM_ATTRIBUTE_FORMAT_S8N:
        return normalized ? (count == 1 ? MTLVertexFormatCharNormalized : count == 2 ? MTLVertexFormatChar2Normalized : count == 3 ? MTLVertexFormatChar3Normalized : MTLVertexFormatChar4Normalized)
                          : (count == 1 ? MTLVertexFormatChar : count == 2 ? MTLVertexFormatChar2 : count == 3 ? MTLVertexFormatChar3 : MTLVertexFormatChar4);
    case SCE_GXM_ATTRIBUTE_FORMAT_U16:
    case SCE_GXM_ATTRIBUTE_FORMAT_U16N:
        return normalized ? (count == 1 ? MTLVertexFormatUShortNormalized : count == 2 ? MTLVertexFormatUShort2Normalized : count == 3 ? MTLVertexFormatUShort3Normalized : MTLVertexFormatUShort4Normalized)
                          : (count == 1 ? MTLVertexFormatUShort : count == 2 ? MTLVertexFormatUShort2 : count == 3 ? MTLVertexFormatUShort3 : MTLVertexFormatUShort4);
    case SCE_GXM_ATTRIBUTE_FORMAT_S16:
    case SCE_GXM_ATTRIBUTE_FORMAT_S16N:
        return normalized ? (count == 1 ? MTLVertexFormatShortNormalized : count == 2 ? MTLVertexFormatShort2Normalized : count == 3 ? MTLVertexFormatShort3Normalized : MTLVertexFormatShort4Normalized)
                          : (count == 1 ? MTLVertexFormatShort : count == 2 ? MTLVertexFormatShort2 : count == 3 ? MTLVertexFormatShort3 : MTLVertexFormatShort4);
    case SCE_GXM_ATTRIBUTE_FORMAT_UNTYPED:
        return count == 1 ? MTLVertexFormatInt : count == 2 ? MTLVertexFormatInt2 : count == 3 ? MTLVertexFormatInt3 : MTLVertexFormatInt4;
    }
    return MTLVertexFormatFloat4;
}

static size_t attribute_size(SceGxmAttributeFormat format, uint8_t count) {
    size_t scalar = 4;
    switch (format) {
    case SCE_GXM_ATTRIBUTE_FORMAT_U8:
    case SCE_GXM_ATTRIBUTE_FORMAT_U8N:
    case SCE_GXM_ATTRIBUTE_FORMAT_S8:
    case SCE_GXM_ATTRIBUTE_FORMAT_S8N: scalar = 1; break;
    case SCE_GXM_ATTRIBUTE_FORMAT_U16:
    case SCE_GXM_ATTRIBUTE_FORMAT_U16N:
    case SCE_GXM_ATTRIBUTE_FORMAT_S16:
    case SCE_GXM_ATTRIBUTE_FORMAT_S16N:
    case SCE_GXM_ATTRIBUTE_FORMAT_F16: scalar = 2; break;
    default: break;
    }
    return scalar * count;
}

static std::string pointer_key(const void *a, const void *b, const GxmRecordState &r) {
    std::ostringstream out;
    out << a << ':' << b << ':' << static_cast<uint32_t>(r.cull_mode)
        << ':' << static_cast<uint32_t>(r.front_depth_func)
        << ':' << static_cast<uint32_t>(r.front_depth_write_mode)
        << ':' << static_cast<uint32_t>(r.color_base_format)
        << ':' << r.front_stencil_state_values.ref;
    return out.str();
}

static void remap_msl_bindings(spirv_cross::CompilerMSL &compiler) {
    auto remap = [&](const auto &resources,
                     uint32_t texture_base, uint32_t sampler_base) {
        for (const auto &resource : resources) {
            const uint32_t set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
            const uint32_t binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
            uint32_t mapped = binding;
            if (set == 2 || set == 3) mapped = binding & 15u;
            else if (set == 1) mapped = 30u + (binding & 1u);
            compiler.set_decoration(resource.id, spv::DecorationBinding, mapped);
        }
    };

    auto resources = compiler.get_shader_resources();
    remap(resources.sampled_images, 0, 0);
    remap(resources.separate_images, 0, 0);
    remap(resources.separate_samplers, 0, 0);
    remap(resources.storage_images, 30, 0);
    remap(resources.uniform_buffers, 0, 0);
    remap(resources.storage_buffers, 0, 0);
}

static std::pair<std::string, std::string> compile_shader(const SceGxmProgram &program,
    const std::string &hash, const FeatureState &features, const shader::Hints &hints,
    bool maskupdate) {
    const auto generated = shader::convert_gxp(program, hash, features,
        shader::Target::SpirVOpenGL, hints, maskupdate);
    if (generated.spirv.empty())
        return {};

    spirv_cross::CompilerMSL compiler(generated.spirv);
    auto options = compiler.get_msl_options();
    options.platform = spirv_cross::CompilerMSL::Options::iOS;
    options.enable_decoration_binding = true;
    options.force_active_argument_buffer_resources = false;
    compiler.set_msl_options(options);
    remap_msl_bindings(compiler);

    auto entries = compiler.get_entry_points_and_stages();
    std::string entry;
    for (const auto &e : entries) {
        if ((program.is_vertex() && e.execution_model == spv::ExecutionModelVertex)
            || (program.is_fragment() && e.execution_model == spv::ExecutionModelFragment)) {
            entry = e.name;
            break;
        }
    }
    if (entry.empty() && !entries.empty())
        entry = entries.front().name;

    try {
        return { compiler.compile(), entry };
    } catch (const std::exception &e) {
        LOG_ERROR("GE:R Metal shader translation failed for {}: {}", hash, e.what());
        return {};
    }
}

static id<MTLBuffer> make_shared_buffer(id<MTLDevice> device, const void *data, size_t size) {
    if (!size) return nil;
    id<MTLBuffer> b = [device newBufferWithLength:size options:MTLResourceStorageModeShared];
    if (!b) return nil;
    if (data) memcpy(b.contents, data, size);
    return b;
}

} // namespace

MetalTextureCache::MetalTextureCache(MetalState &state)
    : state(state) {
    backend = Backend::Metal;
}

bool MetalTextureCache::init(const std::string_view game_id) {
    backend = Backend::Metal;
    return TextureCache::init(true, state.texture_folder(), game_id, TextureCacheSize);
}

void MetalTextureCache::select(size_t, const SceGxmTexture &) {
    // TextureCache::cache_and_bind_texture() has already selected current_info.
    // Metal resources are addressed by that cache index.
}

void MetalTextureCache::configure_texture(const SceGxmTexture &texture) {
    if (!current_info) return;
    const size_t index = static_cast<size_t>(current_info->index);
    const uint32_t width = std::max<uint32_t>(1, gxm::get_width(texture));
    const uint32_t height = std::max<uint32_t>(1, gxm::get_height(texture));
    if (textures[index] && (textures[index].width != width || textures[index].height != height))
        textures[index] = nil;

    if (textures[index]) return;

    auto *desc = [[MTLTextureDescriptor alloc] init];
    desc.textureType = (texture.texture_type() == SCE_GXM_TEXTURE_CUBE || texture.texture_type() == SCE_GXM_TEXTURE_CUBE_ARBITRARY)
        ? MTLTextureTypeCube : MTLTextureType2D;
    const auto base = gxm::get_base_format(gxm::get_format(texture));
    switch (base) {
    case SCE_GXM_TEXTURE_BASE_FORMAT_U8: desc.pixelFormat = MTLPixelFormatR8Unorm; break;
    case SCE_GXM_TEXTURE_BASE_FORMAT_S8: desc.pixelFormat = MTLPixelFormatR8Snorm; break;
    case SCE_GXM_TEXTURE_BASE_FORMAT_U4U4U4U4: desc.pixelFormat = MTLPixelFormatABGR4Unorm; break;
    case SCE_GXM_TEXTURE_BASE_FORMAT_U1U5U5U5: desc.pixelFormat = MTLPixelFormatA1BGR5Unorm; break;
    case SCE_GXM_TEXTURE_BASE_FORMAT_U5U6U5: desc.pixelFormat = MTLPixelFormatB5G6R5Unorm; break;
    case SCE_GXM_TEXTURE_BASE_FORMAT_U8U8: desc.pixelFormat = MTLPixelFormatRG8Unorm; break;
    case SCE_GXM_TEXTURE_BASE_FORMAT_S8S8: desc.pixelFormat = MTLPixelFormatRG8Snorm; break;
    case SCE_GXM_TEXTURE_BASE_FORMAT_U16: desc.pixelFormat = MTLPixelFormatR16Uint; break;
    case SCE_GXM_TEXTURE_BASE_FORMAT_S16: desc.pixelFormat = MTLPixelFormatR16Sint; break;
    case SCE_GXM_TEXTURE_BASE_FORMAT_F16: desc.pixelFormat = MTLPixelFormatR16Float; break;
    case SCE_GXM_TEXTURE_BASE_FORMAT_U8U8U8U8: desc.pixelFormat = MTLPixelFormatRGBA8Unorm; break;
    case SCE_GXM_TEXTURE_BASE_FORMAT_S8S8S8S8: desc.pixelFormat = MTLPixelFormatRGBA8Snorm; break;
    case SCE_GXM_TEXTURE_BASE_FORMAT_U16U16: desc.pixelFormat = MTLPixelFormatRG16Uint; break;
    case SCE_GXM_TEXTURE_BASE_FORMAT_S16S16: desc.pixelFormat = MTLPixelFormatRG16Sint; break;
    case SCE_GXM_TEXTURE_BASE_FORMAT_F16F16: desc.pixelFormat = MTLPixelFormatRG16Float; break;
    case SCE_GXM_TEXTURE_BASE_FORMAT_F32: desc.pixelFormat = MTLPixelFormatR32Float; break;
    case SCE_GXM_TEXTURE_BASE_FORMAT_F16F16F16F16: desc.pixelFormat = MTLPixelFormatRGBA16Float; break;
    case SCE_GXM_TEXTURE_BASE_FORMAT_U16U16U16U16: desc.pixelFormat = MTLPixelFormatRGBA16Uint; break;
    case SCE_GXM_TEXTURE_BASE_FORMAT_S16S16S16S16: desc.pixelFormat = MTLPixelFormatRGBA16Sint; break;
    case SCE_GXM_TEXTURE_BASE_FORMAT_F32F32: desc.pixelFormat = MTLPixelFormatRG32Float; break;
    case SCE_GXM_TEXTURE_BASE_FORMAT_U32U32: desc.pixelFormat = MTLPixelFormatRG32Uint; break;
    case SCE_GXM_TEXTURE_BASE_FORMAT_U8U8U8:
    case SCE_GXM_TEXTURE_BASE_FORMAT_S8S8S8:
    case SCE_GXM_TEXTURE_BASE_FORMAT_U8U3U3U2:
    case SCE_GXM_TEXTURE_BASE_FORMAT_P4:
    case SCE_GXM_TEXTURE_BASE_FORMAT_P8:
    case SCE_GXM_TEXTURE_BASE_FORMAT_PVRT2BPP:
    case SCE_GXM_TEXTURE_BASE_FORMAT_PVRT4BPP:
    case SCE_GXM_TEXTURE_BASE_FORMAT_PVRTII2BPP:
    case SCE_GXM_TEXTURE_BASE_FORMAT_PVRTII4BPP:
    default: desc.pixelFormat = MTLPixelFormatRGBA8Unorm; break;
    }
    desc.width = width;
    desc.height = height;
    desc.mipmapLevelCount = std::max<uint32_t>(1, current_info->mip_count);
    desc.arrayLength = 1;
    desc.usage = MTLTextureUsageShaderRead;
    // CPU upload path uses replaceRegion:withBytes:. Private textures require
    // a staging resource + blit, so keep this correctness-first path shared.
    desc.storageMode = MTLStorageModeShared;
    textures[index] = [state.device newTextureWithDescriptor:desc];
}

void MetalTextureCache::upload_texture_impl(SceGxmTextureBaseFormat base_format, uint32_t width,
    uint32_t height, uint32_t mip_index, const void *pixels, int face, uint32_t pixels_per_stride) {
    if (!current_info || !pixels || !textures[current_info->index]) return;
    auto tex = textures[current_info->index];
    const uint32_t stride = std::max(width, pixels_per_stride);
    size_t bpp = 4;
    switch (tex.pixelFormat) {
    case MTLPixelFormatR8Unorm:
    case MTLPixelFormatR8Snorm: bpp = 1; break;
    case MTLPixelFormatRG8Unorm:
    case MTLPixelFormatRG8Snorm:
    case MTLPixelFormatR16Uint:
    case MTLPixelFormatR16Sint:
    case MTLPixelFormatR16Float:
    case MTLPixelFormatB5G6R5Unorm:
    case MTLPixelFormatA1BGR5Unorm:
    case MTLPixelFormatABGR4Unorm: bpp = 2; break;
    case MTLPixelFormatRGBA16Float:
    case MTLPixelFormatRGBA16Uint:
    case MTLPixelFormatRGBA16Sint: bpp = 8; break;
    case MTLPixelFormatRG16Uint:
    case MTLPixelFormatRG16Sint:
    case MTLPixelFormatRG16Float:
    case MTLPixelFormatR32Float: bpp = 4; break;
    case MTLPixelFormatRG32Float:
    case MTLPixelFormatRG32Uint: bpp = 8; break;
    default: bpp = 4; break;
    }
    const size_t slice = face > 0 ? static_cast<size_t>(face - 1) : 0;
    MTLRegion region = MTLRegionMake2D(0, 0, width, height);
    if (base_format == SCE_GXM_TEXTURE_BASE_FORMAT_U8U8U8
        || base_format == SCE_GXM_TEXTURE_BASE_FORMAT_S8S8S8) {
        const auto *src = static_cast<const uint8_t *>(pixels);
        std::vector<uint8_t> rgba(static_cast<size_t>(stride) * height * 4);
        for (uint32_t y = 0; y < height; ++y) {
            const auto *row = src + static_cast<size_t>(y) * stride * 3;
            auto *dst = rgba.data() + static_cast<size_t>(y) * stride * 4;
            for (uint32_t x = 0; x < width; ++x) {
                dst[x * 4 + 0] = row[x * 3 + 0];
                dst[x * 4 + 1] = row[x * 3 + 1];
                dst[x * 4 + 2] = row[x * 3 + 2];
                dst[x * 4 + 3] = base_format == SCE_GXM_TEXTURE_BASE_FORMAT_U8U8U8 ? 0xff : 0x7f;
            }
        }
        [tex replaceRegion:region mipmapLevel:mip_index slice:slice withBytes:rgba.data()
              bytesPerRow:static_cast<size_t>(stride) * 4 bytesPerImage:0];
        return;
    }
    const size_t row_bytes = stride * bpp;
    [tex replaceRegion:region mipmapLevel:mip_index slice:slice withBytes:pixels
          bytesPerRow:row_bytes bytesPerImage:0];
}

void MetalTextureCache::configure_sampler(size_t index, const SceGxmTexture &texture, bool no_linear) {
    MTLSamplerDescriptor *d = [MTLSamplerDescriptor new];
    const auto min = no_linear ? SCE_GXM_TEXTURE_FILTER_POINT : static_cast<SceGxmTextureFilter>(texture.min_filter);
    const auto mag = no_linear ? SCE_GXM_TEXTURE_FILTER_POINT : static_cast<SceGxmTextureFilter>(texture.mag_filter);
    d.minFilter = min == SCE_GXM_TEXTURE_FILTER_POINT ? MTLSamplerMinMagFilterNearest : MTLSamplerMinMagFilterLinear;
    d.magFilter = mag == SCE_GXM_TEXTURE_FILTER_POINT ? MTLSamplerMinMagFilterNearest : MTLSamplerMinMagFilterLinear;
    d.mipFilter = texture.mip_filter ? MTLSamplerMipFilterLinear : MTLSamplerMipFilterNearest;
    d.sAddressMode = MTLSamplerAddressModeRepeat;
    d.tAddressMode = MTLSamplerAddressModeRepeat;
    d.rAddressMode = MTLSamplerAddressModeRepeat;
    switch (static_cast<SceGxmTextureAddrMode>(texture.uaddr_mode)) {
    case SCE_GXM_TEXTURE_ADDR_CLAMP:
        d.sAddressMode = MTLSamplerAddressModeClampToEdge; break;
    case SCE_GXM_TEXTURE_ADDR_MIRROR:
        d.sAddressMode = MTLSamplerAddressModeMirrorRepeat; break;
    default: break;
    }
    switch (static_cast<SceGxmTextureAddrMode>(texture.vaddr_mode)) {
    case SCE_GXM_TEXTURE_ADDR_CLAMP:
        d.tAddressMode = MTLSamplerAddressModeClampToEdge; break;
    case SCE_GXM_TEXTURE_ADDR_MIRROR:
        d.tAddressMode = MTLSamplerAddressModeMirrorRepeat; break;
    default: break;
    }
    d.lodMinClamp = static_cast<float>(texture.true_lod_min());
    d.lodMaxClamp = FLT_MAX;
    samplers[index] = [state.device newSamplerStateWithDescriptor:d];
}

void MetalTextureCache::import_configure_impl(SceGxmTextureBaseFormat, uint32_t, uint32_t,
    bool, uint16_t, uint16_t, bool) {
}

MetalContext::MetalContext(MetalState &state, MemState &mem)
    : state(state), mem(mem) {
}

MetalContext::~MetalContext() = default;

void MetalContext::set_context(MetalRenderTarget *target) {
    render_target = target;
    first_render_pass = true;
    record.color_surface.downscale = false;
}

void MetalContext::set_vertex_stream(size_t index, size_t size, Ptr<const uint8_t> data) {
    if (index >= vertex_streams.size() || !data || !size) return;
    vertex_streams[index] = make_shared_buffer(state.device, data.get(mem), size);
    vertex_stream_sizes[index] = size;
}

void MetalContext::set_uniform(bool vertex, int block, uint32_t size, const Ptr<const void> data) {
    if (block < 0 || block >= static_cast<int>(SCE_GXM_MAX_TEXTURE_UNITS) || !data || !size)
        return;
    auto *program = vertex ? record.vertex_program.get(mem)->renderer_data.get()
                            : record.fragment_program.get(mem)->renderer_data.get();
    if (!program) return;
    const auto &offsets = program->uniform_buffer_data_offsets;
    if (block < 0 || block >= static_cast<int>(offsets.size())) return;
    const uint32_t offset = offsets[block];
    if (offset == static_cast<uint32_t>(-1)) return;
    auto &blob = vertex ? vertex_ubo_blob : fragment_ubo_blob;
    if (blob.size() < static_cast<size_t>(offset) + size)
        blob.resize(static_cast<size_t>(offset) + size);
    const void *src = data.get(mem);
    if (!src) return;
    memcpy(blob.data() + offset, src, size);
    auto buffer = make_shared_buffer(state.device, blob.data(), blob.size());
    if (vertex) vertex_uniforms[0] = buffer;
    else fragment_uniforms[0] = buffer;
}

void MetalContext::set_texture(uint32_t index, const SceGxmTexture &, bool) {
    // Binding is owned by sync_texture(), which has cache/sampler context.
    // Keep this compatibility hook side-effect free; the old vertex branch
    // could subtract from an already-unit-sized index and underflow.
    if (index >= SCE_GXM_MAX_TEXTURE_UNITS) return;
}

void MetalContext::draw(SceGxmPrimitiveType type, SceGxmIndexFormat index_type,
    Ptr<const void> indices, uint32_t count, uint32_t instance_count) {
    if (!render_target || !record.vertex_program || !record.fragment_program || !count || !indices)
        return;

    const auto *vp = dynamic_cast<const MetalVertexProgram *>(record.vertex_program.get(mem)->renderer_data.get());
    const auto *fp = dynamic_cast<const MetalFragmentProgram *>(record.fragment_program.get(mem)->renderer_data.get());
    if (!vp || !fp) return;

    auto pipeline = pipeline_for_draw();
    if (!pipeline) return;

    auto cmd = [state.command_queue commandBuffer];
    if (!cmd) return;
    command_buffer = cmd;

    MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = render_target->color;
    pass.colorAttachments[0].loadAction = first_render_pass ? MTLLoadActionClear : MTLLoadActionLoad;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
    pass.depthAttachment.texture = render_target->depth;
    pass.depthAttachment.loadAction = first_render_pass && !record.depth_stencil_surface.force_load ? MTLLoadActionClear : MTLLoadActionLoad;
    pass.depthAttachment.storeAction = MTLStoreActionStore;
    pass.depthAttachment.clearDepth = record.depth_stencil_surface.background_depth;
    pass.stencilAttachment.texture = render_target->depth;
    pass.stencilAttachment.loadAction = first_render_pass && !record.depth_stencil_surface.force_load ? MTLLoadActionClear : MTLLoadActionLoad;
    pass.stencilAttachment.storeAction = MTLStoreActionStore;
    pass.stencilAttachment.clearStencil = record.depth_stencil_surface.stencil;

    auto enc = [cmd renderCommandEncoderWithDescriptor:pass];
    if (!enc) return;
    [enc setRenderPipelineState:pipeline];
    [enc setDepthStencilState:depth_state_for_draw()];
    [enc setCullMode:cull_mode(record.cull_mode)];
    [enc setTriangleFillMode:fill_mode(record.front_polygon_mode)];
    [enc setFrontFacingWinding:MTLWindingCounterClockwise];
    [enc setViewport:(MTLViewport){0, 0, static_cast<double>(render_target->width), static_cast<double>(render_target->height), 0, 1}];

    for (size_t i = 0; i < record.vertex_streams.size(); ++i) {
        const auto &stream = record.vertex_streams[i];
        if (!stream.data || !stream.size) continue;
        auto buffer = make_shared_buffer(state.device, stream.data.get(mem), stream.size);
        if (buffer)
            [enc setVertexBuffer:buffer offset:0 atIndex:4 + i];
    }
    for (size_t i = 0; i < SCE_GXM_MAX_TEXTURE_UNITS; ++i) {
        if (vertex_textures[i]) {
            [enc setVertexTexture:vertex_textures[i] atIndex:i];
            [enc setVertexSamplerState:vertex_samplers[i] atIndex:i];
        }
        if (fragment_textures[i]) {
            [enc setFragmentTexture:fragment_textures[i] atIndex:i];
            [enc setFragmentSamplerState:fragment_samplers[i] atIndex:i];
        }
    }

    // Render-info buffers are bound to 2/3 for the OpenGL SPIR-V target.
    shader::RenderVertUniformBlock vert_info{};
    vert_info.viewport_flip = record.viewport_flip;
    vert_info.viewport_flag = record.viewport_flat ? 1.0f : 0.0f;
    vert_info.screen_width = static_cast<float>(render_target->width);
    vert_info.screen_height = static_cast<float>(render_target->height);
    vert_info.z_offset = record.z_offset;
    vert_info.z_scale = record.z_scale;

    shader::RenderFragUniformBlock frag_info{};
    frag_info.back_disabled = record.back_side_fragment_program_mode == SCE_GXM_FRAGMENT_PROGRAM_DISABLED;
    frag_info.front_disabled = record.front_side_fragment_program_mode == SCE_GXM_FRAGMENT_PROGRAM_DISABLED;
    frag_info.writing_mask = record.writing_mask;
    frag_info.use_raw_image = 0.0f;
    frag_info.res_multiplier = state.res_multiplier;

    const uint16_t vertex_texture_count = vp->texture_count;
    const uint16_t fragment_texture_count = fp->texture_count;
    const size_t vert_header = align(sizeof(vert_info), 8);
    const size_t frag_header = align(sizeof(frag_info), 8);
    const size_t vert_size = vert_header + vertex_texture_count * sizeof(float) * 4;
    const size_t frag_size = frag_header + fragment_texture_count * sizeof(float) * 4;
    std::vector<uint8_t> vert_bytes(vert_size);
    std::vector<uint8_t> frag_bytes(frag_size);
    memcpy(vert_bytes.data(), &vert_info, sizeof(vert_info));
    memcpy(frag_bytes.data(), &frag_info, sizeof(frag_info));
    const float ratio[2] = {1.0f / static_cast<float>(render_target->width), 1.0f / static_cast<float>(render_target->height)};
    const float offset[2] = {0.0f, 0.0f};
    for (uint16_t i = 0; i < vertex_texture_count; ++i) {
        memcpy(vert_bytes.data() + vert_header + i * 8, ratio, sizeof(ratio));
        memcpy(vert_bytes.data() + vert_header + vertex_texture_count * 8 + i * 8, offset, sizeof(offset));
    }
    for (uint16_t i = 0; i < fragment_texture_count; ++i) {
        memcpy(frag_bytes.data() + frag_header + i * 8, ratio, sizeof(ratio));
        memcpy(frag_bytes.data() + frag_header + fragment_texture_count * 8 + i * 8, offset, sizeof(offset));
    }

    auto vert_info_buffer = make_shared_buffer(state.device, vert_bytes.data(), vert_bytes.size());
    auto frag_info_buffer = make_shared_buffer(state.device, frag_bytes.data(), frag_bytes.size());
    [enc setVertexBuffer:vert_info_buffer offset:0 atIndex:2];
    [enc setFragmentBuffer:frag_info_buffer offset:0 atIndex:3];

    if (vertex_uniforms[0]) [enc setVertexBuffer:vertex_uniforms[0] offset:0 atIndex:0];
    if (fragment_uniforms[0]) [enc setFragmentBuffer:fragment_uniforms[0] offset:0 atIndex:1];

    const size_t index_size = index_type == SCE_GXM_INDEX_FORMAT_U16 ? sizeof(uint16_t) : sizeof(uint32_t);
    if (type == SCE_GXM_PRIMITIVE_TRIANGLE_FAN) {
        const size_t tri_count = count >= 3 ? count - 2 : 0;
        if (!tri_count) return;
        if (index_type == SCE_GXM_INDEX_FORMAT_U16) {
            const auto *src = static_cast<const uint16_t *>(indices.get(mem));
            std::vector<uint16_t> fan(tri_count * 3);
            for (size_t i = 0; i < tri_count; ++i) {
                fan[i * 3 + 0] = src[0];
                fan[i * 3 + 1] = src[i + 1];
                fan[i * 3 + 2] = src[i + 2];
            }
            index_buffer = make_shared_buffer(state.device, fan.data(), fan.size() * sizeof(uint16_t));
        } else if (index_type == SCE_GXM_INDEX_FORMAT_U32) {
            const auto *src = static_cast<const uint32_t *>(indices.get(mem));
            std::vector<uint32_t> fan(tri_count * 3);
            for (size_t i = 0; i < tri_count; ++i) {
                fan[i * 3 + 0] = src[0];
                fan[i * 3 + 1] = src[i + 1];
                fan[i * 3 + 2] = src[i + 2];
            }
            index_buffer = make_shared_buffer(state.device, fan.data(), fan.size() * sizeof(uint32_t));
        } else {
            return;
        }
        if (!index_buffer) return;
        const MTLIndexType metal_index_type = index_type == SCE_GXM_INDEX_FORMAT_U16 ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32;
        [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:static_cast<NSUInteger>(tri_count * 3)
            indexType:metal_index_type indexBuffer:index_buffer indexBufferOffset:0
            instanceCount:std::max<uint32_t>(1, instance_count)];
    } else {
        index_buffer = make_shared_buffer(state.device, indices.get(mem), count * index_size);
        if (!index_buffer) return;
        const MTLIndexType metal_index_type = index_type == SCE_GXM_INDEX_FORMAT_U16 ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32;
        [enc drawIndexedPrimitives:primitive_type(type) indexCount:count indexType:metal_index_type
            indexBuffer:index_buffer indexBufferOffset:0 instanceCount:std::max<uint32_t>(1, instance_count)];
    }

    [enc endEncoding];
    [cmd commit];
    first_render_pass = false;
}

id<MTLDepthStencilState> MetalContext::depth_state_for_draw() {
    const auto key = std::to_string(static_cast<uint32_t>(record.front_depth_func)) + ":" +
        std::to_string(static_cast<uint32_t>(record.front_depth_write_mode)) + ":" +
        std::to_string(static_cast<uint32_t>(record.front_stencil_state_op.func)) + ":" +
        std::to_string(record.front_stencil_state_values.ref);
    auto it = depth_states.find(key);
    if (it != depth_states.end()) return it->second;

    MTLDepthStencilDescriptor *d = [MTLDepthStencilDescriptor new];
    d.depthCompareFunction = compare_func(record.front_depth_func);
    d.depthWriteEnabled = record.front_depth_write_mode == SCE_GXM_DEPTH_WRITE_ENABLED;
    d.frontFaceStencil.stencilCompareFunction = stencil_func(record.front_stencil_state_op.func);
    d.frontFaceStencil.stencilFailureOperation = stencil_op(record.front_stencil_state_op.stencil_fail);
    d.frontFaceStencil.depthFailureOperation = stencil_op(record.front_stencil_state_op.depth_fail);
    d.frontFaceStencil.depthStencilPassOperation = stencil_op(record.front_stencil_state_op.depth_pass);
    d.frontFaceStencil.readMask = record.front_stencil_state_values.compare_mask;
    d.frontFaceStencil.writeMask = record.front_stencil_state_values.write_mask;
    d.backFaceStencil.stencilCompareFunction = stencil_func(record.back_stencil_state_op.func);
    d.backFaceStencil.stencilFailureOperation = stencil_op(record.back_stencil_state_op.stencil_fail);
    d.backFaceStencil.depthFailureOperation = stencil_op(record.back_stencil_state_op.depth_fail);
    d.backFaceStencil.depthStencilPassOperation = stencil_op(record.back_stencil_state_op.depth_pass);
    d.backFaceStencil.readMask = record.back_stencil_state_values.compare_mask;
    d.backFaceStencil.writeMask = record.back_stencil_state_values.write_mask;
    auto state_obj = [state.device newDepthStencilStateWithDescriptor:d];
    depth_states[key] = state_obj;
    return state_obj;
}

id<MTLRenderPipelineState> MetalContext::pipeline_for_draw() {
    auto *vp = dynamic_cast<MetalVertexProgram *>(record.vertex_program.get(mem)->renderer_data.get());
    auto *fp = dynamic_cast<MetalFragmentProgram *>(record.fragment_program.get(mem)->renderer_data.get());
    if (!vp || !fp) return nil;
    const auto key = pointer_key(vp, fp, record);
    auto found = pipelines.find(key);
    if (found != pipelines.end()) return found->second;

    if (!vp->function || !fp->function) {
        shader::Hints hints = shader_hints;
        hints.attributes = &vp->attributes;
        hints.color_format = record.color_surface.colorFormat;
        const auto *vgxp = state.gxp_ptr_map.at(vp->hash);
        const auto *fgxp = state.gxp_ptr_map.at(fp->hash);
        const auto v = compile_shader(*vgxp, "ger-metal-vert", state.features, hints, false);
        const auto f = compile_shader(*fgxp, "ger-metal-frag", state.features, hints, fp->has_blend && record.is_maskupdate);
        if (v.first.empty() || f.first.empty()) return nil;
        vp->msl = v.first; vp->entry = v.second;
        fp->msl = f.first; fp->entry = f.second;
        NSError *error = nil;
        id<MTLLibrary> vl = [state.device newLibraryWithSource:[NSString stringWithUTF8String:vp->msl.c_str()] options:nil error:&error];
        vp->function = vl ? [vl newFunctionWithName:[NSString stringWithUTF8String:vp->entry.c_str()]] : nil;
        if (!vp->function) {
            LOG_ERROR("GE:R Metal vertex MSL compile failed: {}", error.localizedDescription.UTF8String);
            return nil;
        }
        error = nil;
        id<MTLLibrary> fl = [state.device newLibraryWithSource:[NSString stringWithUTF8String:fp->msl.c_str()] options:nil error:&error];
        fp->function = fl ? [fl newFunctionWithName:[NSString stringWithUTF8String:fp->entry.c_str()]] : nil;
        if (!fp->function) {
            LOG_ERROR("GE:R Metal fragment MSL compile failed: {}", error.localizedDescription.UTF8String);
            return nil;
        }
    }

    MTLRenderPipelineDescriptor *d = [MTLRenderPipelineDescriptor new];
    d.vertexFunction = vp->function;
    d.fragmentFunction = fp->function;
    d.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;
    d.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
    d.stencilAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
    d.vertexDescriptor = [MTLVertexDescriptor vertexDescriptor];

    const auto *gxm_vp = record.vertex_program.get(mem);
    std::array<NSUInteger, SCE_GXM_MAX_VERTEX_STREAMS> strides{};
    if (gxm_vp) {
        for (size_t i = 0; i < gxm_vp->streams.size() && i < strides.size(); ++i)
            strides[i] = gxm_vp->streams[i].stride;
    }
    for (const auto &a : vp->attributes) {
        const auto info_it = vp->attribute_infos.find(a.regIndex);
        if (info_it == vp->attribute_infos.end()) continue;
        const uint16_t location = info_it->second.location;
        if (location >= 31) continue;
        d.vertexDescriptor.attributes[location].format = vertex_format(a.format, a.componentCount);
        d.vertexDescriptor.attributes[location].offset = a.offset;
        d.vertexDescriptor.attributes[location].bufferIndex = 4 + a.streamIndex;
        strides[a.streamIndex] = std::max<NSUInteger>(strides[a.streamIndex], a.offset + attribute_size(a.format, a.componentCount));
    }
    for (size_t i = 0; i < strides.size(); ++i)
        d.vertexDescriptor.layouts[4 + i].stride = strides[i] ? strides[i] : 4;
    if (fp->has_blend && !record.is_maskupdate) {
        auto &a = d.colorAttachments[0];
        a.blendingEnabled = fp->blend.colorFunc != SCE_GXM_BLEND_FUNC_NONE || fp->blend.alphaFunc != SCE_GXM_BLEND_FUNC_NONE;
        a.rgbBlendOperation = blend_op(fp->blend.colorFunc);
        a.alphaBlendOperation = blend_op(fp->blend.alphaFunc);
        a.sourceRGBBlendFactor = blend_factor(fp->blend.colorSrc);
        a.destinationRGBBlendFactor = blend_factor(fp->blend.colorDst);
        a.sourceAlphaBlendFactor = blend_factor(fp->blend.alphaSrc);
        a.destinationAlphaBlendFactor = blend_factor(fp->blend.alphaDst);
        MTLColorWriteMask mask = 0;
        if (fp->blend.colorMask & SCE_GXM_COLOR_MASK_R) mask |= MTLColorWriteMaskRed;
        if (fp->blend.colorMask & SCE_GXM_COLOR_MASK_G) mask |= MTLColorWriteMaskGreen;
        if (fp->blend.colorMask & SCE_GXM_COLOR_MASK_B) mask |= MTLColorWriteMaskBlue;
        if (fp->blend.colorMask & SCE_GXM_COLOR_MASK_A) mask |= MTLColorWriteMaskAlpha;
        a.writeMask = mask;
    }

    NSError *error = nil;
    auto p = [state.device newRenderPipelineStateWithDescriptor:d error:&error];
    if (!p) {
        LOG_ERROR("GE:R Metal pipeline creation failed: {}", error.localizedDescription.UTF8String);
        return nil;
    }
    pipelines[key] = p;
    return p;
}

void sync_texture(MetalContext &context, MemState &mem, size_t index,
    SceGxmTexture texture, const Config &config) {
    const bool vertex = index >= SCE_GXM_MAX_TEXTURE_UNITS;
    const size_t unit = vertex ? index - SCE_GXM_MAX_TEXTURE_UNITS : index;
    if (unit >= SCE_GXM_MAX_TEXTURE_UNITS) return;
    context.state.texture_cache.cache_and_bind_texture(texture, mem);
    const size_t bound = context.state.texture_cache.bound_index();
    auto tex = context.state.texture_cache.texture_at(bound);
    auto samp = context.state.texture_cache.sampler_at(context.state.texture_cache.last_bound_sampler_index);
    if (vertex) {
        context.vertex_textures[unit] = tex;
        context.vertex_samplers[unit] = samp;
        context.shader_hints.vertex_textures[unit] = gxm::get_format(texture);
    } else {
        context.fragment_textures[unit] = tex;
        context.fragment_samplers[unit] = samp;
        context.shader_hints.fragment_textures[unit] = gxm::get_format(texture);
    }
}

void sync_viewport_real(MetalContext &context, float xOffset, float yOffset,
    float zOffset, float xScale, float yScale, float zScale) {
    context.record.viewport_flip[0] = 1.0f;
    context.record.viewport_flip[1] = yScale < 0 ? -1.0f : 1.0f;
    context.record.viewport_flip[2] = 1.0f;
    context.record.viewport_flip[3] = 1.0f;
    context.record.z_offset = zOffset;
    context.record.z_scale = zScale;
}

void sync_viewport_flat(MetalContext &context) {
    context.record.viewport_flat = true;
    context.record.viewport_flip = {1.0f, -1.0f, 1.0f, 1.0f};
    context.record.z_offset = 0.0f;
    context.record.z_scale = 1.0f;
}

MetalState::MetalState()
    : texture_cache(*this) {
    current_backend = Backend::Metal;
}

MetalState::~MetalState() = default;

bool MetalState::init() {
    device = MTLCreateSystemDefaultDevice();
    if (!device) {
        LOG_ERROR("GE:R Metal: no MTLDevice");
        return false;
    }
    command_queue = [device newCommandQueue];
    if (!command_queue) return false;

    if (frame) {
        auto handle = frame->handle();
        if (auto *sdl = std::get_if<SDLDisplayHandle>(&handle); sdl && sdl->window) {
            metal_view = SDL_Metal_CreateView(sdl->window);
            layer = SDL_Metal_GetLayer(metal_view);
        }
    }
    if (!layer) {
        LOG_ERROR("GE:R Metal: SDL Metal layer unavailable");
        return false;
    }
    layer.device = device;
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer.framebufferOnly = NO;
    return true;
}

void MetalState::cleanup() {
    active_context = nullptr;
    if (metal_view) {
        SDL_Metal_DestroyView(metal_view);
        metal_view = nil;
    }
    layer = nil;
    command_queue = nil;
    device = nil;
}

void MetalState::late_init(const Config &, const std::string_view game_id, MemState &) {
    texture_cache.init(game_id);
    features.enable_memory_mapping = false;
    features.use_texture_viewport = true;
    features.support_rgb_attributes = true;
    features.support_scaled_attribute_formats = true;
    features.support_unmapped_surface_sync = true;
}

void MetalState::render_frame(DisplayState &display, const GxmState &, MemState &mem) {
    DisplayFrameInfo frame;
    {
        std::lock_guard<std::mutex> guard(display.display_info_mutex);
        frame = display.next_rendered_frame;
    }
    if (!frame.base || frame.image_size.x <= 0 || frame.image_size.y <= 0) return;
    present_frame(frame, mem);
}

void MetalState::present_frame(const DisplayFrameInfo &frame, MemState &mem) {
    if (!layer || !command_queue) return;
    const uint32_t width = static_cast<uint32_t>(frame.image_size.x);
    const uint32_t height = static_cast<uint32_t>(frame.image_size.y);
    if (!present_texture || present_texture.width != width || present_texture.height != height) {
        MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
            width:width height:height mipmapped:NO];
        d.usage = MTLTextureUsageShaderRead;
        d.storageMode = MTLStorageModeShared;
        present_texture = [device newTextureWithDescriptor:d];
    }
    const uint8_t *src = static_cast<const uint8_t *>(frame.base.get(mem));
    if (!src || !present_texture) return;
    [present_texture replaceRegion:MTLRegionMake2D(0, 0, width, height)
        mipmapLevel:0 withBytes:src bytesPerRow:frame.pitch * 4];

    if (!present_pipeline) {
        static NSString *source =
            @"#include <metal_stdlib>\nusing namespace metal;\n"
             "struct O { float4 p [[position]]; float2 uv; };\n"
             "vertex O v(uint id [[vertex_id]]) { float2 p[3]={{-1,-1},{3,-1},{-1,3}}; O o; o.p=float4(p[id],0,1); o.uv=float2((p[id].x+1)*0.5,1.0-(p[id].y+1)*0.5); return o; }\n"
             "fragment float4 f(O in [[stage_in]], texture2d<float> t [[texture(0)]], sampler s [[sampler(0)]]) { return t.sample(s,in.uv); }";
        NSError *error = nil;
        id<MTLLibrary> lib = [device newLibraryWithSource:source options:nil error:&error];
        if (!lib) {
            LOG_ERROR("GE:R Metal presentation shader failed: {}", error.localizedDescription.UTF8String);
            return;
        }
        MTLRenderPipelineDescriptor *p = [MTLRenderPipelineDescriptor new];
        p.vertexFunction = [lib newFunctionWithName:@"v"];
        p.fragmentFunction = [lib newFunctionWithName:@"f"];
        p.colorAttachments[0].pixelFormat = layer.pixelFormat;
        present_pipeline = [device newRenderPipelineStateWithDescriptor:p error:&error];
        if (!present_pipeline) return;
        MTLSamplerDescriptor *sd = [MTLSamplerDescriptor new];
        sd.minFilter = MTLSamplerMinMagFilterLinear;
        sd.magFilter = MTLSamplerMinMagFilterLinear;
        present_sampler = [device newSamplerStateWithDescriptor:sd];
    }

    id<CAMetalDrawable> drawable = [layer nextDrawable];
    if (!drawable) return;
    id<MTLCommandBuffer> cmd = [command_queue commandBuffer];
    MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = drawable.texture;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
    id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:pass];
    [enc setRenderPipelineState:present_pipeline];
    [enc setFragmentTexture:present_texture atIndex:0];
    [enc setFragmentSamplerState:present_sampler atIndex:0];
    [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [enc endEncoding];
    [cmd presentDrawable:drawable];
    [cmd commit];
    should_display = false;
    host_frames_presented.fetch_add(1, std::memory_order_relaxed);
}

void MetalState::swap_window() {
}

void MetalState::request_screen_rebuild() {
}

std::vector<uint32_t> MetalState::dump_frame(DisplayState &display, uint32_t &width, uint32_t &height) {
    DisplayFrameInfo frame;
    {
        std::lock_guard<std::mutex> guard(display.display_info_mutex);
        frame = display.next_rendered_frame;
    }
    width = static_cast<uint32_t>(frame.image_size.x);
    height = static_cast<uint32_t>(frame.image_size.y);
    return {};
}

uint32_t MetalState::get_features_mask() {
    return (features.use_texture_viewport ? 1u : 0u)
        | (features.support_rgb_attributes ? 2u : 0u)
        | (features.support_scaled_attribute_formats ? 4u : 0u);
}

int MetalState::get_supported_filters() {
    return static_cast<int>(Filter::NEAREST) | static_cast<int>(Filter::BILINEAR);
}

void MetalState::set_screen_filter(const std::string_view &) {
}

int MetalState::get_max_anisotropic_filtering() { return 1; }

void MetalState::set_anisotropic_filtering(int value) {
    texture_cache.anisotropic_filtering = std::max(1, value);
}

int MetalState::get_max_2d_texture_width() { return 16384; }
void MetalState::set_async_compilation(bool) {}
uint32_t MetalState::get_gpu_version() { return 0; }
std::string_view MetalState::get_gpu_name() { return device ? device.name.UTF8String : "Metal"; }
void MetalState::precompile_shader(const ShadersHash &) {}
void MetalState::preclose_action() {}
bool MetalState::map_memory(MemState &, Ptr<void>, uint32_t) { return true; }
void MetalState::unmap_memory(MemState &, Ptr<void>) {}

bool create(MetalState &state, std::unique_ptr<Context> &context, MemState &mem) {
    context = std::make_unique<MetalContext>(state, mem);
    state.active_context = static_cast<MetalContext *>(context.get());
    return true;
}

bool create(MetalState &state, std::unique_ptr<RenderTarget> &target,
    const SceGxmRenderTargetParams &params) {
    auto rt = std::make_unique<MetalRenderTarget>();
    rt->width = std::max<uint32_t>(1, static_cast<uint32_t>(params.width * state.res_multiplier));
    rt->height = std::max<uint32_t>(1, static_cast<uint32_t>(params.height * state.res_multiplier));
    rt->color = [state.device newTextureWithDescriptor:({
        MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
            width:rt->width height:rt->height mipmapped:NO];
        d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        // sync_surface() currently reads this target back with getBytes().
        d.storageMode = MTLStorageModeShared; d;
    })];
    MTLTextureDescriptor *dd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float_Stencil8
        width:rt->width height:rt->height mipmapped:NO];
    dd.usage = MTLTextureUsageRenderTarget;
    dd.storageMode = MTLStorageModePrivate;
    rt->depth = [state.device newTextureWithDescriptor:dd];
    target = std::move(rt);
    return target->get() != nullptr;
}

void destroy(MetalState &, std::unique_ptr<RenderTarget> &target) {
    target.reset();
}

bool create(std::unique_ptr<VertexProgram> &program, MetalState &, const SceGxmProgram &gxm,
    const std::vector<SceGxmVertexAttribute> &attributes) {
    auto p = std::make_unique<MetalVertexProgram>();
    p->attributes = attributes;
    program = std::move(p);
    (void)gxm;
    return true;
}

bool create(std::unique_ptr<FragmentProgram> &program, MetalState &, const SceGxmProgram &gxm,
    const SceGxmBlendInfo *blend) {
    auto p = std::make_unique<MetalFragmentProgram>();
    if (blend) { p->blend = *blend; p->has_blend = true; }
    (void)gxm;
    program = std::move(p);
    return true;
}

void sync_texture(MetalContext &context, MemState &mem, size_t index,
    SceGxmTexture texture, const Config &config) {
    const bool vertex = index >= SCE_GXM_MAX_TEXTURE_UNITS;
    const size_t unit = vertex ? index - SCE_GXM_MAX_TEXTURE_UNITS : index;
    if (unit >= SCE_GXM_MAX_TEXTURE_UNITS) return;
    context.state.texture_cache.cache_and_bind_texture(texture, mem);
    const size_t bound = context.state.texture_cache.bound_index();
    const auto tex = context.state.texture_cache.texture_at(bound);
    const auto sampler = context.state.texture_cache.sampler_at(
        context.state.texture_cache.last_bound_sampler_index < TextureCacheSize
            ? context.state.texture_cache.last_bound_sampler_index : 0);
    if (vertex) {
        context.vertex_textures[unit] = tex;
        context.vertex_samplers[unit] = sampler;
        context.shader_hints.vertex_textures[unit] = gxm::get_format(texture);
    } else {
        context.fragment_textures[unit] = tex;
        context.fragment_samplers[unit] = sampler;
        context.shader_hints.fragment_textures[unit] = gxm::get_format(texture);
    }
    (void)config;
}

void sync_viewport_real(MetalContext &context, float, float, float zOffset,
    float, float yScale, float zScale) {
    context.record.viewport_flat = false;
    context.record.viewport_flip[0] = 1.0f;
    context.record.viewport_flip[1] = yScale < 0 ? -1.0f : 1.0f;
    context.record.viewport_flip[2] = 1.0f;
    context.record.viewport_flip[3] = 1.0f;
    context.record.z_offset = zOffset;
    context.record.z_scale = zScale;
}

void sync_viewport_flat(MetalContext &context) {
    context.record.viewport_flat = true;
    context.record.viewport_flip = {1.0f, -1.0f, 1.0f, 1.0f};
    context.record.z_offset = 0.0f;
    context.record.z_scale = 1.0f;
}

void MetalContext::sync_surface(const SceGxmNotification &vertex, const SceGxmNotification &fragment) {
    if (!command_buffer || !render_target || !record.color_surface.data) return;
    [command_buffer waitUntilCompleted];
    const size_t width = record.color_surface.width;
    const size_t height = record.color_surface.height;
    const size_t stride = record.color_surface.strideInPixels;
    const size_t bytes = height * stride * 4;
    std::vector<uint8_t> pixels(bytes);
    [render_target->color getBytes:pixels.data() bytesPerRow:stride * 4
        bytesPerImage:0 from:MTLRegionMake2D(0, 0, width, height) mipmapLevel:0 slice:0];
    auto *dst = static_cast<uint8_t *>(record.color_surface.data.get(mem));
    if (dst) {
        for (size_t y = 0; y < height; ++y)
            memcpy(dst + y * stride * 4, pixels.data() + y * width * 4, width * 4);
    }

    {
        std::unique_lock<std::mutex> lock(state.notification_mutex);
        if (vertex.address)
            *vertex.address.get(mem) = vertex.value;
        if (fragment.address)
            *fragment.address.get(mem) = fragment.value;
        lock.unlock();
        state.notification_ready.notify_all();
    }
}

} // namespace renderer::metal
