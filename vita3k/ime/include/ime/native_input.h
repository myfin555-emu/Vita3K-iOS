#pragma once

#include <ime/event.h>

#include <cstdint>
#include <string_view>
#include <utility>

namespace ime {

// Guarded by Ime::mutex. Guest callback completion is not a request to reopen
// the native editor: only an explicit new IME session resets dismissal.
class NativeInputState {
public:
    bool submit(ImeEvent action) {
        if (dismissed_ || (action != SCE_IME_EVENT_PRESS_ENTER && action != SCE_IME_EVENT_PRESS_CLOSE))
            return false;
        dismissed_ = true;
        pending_action_ = action;
        drain_edit_ = true;
        return true;
    }

    uint32_t take_event(uint32_t &edit_event) {
        // Drain the final text/caret update before Enter/Close. A guest callback
        // may echo sceImeSetText; that must not starve the queued terminal event.
        if (pending_action_ != SCE_IME_EVENT_OPEN && (!drain_edit_ || edit_event == SCE_IME_EVENT_OPEN))
            return std::exchange(pending_action_, SCE_IME_EVENT_OPEN);
        drain_edit_ = false;
        return std::exchange(edit_event, SCE_IME_EVENT_OPEN);
    }

    bool dismissed() const { return dismissed_; }

    void reset() { *this = NativeInputState{}; }

private:
    bool dismissed_ = false;
    bool drain_edit_ = false;
    ImeEvent pending_action_ = SCE_IME_EVENT_OPEN;
};

// The native Return/Enter key confirms, including for multiline requests.
// Choosing a Japanese/Chinese composition candidate is not a submission.
inline bool is_submit_key(std::u16string_view text, bool composing) {
    return !composing && (text == u"\n" || text == u"\r" || text == u"\r\n");
}

} // namespace ime
