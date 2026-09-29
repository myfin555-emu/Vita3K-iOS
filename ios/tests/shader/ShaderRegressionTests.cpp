// Vita3K emulator project
// SPDX-License-Identifier: GPL-2.0-or-later
#include <SPIRV/SpvBuilder.h>
#include <fstream>
#include <iostream>
#include <map>
#include <shader/gxp_parser.h>
#include <shader/spirv_recompiler.h>
#include <shader/usse_program_analyzer.h>
#include <shader/usse_utilities.h>
#include <spirv_msl.hpp>
#include <stdexcept>

using namespace shader::usse;

static void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

// Synthetic instructions, not game assets. Field layouts come from the USSE decoder.
static uint64_t encode(const std::string &pattern, std::map<char, uint32_t> values) {
    require(pattern.size() == 64, "instruction must be 64 bits");
    uint64_t result = 0;
    for (int i = 63; i >= 0; --i) {
        const char field = pattern[i];
        bool bit = field == '1';
        if (field != '0' && field != '1' && field != '-') {
            bit = values[field] & 1;
            values[field] >>= 1;
        }
        result |= uint64_t(bit) << (63 - i);
    }
    for (const auto &[field, remaining] : values)
        require(remaining == 0, "instruction field overflow");
    return result;
}

struct SyntheticProgram {
    SceGxmProgram program{};
    SceGxmProgramVertexVaryings varyings{};
    SceGxmProgramParameter parameters[2]{};
    SceGxmProgramParameterContainer containers[1]{};
    char attribute_name[16] = "raw_attribute";
    alignas(8) uint64_t code[32]{};

    explicit SyntheticProgram(bool fragment = true) {
        program.magic = 0x00505847;
        program.major_version = 1;
        program.minor_version = 4;
        program.size = sizeof(*this);
        program.program_flags = fragment ? SCE_GXM_PROGRAM_FLAG_FRAGMENT | SCE_GXM_PROGRAM_FLAG_NATIVECOLOR_USED : 0;
        program.varyings_offset = offset(program.varyings_offset, &varyings);
        program.parameters_offset = offset(program.parameters_offset, parameters);
        program.container_offset = offset(program.container_offset, containers);
        program.primary_program_offset = offset(program.primary_program_offset, code);
        program.secondary_program_offset = offset(program.secondary_program_offset, code);
        program.secondary_program_offset_end = offset(program.secondary_program_offset_end, code);
        if (fragment) {
            varyings.output_param_type = SCE_GXM_PARAMETER_TYPE_F32;
            varyings.output_comp_count = 4;
        }
    }
    static uint32_t offset(uint32_t &field, const void *target) {
        return static_cast<const uint8_t *>(target) - reinterpret_cast<const uint8_t *>(&field);
    }
};

static size_t count_op(const std::vector<uint32_t> &words, spv::Op opcode, int operand = -1, uint32_t value = 0) {
    size_t found = 0;
    for (size_t pos = 5; pos < words.size();) {
        const uint32_t size = words[pos] >> 16;
        require(size > 0 && pos + size <= words.size(), "invalid SPIR-V instruction size");
        if ((words[pos] & 0xffff) == opcode && (operand < 0 || (size > uint32_t(operand) && words[pos + operand] == value)))
            ++found;
        pos += size;
    }
    return found;
}

static std::vector<uint32_t> compile(SyntheticProgram &fixture, bool mapping, const std::string &name, const std::string &directory, bool interlock = false) {
    FeatureState features{};
    features.support_shader_interlock = interlock;
    features.enable_memory_mapping = mapping;
    shader::Hints hints{};
    std::vector<SceGxmVertexAttribute> attributes;
    hints.attributes = &attributes;
    hints.color_format = SCE_GXM_COLOR_FORMAT_U8U8U8U8_ABGR;
    auto result = shader::convert_gxp(fixture.program, name, features, shader::Target::SpirVVulkan, hints);
    require(!result.spirv.empty(), "compiler returned empty module");
    std::ofstream file(directory + "/" + name + ".spv", std::ios::binary);
    file.write(reinterpret_cast<const char *>(result.spirv.data()), result.spirv.size() * sizeof(uint32_t));
    require(bool(file), "could not save generated shader");
    // Exercise the same translation family used by MoltenVK (no Metal SDK/compiler on Linux).
    spirv_cross::CompilerMSL msl(result.spirv);
    auto options = msl.get_msl_options();
    options.platform = spirv_cross::CompilerMSL::Options::iOS;
    options.set_msl_version(2, 3);
    options.check_discarded_frag_stores = true;
    msl.set_msl_options(options);
    const auto metal = msl.compile();
    require(!metal.empty(), "SPIRV-Cross failed to emit Metal source");
    if (interlock)
        require(metal.find("simd_is_helper_thread") == std::string::npos, "interlock discard emitted unsupported helper query");
    return result.spirv;
}

