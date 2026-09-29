// Vita3K emulator project
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <SPIRV/SpvBuilder.h>

namespace shader {

// Interlock shaders have no color attachment output and use EarlyFragmentTests.
// Discard therefore means stop the guest program and suppress subsequent stores;
// depth/stencil tests have already run. Returning from the phase and guarding
// later phases also keeps EndInvocationInterlock reachable. Avoiding OpKill in
// this path prevents MoltenVK from injecting unsupported helper-thread queries
// for discarded storage-image writes on older iOS GPUs.
inline spv::Id create_interlock_discard_flag(spv::Builder &b) {
    return b.createVariable(spv::NoPrecision, spv::StorageClassPrivate,
        b.makeBoolType(), "fragment_discarded", b.makeBoolConstant(false));
}

inline void emit_fragment_discard(spv::Builder &b, spv::Id discarded) {
    if (discarded) {
        b.createStore(b.makeBoolConstant(true), discarded);
        b.makeReturn(false);
    } else {
        b.makeStatementTerminator(spv::OpKill, "kill");
    }
}

inline void call_fragment_phase(spv::Builder &b, spv::Function *phase, spv::Id discarded) {
    if (discarded) {
        const auto live = b.createUnaryOp(spv::OpLogicalNot, b.makeBoolType(), b.createLoad(discarded, spv::NoPrecision));
        spv::Builder::If if_live(live, spv::SelectionControlMaskNone, b);
        b.createFunctionCall(phase, {});
        if_live.makeEndIf();
    } else {
        b.createFunctionCall(phase, {});
    }
}

} // namespace shader
