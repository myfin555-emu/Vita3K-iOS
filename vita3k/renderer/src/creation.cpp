// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

#include <gxm/types.h>
#include <renderer/commands.h>
#include <renderer/driver_functions.h>
#include <renderer/state.h>
#include <renderer/types.h>

#include <renderer/metal/state.h>

#include <gxm/functions.h>
#include <renderer/functions.h>
#include <util/align.h>
#include <util/log.h>
#include <util/tracy.h>

namespace renderer {

static void layout_ssbo_offset_from_uniform_buffer_sizes(UniformBufferSizes &sizes, UniformBufferSizes &offsets, std::size_t &total_hold) {
    std::uint32_t last_offset = 0;

    for (std::size_t i = 0; i < sizes.size(); i++) {
        if (sizes[i] != 0) {
            // Round to vec4 unit
            offsets[i] = last_offset;
            last_offset += align(sizes[i], 4);
        } else {
            offsets[i] = static_cast<std::uint32_t>(-1);
        }
    }

    total_hold = static_cast<std::size_t>(last_offset);
}

COMMAND(handle_create_context) {
    TRACY_FUNC_COMMANDS(handle_create_context);
    std::unique_ptr<Context> *ctx = helper.pop<std::unique_ptr<Context> *>();
    bool result = false;

    switch (renderer.current_backend) {
    case Backend::Metal: {
        result = metal::create(static_cast<metal::MetalState &>(renderer), *ctx, mem);
        break;
    }

    default: {
        REPORT_MISSING(renderer.current_backend);
        break;
    }
    }

    renderer.context = ctx->get();

    // fill with default values
    renderer.context->shader_hints = {
        .attributes = nullptr,
        .color_format = SCE_GXM_COLOR_FORMAT_U8U8U8U8_ABGR
    };
    std::fill_n(renderer.context->shader_hints.vertex_textures, SCE_GXM_MAX_TEXTURE_UNITS, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR);
    std::fill_n(renderer.context->shader_hints.fragment_textures, SCE_GXM_MAX_TEXTURE_UNITS, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR);

    complete_command(renderer, helper, result);
}

COMMAND(handle_destroy_context) {
    TRACY_FUNC_COMMANDS(handle_destroy_context);
    std::unique_ptr<Context> *ctx = helper.pop<std::unique_ptr<Context> *>();

    ctx->reset();
    renderer.context = nullptr;

    complete_command(renderer, helper, 0);
}

COMMAND(handle_create_render_target) {
    TRACY_FUNC_COMMANDS(handle_create_render_target);
    std::unique_ptr<RenderTarget> *render_target = helper.pop<std::unique_ptr<RenderTarget> *>();
    SceGxmRenderTargetParams *params = helper.pop<SceGxmRenderTargetParams *>();

    bool result = false;

    switch (renderer.current_backend) {
    case Backend::Metal:
        result = metal::create(static_cast<metal::MetalState &>(renderer), *render_target, *params);
        break;

    default:
        REPORT_MISSING(renderer.current_backend);
        break;
    }
    (*render_target)->multisample_mode = params->multisampleMode;
    (*render_target)->has_macroblock_sync = (params->flags & SCE_GXM_RENDER_TARGET_MACROTILE_SYNC);
    if ((*render_target)->has_macroblock_sync) {
        // there are between 1 and 4 macroblocks in the x and y direction
        uint16_t nb_macroblocks_x = (params->flags >> 8) & 0b111;
        uint16_t nb_macroblocks_y = (params->flags >> 12) & 0b111;

        // the width and height should be multiple of 128
        (*render_target)->macroblock_width = static_cast<uint16_t>((params->width / nb_macroblocks_x) * renderer.res_multiplier);
        (*render_target)->macroblock_height = static_cast<uint16_t>((params->height / nb_macroblocks_y) * renderer.res_multiplier);
    }

    complete_command(renderer, helper, result);
}

COMMAND(handle_destroy_render_target) {
    TRACY_FUNC_COMMANDS(handle_destroy_render_target);
    std::unique_ptr<RenderTarget> *render_target = helper.pop<std::unique_ptr<RenderTarget> *>();

    switch (renderer.current_backend) {
    case Backend::Metal:
        metal::destroy(dynamic_cast<metal::MetalState &>(renderer), *render_target);
        break;

    default:
        REPORT_MISSING(renderer.current_backend);
        break;
    }

    render_target->reset();

    complete_command(renderer, helper, 0);
}

COMMAND(handle_memory_map) {
    TRACY_FUNC_COMMANDS(handle_memory_map);
    const Ptr<void> addr = helper.pop<Ptr<void>>();
    const uint32_t size = helper.pop<uint32_t>();

    if (renderer.current_backend == Backend::Metal) {
        static_cast<metal::MetalState &>(renderer).map_memory(mem, addr, size);
    }

    complete_command(renderer, helper, 0);
}

COMMAND(handle_memory_unmap) {
    TRACY_FUNC_COMMANDS(handle_memory_unmap);

    const Ptr<void> addr = helper.pop<Ptr<void>>();

    if (renderer.current_backend == Backend::Metal) {
        static_cast<metal::MetalState &>(renderer).unmap_memory(mem, addr);
    }

    complete_command(renderer, helper, 0);
}

// Client
bool create(std::unique_ptr<FragmentProgram> &fp, State &state, const SceGxmProgram &program, const SceGxmBlendInfo *blend, GXPPtrMap &gxp_ptr_map) {
    switch (state.current_backend) {
    case Backend::Metal:
        if (!metal::create(fp, static_cast<metal::MetalState &>(state), program, blend))
            return false;
        break;

    default:
        REPORT_MISSING(state.current_backend);
        return false;
    }

    // Try to hash this shader
    fp->hash = sha256(&program, program.size);
    gxp_ptr_map.emplace(fp->hash, &program);

    fp->buffer_count = shader::usse::get_uniform_buffer_sizes(program, fp->uniform_buffer_sizes);
    layout_ssbo_offset_from_uniform_buffer_sizes(fp->uniform_buffer_sizes, fp->uniform_buffer_data_offsets, fp->max_total_uniform_buffer_storage);
    fp->textures_used = gxp::get_textures_used(program);
    fp->texture_count = std::bit_width(fp->textures_used.to_ulong());

    return true;
}

bool create(std::unique_ptr<VertexProgram> &vp, State &state, const SceGxmProgram &program, GXPPtrMap &gxp_ptr_map, const std::vector<SceGxmVertexAttribute> &attributes) {
    switch (state.current_backend) {
    case Backend::Metal:
        if (!metal::create(vp, static_cast<metal::MetalState &>(state), program, attributes))
            return false;
        break;

    default:
        REPORT_MISSING(state.current_backend);
        return false;
    }

    // Hash this shader
    vp->hash = sha256(&program, program.size);
    gxp_ptr_map.emplace(vp->hash, &program);

    vp->buffer_count = shader::usse::get_uniform_buffer_sizes(program, vp->uniform_buffer_sizes);
    shader::usse::get_attribute_informations(program, vp->attribute_infos);
    layout_ssbo_offset_from_uniform_buffer_sizes(vp->uniform_buffer_sizes, vp->uniform_buffer_data_offsets, vp->max_total_uniform_buffer_storage);
    vp->textures_used = gxp::get_textures_used(program);
    vp->texture_count = std::bit_width(vp->textures_used.to_ulong());

    if (vp->attribute_infos.empty()) {
        // Insert some symbols here
        if (program.primary_reg_count != 0) {
            for (size_t i = 0; i < attributes.size(); i++) {
                vp->attribute_infos.emplace(attributes[i].regIndex, shader::usse::AttributeInformation(static_cast<uint16_t>(i), SCE_GXM_PARAMETER_TYPE_F32, 1, false, false, false));
            }
        }
    }

    return true;
}

void create(SceGxmSyncObject *sync, State &state) {
    // Set as if the last display was already done
    sync->last_display = 0;
    sync->timestamp_current = 0;
    sync->timestamp_ahead = 0;
    sync->being_deleted = false;
}

void destroy(SceGxmSyncObject *sync, State &state) {
    // nothing to do right now
}

bool init(FrameHost &frame, std::unique_ptr<State> &state, Backend backend, const Config &config, const Root &root_paths) {
    switch (backend) {
    case Backend::Metal:
        state = std::make_unique<metal::MetalState>();
        state->frame = &frame;
        state->init_paths(root_paths);
        if (!state->init())
            return false;
        break;

    default:
        LOG_ERROR("Cannot create a renderer with unsupported backend {}.", static_cast<int>(backend));
        return false;
    }

    state->current_backend = backend;

    // Can change this
    state->command_buffer_queue.maxPendingCount_ = 30;

    return true;
}
} // namespace renderer