// Register-format streams contain already packed USSE register bits. Driver
// support for scaled/RGB formats must not change how those bits reach pa[].
static void test_raw_vertex_inputs(const std::string &directory) {
    for (bool half : {true, false}) {
        for (bool raw : {false, true}) {
            SyntheticProgram fixture(false);
            fixture.program.parameter_count = 1;
            fixture.program.primary_reg_count = 4;
            fixture.varyings.untyped_pa_regs = raw ? 1 : 0;
            auto &parameter = fixture.parameters[0];
            parameter.name_offset = reinterpret_cast<const uint8_t *>(fixture.attribute_name)
                - reinterpret_cast<const uint8_t *>(&parameter.name_offset);
            parameter.category = SCE_GXM_PARAMETER_CATEGORY_ATTRIBUTE;
            parameter.type = half ? SCE_GXM_PARAMETER_TYPE_F16 : SCE_GXM_PARAMETER_TYPE_F32;
            parameter.component_count = 4;
            parameter.array_size = 1;
            std::vector<SceGxmVertexAttribute> attributes(1);
            attributes[0].format = raw && !half ? SCE_GXM_ATTRIBUTE_FORMAT_UNTYPED : SCE_GXM_ATTRIBUTE_FORMAT_U16;
            attributes[0].componentCount = half && raw ? 4 : 3;
            shader::Hints hints{};
            hints.attributes = &attributes;
            FeatureState features{};
            features.support_scaled_attribute_formats = false;
            features.support_rgb_attributes = false;
            const auto name = std::string(raw ? "raw_" : "typed_") + (half ? "half" : "float");
            auto translated = shader::convert_gxp(fixture.program, name, features, shader::Target::SpirVVulkan, hints);
            require(!translated.spirv.empty(), "attribute shader generation failed");
            const auto conversions = count_op(translated.spirv, spv::OpConvertUToF);
            require(raw ? conversions == 0 : conversions > 0,
                "raw register bits must bypass scaled conversion; typed inputs must retain it");
            features.support_scaled_attribute_formats = true;
            features.support_rgb_attributes = true;
            auto native = shader::convert_gxp(fixture.program, name, features, shader::Target::SpirVVulkan, hints);
            if (raw)
                require(translated.spirv == native.spirv,
                    "raw input changes with scaled/RGB driver capabilities");
            std::ofstream file(directory + "/" + name + ".spv", std::ios::binary);
            file.write(reinterpret_cast<const char *>(translated.spirv.data()), translated.spirv.size() * sizeof(uint32_t));
            require(bool(file), "could not save attribute shader");
            spirv_cross::CompilerMSL msl(translated.spirv);
            auto options = msl.get_msl_options();
            options.platform = spirv_cross::CompilerMSL::Options::iOS;
            options.set_msl_version(2, 3);
            msl.set_msl_options(options);
            const auto metal = msl.compile();
            require(!metal.empty(), "attribute shader MSL generation failed");
            std::ofstream(directory + "/" + name + ".metal") << metal;
        }
    }
}

static void test_uniform_layout() {
    SyntheticProgram fixture;
    fixture.program.parameter_count = 2;
    fixture.program.primary_reg_count = 4;
    fixture.program.container_count = 1;
    fixture.containers[0] = { 3, 0, 12, 8 };
    for (auto &parameter : fixture.parameters) {
        parameter.category = SCE_GXM_PARAMETER_CATEGORY_UNIFORM_BUFFER;
        parameter.resource_index = 3;
    }
    fixture.parameters[0].array_size = 16;
    fixture.parameters[1].array_size = 64;
    auto inputs = shader::get_program_input(fixture.program);
    bool found = false;
    for (const auto &buffer : inputs.uniform_buffers) {
        if (buffer.index == 3) {
            found = true;
            require(buffer.reg_start_offset == 12 && buffer.reg_block_size == 8, "uniform container secondary-register mapping lost");
            require(buffer.size == 16, "duplicate uniform declaration must keep maximum size");
        }
    }
    require(found, "uniform buffer missing");
}

