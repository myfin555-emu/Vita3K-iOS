#pragma once

#include <ime/state.h>
#include <ime/text.h>

#include <cstring>
#include <optional>

namespace ime {

// The caller holds Ime::mutex. This is the actual buffer/payload boundary used
// by sceImeUpdate before releasing the lock and entering the guest callback.
inline std::optional<SceImeEvent> take_guest_event(Ime &state, MemState &mem) {
    const auto id = state.native_input.take_event(state.event_id);
    if (!id)
        return std::nullopt;

    SceImeEvent event{};
    event.id = *id;
    if (*id == SCE_IME_EVENT_UPDATE_TEXT) {
        const auto edit = describe_edit(state.delivered_text, state.str);
        state.edit_text.editIndex = static_cast<SceUInt32>(edit.index);
        state.edit_text.editLengthChange = edit.length_change;
        event.param.text = state.edit_text;
        state.delivered_text = state.str;
    } else if (*id == SCE_IME_EVENT_UPDATE_CARET) {
        event.param.caretIndex = state.caretIndex;
    } else if (*id == SCE_IME_EVENT_PRESS_ENTER || *id == SCE_IME_EVENT_PRESS_CLOSE) {
        event.param.text = state.edit_text;
    }
    // OPEN has an empty rectangle: no emulated keyboard rectangle is reserved
    // in the guest framebuffer. The host provides the native input surface.
    std::memcpy(state.edit_text.str.get(mem), state.str.c_str(),
        (state.str.size() + 1) * sizeof(SceWChar16));
    return event;
}

} // namespace ime
