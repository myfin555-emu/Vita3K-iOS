#pragma once

#include <ime/event.h>

#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

namespace ime {

// Guarded by Ime::mutex. Guest callback completion is not a request to reopen
// the native editor: reopening requires a new session or explicit user edit.
class NativeInputState {
public:
    void begin() {
        reset();
        open_pending_ = true;
    }

    bool submit(ImeEvent action) {
        if (dismissed_ || (action != SCE_IME_EVENT_PRESS_ENTER && action != SCE_IME_EVENT_PRESS_CLOSE))
            return false;
        dismissed_ = true;
        pending_action_ = action;
        drain_edit_ = true;
        return true;
    }

    std::optional<uint32_t> take_event(uint32_t &edit_event) {
        // OPEN is a real guest callback, distinct from an empty event queue.
        // It must survive edits arriving before the first sceImeUpdate call.
        if (std::exchange(open_pending_, false))
            return SCE_IME_EVENT_OPEN;
        // Drain the final text/caret update before Enter/Close. A guest callback
        // may echo sceImeSetText; that must not starve the queued terminal event.
        if (pending_action_ != SCE_IME_EVENT_OPEN && (!drain_edit_ || edit_event == SCE_IME_EVENT_OPEN)) {
            action_in_flight_ = true;
            return std::exchange(pending_action_, SCE_IME_EVENT_OPEN);
        }
        drain_edit_ = false;
        if (edit_event == SCE_IME_EVENT_OPEN)
            return std::nullopt;
        return std::exchange(edit_event, SCE_IME_EVENT_OPEN);
    }

    bool dismissed() const { return dismissed_; }
    bool pending() const { return open_pending_ || action_in_flight_ || pending_action_ != SCE_IME_EVENT_OPEN; }

    void callback_completed(uint32_t event) {
        if (event == SCE_IME_EVENT_PRESS_ENTER || event == SCE_IME_EVENT_PRESS_CLOSE)
            action_in_flight_ = false;
    }

    // Games can keep the same IME session open for validation or chat. Editing
    // can resume only through an explicit user action, never an idle queue.
    bool resume() {
        if (!dismissed_ || pending())
            return false;
        dismissed_ = false;
        return true;
    }

    void reset() { *this = NativeInputState{}; }

private:
    bool action_in_flight_ = false;
    bool open_pending_ = false;
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