static void test_predicates_and_lod() {
    require(static_cast<uint8_t>(ExtPredicate::PN) == 7, "scalar predicate encoding must remain unchanged");
    require(ext_vec_predicate_to_ext(ExtVecPredicate::NEGP2) == ExtPredicate::NEGP2, "NEGP2 mapping");
    auto instruction = encode("00001pppsrrydcbawwwwneeeemmoiittkkllffffffzzzzzzzggghhhhhhjjjjjj", { { 'p', 6 } });
    require(get_predicate(instruction) == static_cast<uint8_t>(ExtPredicate::NEGP2), "vector NEGP2 decoding");
    for (unsigned lod = 0; lod < 16; ++lod) {
        SceGxmTexture texture{};
        texture.lod_min0 = lod >> 2;
        texture.lod_min1 = lod & 3;
        require(texture.true_lod_min() == lod, "split texture min LOD decoded incorrectly");
    }
}

static void test_branch_analysis() {
    // Jump over instruction 2 to instruction 3. The old analyzer moved its node
    // into the tree, then shadowed the replacement, leaving a dangling/null node.
    const uint64_t ordinary = uint64_t{ 0b00110 } << 59;
    const uint64_t branch = (uint64_t{ 0b11111 } << 59) | 2;
    std::vector<uint64_t> code = { ordinary, branch, ordinary, ordinary, ordinary };
    USSEBlockNode root(nullptr, 0);
    analyze(root, code.size() - 1, [&](USSEOffset offset) { return code.at(offset); });
    require(root.children_count() == 2, "branch must create two code spans");
    auto first = static_cast<const USSECodeNode *>(root.children_at(0));
    auto second = static_cast<const USSECodeNode *>(root.children_at(1));
    require(first->offset == 0 && first->size == 1, "pre-branch code span");
    require(second->offset == 3 && second->size == 2, "branch target code span");
    code[1] = (uint64_t{ 0b11111 } << 59) | 20;
    analyze(root, code.size() - 1, [&](USSEOffset offset) { return code.at(offset); });
    require(root.children_count() == 1, "out-of-block branch must stop analysis");
}

static void test_address_arithmetic(const std::string &directory) {
    spv::SpvBuildLogger logger;
    spv::Builder b(0x00010000, 0, &logger);
    b.addCapability(spv::CapabilityShader);
    b.addExtension("SPV_KHR_storage_buffer_storage_class");
    b.setMemoryModel(spv::AddressingModelLogical, spv::MemoryModelGLSL450);
    auto main = b.makeEntryPoint("main");
    b.addEntryPoint(spv::ExecutionModelGLCompute, main, "main");
    b.addExecutionMode(main, spv::ExecutionModeLocalSize, 1, 1, 1);
    const auto u32 = b.makeUintType(32);
    const auto uvec2 = b.makeVectorType(u32, 2);
    const uint64_t bases[] = { 0, 0x100000000ULL, 0x12345678fffffff0ULL, 0xffffffffffffffffULL };
    const int32_t offsets[] = { 0, 4, -4, 0x7fffffff, (-2147483647 - 1) };
    constexpr unsigned count = std::size(bases) * std::size(offsets);
    auto array = b.makeArrayType(uvec2, b.makeUintConstant(count), 8);
    b.addDecoration(array, spv::DecorationArrayStride, 8);
    auto structure = b.makeStructType({ array }, "Results");
    b.addDecoration(structure, spv::DecorationBlock);
    b.addMemberDecoration(structure, 0, spv::DecorationOffset, 0);
    auto output = b.createVariable(spv::NoPrecision, spv::StorageClassStorageBuffer, structure, "output");
    b.addDecoration(output, spv::DecorationDescriptorSet, 0);
    b.addDecoration(output, spv::DecorationBinding, 0);
    std::ofstream expected(directory + "/address_expected.txt");
    unsigned index = 0;
    for (auto base : bases) {
        for (auto offset : offsets) {
            auto value = b.makeCompositeConstant(uvec2, { b.makeUintConstant(static_cast<uint32_t>(base)), b.makeUintConstant(static_cast<uint32_t>(base >> 32)) });
            auto result = utils::add_uvec2_int(b, value, b.makeIntConstant(offset));
            auto target = utils::create_access_chain(b, spv::StorageClassStorageBuffer, output, { b.makeIntConstant(0), b.makeIntConstant(index++) });
            b.createStore(result, target);
            const uint64_t sum = base + static_cast<uint64_t>(static_cast<int64_t>(offset));
            expected << static_cast<uint32_t>(sum) << ' ' << static_cast<uint32_t>(sum >> 32) << '\n';
        }
    }
    b.makeReturn(false);
    b.leaveFunction();
    std::vector<uint32_t> words;
    b.dump(words);
    std::ofstream file(directory + "/address_arithmetic.spv", std::ios::binary);
    file.write(reinterpret_cast<const char *>(words.data()), words.size() * sizeof(uint32_t));
    require(bool(file) && bool(expected), "address fixture output failed");
}

