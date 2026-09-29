// Vita3K emulator project
// SPDX-License-Identifier: GPL-2.0-or-later
#include <shader/interlock_discard.h>

#include <fstream>
#include <string>
#include <vector>

int main(int argc, char **argv) {
    if (argc != 3)
        return 1;
    const bool deferred = std::string(argv[1]) == "deferred";
    spv::SpvBuildLogger logger;
    spv::Builder b(0x00010300, 0, &logger);
    b.addCapability(spv::CapabilityShader);
    b.addCapability(spv::CapabilityFragmentShaderSampleInterlockEXT);
    b.addExtension("SPV_EXT_fragment_shader_interlock");
    b.setMemoryModel(spv::AddressingModelLogical, spv::MemoryModelGLSL450);
    auto *main = b.makeEntryPoint("main");
    auto *entry = b.addEntryPoint(spv::ExecutionModelFragment, main, "main");
    b.addExecutionMode(main, spv::ExecutionModeOriginUpperLeft);
    b.addExecutionMode(main, spv::ExecutionModeEarlyFragmentTests);
    b.addExecutionMode(main, spv::ExecutionModeSampleInterlockOrderedEXT);
    const auto discarded = deferred ? shader::create_interlock_discard_flag(b) : spv::NoResult;
    const auto i32 = b.makeIntType(32);
    const auto f32 = b.makeFloatType(32);
    auto image_type = b.makeImageType(f32, spv::Dim2D, false, false, false, 2, spv::ImageFormatRgba8);
    auto image = b.createVariable(spv::NoPrecision, spv::StorageClassUniformConstant, image_type, "framebuffer");
    b.addDecoration(image, spv::DecorationDescriptorSet, 0);
    b.addDecoration(image, spv::DecorationBinding, 0);
    // Location0 controls an independently predicated kill in each guest phase.
    auto input = b.createVariable(spv::NoPrecision, spv::StorageClassInput, f32, "kill_phase");
    b.addDecoration(input, spv::DecorationLocation, 0);
    entry->addIdOperand(input);
    auto coord = b.makeCompositeConstant(b.makeVectorType(i32, 2), { b.makeIntConstant(0), b.makeIntConstant(0) });
    auto color = b.makeCompositeConstant(b.makeVectorType(f32, 4),
        { b.makeFloatConstant(1), b.makeFloatConstant(1), b.makeFloatConstant(1), b.makeFloatConstant(1) });
    auto *main_block = b.getBuildPoint();
    auto make_phase = [&](const char *name, int number, bool store) {
        spv::Block *body;
        auto *fn = b.makeFunctionEntry(spv::NoPrecision, b.makeVoidType(), name, {}, {}, {}, &body);
        if (number) {
            auto cond = b.createBinOp(spv::OpFOrdEqual, b.makeBoolType(),
                b.createLoad(input, spv::NoPrecision), b.makeFloatConstant(number));
            spv::Builder::If branch(cond, spv::SelectionControlMaskNone, b);
            shader::emit_fragment_discard(b, discarded);
            branch.makeEndIf();
        }
        if (store)
            b.createNoResultOp(spv::OpImageWrite, { b.createLoad(image, spv::NoPrecision), coord, color });
        b.leaveFunction();
        b.setBuildPoint(main_block);
        return fn;
    };
    auto *primary = make_phase("primary_program", 1, false);
    auto *secondary = make_phase("secondary_program", 2, true);
    auto *finalize = make_phase("frag_finalize", 0, true);
    b.createNoResultOp(spv::OpBeginInvocationInterlockEXT);
    shader::call_fragment_phase(b, primary, discarded);
    shader::call_fragment_phase(b, secondary, discarded);
    shader::call_fragment_phase(b, finalize, discarded);
    b.createNoResultOp(spv::OpEndInvocationInterlockEXT);
    b.leaveFunction();
    std::vector<unsigned> words;
    b.dump(words);
    std::ofstream out(argv[2], std::ios::binary);
    out.write(reinterpret_cast<const char *>(words.data()), words.size() * sizeof(unsigned));
    return out.good() ? 0 : 1;
}