static void test_mapped_buffer_access(const std::string &directory) {
    spv::SpvBuildLogger logger;
    spv::Builder b(0x00010000, 0, &logger);
    b.addCapability(spv::CapabilityShader);
    b.addCapability(spv::CapabilityPhysicalStorageBufferAddresses);
    b.addExtension("SPV_KHR_physical_storage_buffer");
    b.setMemoryModel(spv::AddressingModelPhysicalStorageBuffer64, spv::MemoryModelGLSL450);
    auto main = b.makeEntryPoint("main");
    b.addEntryPoint(spv::ExecutionModelGLCompute, main, "main");
    b.addExecutionMode(main, spv::ExecutionModeLocalSize, 1, 1, 1);
    const auto f32 = b.makeFloatType(32);
    const auto v4 = b.makeVectorType(f32, 4);
    const auto array = b.makeArrayType(b.makeVectorType(b.makeUintType(32), 2), b.makeUintConstant(2), 16);
    b.addDecoration(array, spv::DecorationArrayStride, 16);
    auto structure = b.makeStructType({ array }, "Addresses");
    b.addDecoration(structure, spv::DecorationBlock);
    b.addMemberDecoration(structure, 0, spv::DecorationOffset, 0);
    SpirvShaderParameters params{};
    params.render_info_id = b.createVariable(spv::NoPrecision, spv::StorageClassUniform, structure, "addresses");
    b.addDecoration(params.render_info_id, spv::DecorationDescriptorSet, 0);
    b.addDecoration(params.render_info_id, spv::DecorationBinding, 0);
    params.buffer_addresses_id = 0;
    params.buffer_count = 2;
    const auto registers = b.makeArrayType(v4, b.makeIntConstant(4), 0);
    params.temps = b.createVariable(spv::NoPrecision, spv::StorageClassPrivate, registers, "r", b.makeNullConstant(registers));
    FeatureState features{};
    features.enable_memory_mapping = true;
    utils::SpirvUtilFunctions utils{};
    utils.std_builtins = b.import("GLSL.std.450");
    Operand dest{};
    dest.bank = RegisterBank::TEMP;
    dest.type = DataType::F32;
    dest.swizzle = SWIZZLE_CHANNEL_4_DEFAULT;
    for (unsigned component_size : { 1u, 2u, 4u }) {
        for (bool store : { false, true }) {
            utils::buffer_address_access(b, params, utils, features, dest, 0,
                b.makeIntConstant(0x10000000 - 4), component_size, 4, -1, store);
        }
    }
    b.makeReturn(false);
    b.leaveFunction();
    std::vector<uint32_t> words;
    b.dump(words);
    std::ofstream file(directory + "/mapped_buffer_access.spv", std::ios::binary);
    file.write(reinterpret_cast<const char *>(words.data()), words.size() * sizeof(uint32_t));
    require(bool(file), "mapped buffer fixture output failed");
}

static void test_translation(const std::string &directory) {
    constexpr auto vcomp = "00110pppsddyenr-aaaaobbccmmff-ttkk--ggggggg-------hhhhhhh---wwww";
    for (bool mapping : { false, true }) {
        SyntheticProgram fixture;
        fixture.program.primary_program_instr_count = 2;
        fixture.code[0] = encode(vcomp, { { 'b', 2 }, { 'w', 15 } }); // VLOG r0.xyzw, r0.x
        fixture.code[1] = encode(vcomp, { { 'b', 3 }, { 'g', 2 }, { 'w', 15 }, { 'n', 1 } }); // VEXP r4.xyzw, r0.x; end
        auto words = compile(fixture, mapping, mapping ? "complex_mapped" : "complex_unmapped", directory);
        require(count_op(words, spv::OpExtInst, 4, GLSLstd450Log2) == 1, "VLOG must emit Log2");
        require(count_op(words, spv::OpExtInst, 4, GLSLstd450Exp2) == 1, "VEXP must emit Exp2");
        require(count_op(words, spv::OpExtInst, 4, GLSLstd450Log) == 0, "natural log must not appear");
        require(count_op(words, spv::OpExtInst, 4, GLSLstd450Exp) == 0, "natural exp must not appear");
        require(count_op(words, spv::OpDecorate, 2, spv::DecorationBuiltIn) >= 2, "fragment builtins missing");
        require(count_op(words, spv::OpConstantNull) >= 4, "register banks must be initialized");
    }
    SyntheticProgram conditional;
    conditional.program.primary_program_instr_count = 1;
    conditional.code[0] = encode("00111pppstrydecbmmaanoooiwwwwkllffgghhhhjjjjjjqqqqqquuuuuuvvvvvv", { { 'm', 2 }, { 'o', 5 }, { 'h', 15 } });
    compile(conditional, false, "conditional_vector_move", directory);
    SyntheticProgram repeated;
    repeated.program.primary_program_instr_count = 1;
    repeated.code[0] = encode("11010ppp-nssdercbooo00iga0000kttffhhjjjjjjjlllllllmmmmmmmqqqqqqq", { { 'o', 2 }, { 'e', 1 } });
    const auto repeated_words = compile(repeated, false, "integer_repeat", directory);
    require(count_op(repeated_words, spv::OpIMul) == 3, "I32MAD2 must execute count+1 repetitions");
    SyntheticProgram vertex(false);
    compile(vertex, false, "empty_vertex", directory);
}

static void test_interlock_discard(const std::string &directory) {
    for (uint32_t pred : { 0U, 1U }) {
        SyntheticProgram fixture;
        fixture.program.program_flags |= SCE_GXM_PROGRAM_FLAG_FRAGCOLOR_USED | SCE_GXM_PROGRAM_FLAG_DISCARD_USED;
        fixture.program.primary_program_instr_count = 1;
        fixture.code[0] = encode("11111001--11000000000pp0000001101111----------------------------", { { 'p', pred } });
        auto words = compile(fixture, false, "interlock_discard_" + std::to_string(pred), directory, true);
        require(count_op(words, spv::OpKill) == 0, "iOS interlock KILL must be lowered to phase return");
        require(count_op(words, spv::OpBeginInvocationInterlockEXT) == 1, "interlock begin lost");
        require(count_op(words, spv::OpEndInvocationInterlockEXT) == 1, "interlock end lost");
        // Native/non-interlock fragment discard remains unchanged.
        fixture.program.program_flags &= ~SCE_GXM_PROGRAM_FLAG_FRAGCOLOR_USED;
        words = compile(fixture, false, "native_discard_" + std::to_string(pred), directory);
        require(count_op(words, spv::OpKill) != 0, "native discard lost");
    }
}

int main(int argc, char **argv) {
    try {
        require(argc == 2, "pass shader output directory");
        spdlog::set_level(spdlog::level::warn);
        test_raw_vertex_inputs(argv[1]);
        test_uniform_layout();
        test_predicates_and_lod();
        test_branch_analysis();
        test_translation(argv[1]);
        test_interlock_discard(argv[1]);
        test_address_arithmetic(argv[1]);
        test_mapped_buffer_access(argv[1]);
        std::cout << "Shader/GXM regression checks passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
