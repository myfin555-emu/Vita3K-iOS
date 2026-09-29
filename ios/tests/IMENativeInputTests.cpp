#include <ime/native_input.h>

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {
constexpr uint32_t no_event = UINT32_MAX;
void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

// Model the shared pending edit slot and callback boundary used by sceImeUpdate.
// UIKit requests submission once; no further UI polling is needed for delivery.
struct Guest {
    ime::NativeInputState input;
    uint32_t edit = SCE_IME_EVENT_OPEN;
    std::u16string text;
    std::u16string guest_buffer;
    std::vector<uint32_t> events;

    uint32_t update() {
        const auto event = input.take_event(edit);
        if (event) {
            guest_buffer = text;
            events.push_back(*event);
            input.callback_completed(*event);
        }
        return event.value_or(no_event);
    }
};
} // namespace

int main() {
    Guest guest;
    guest.input.begin();
    require(guest.update() == SCE_IME_EVENT_OPEN, "OPEN must be delivered as a real guest event");
    require(guest.update() == no_event, "OPEN is not the idle queue sentinel");
    guest.events.clear();
    guest.text = u"ชื่อ\U0001F600";
    guest.edit = SCE_IME_EVENT_UPDATE_TEXT;
    require(guest.input.submit(SCE_IME_EVENT_PRESS_ENTER), "first confirmation accepted");
    require(guest.input.dismissed(), "hide the editor immediately");
    require(!guest.input.submit(SCE_IME_EVENT_PRESS_ENTER), "duplicate Return must not submit twice");
    require(!guest.input.submit(SCE_IME_EVENT_PRESS_CLOSE), "Cancel cannot replace a queued confirmation");
    require(guest.update() == SCE_IME_EVENT_UPDATE_TEXT, "final edit precedes confirmation");
    require(guest.guest_buffer == guest.text, "guest receives final Unicode text");
    require(guest.input.dismissed(), "empty edit slot while callback runs must not reopen keyboard");

    // A callback which echoes SetText cannot postpone Enter forever.
    guest.edit = SCE_IME_EVENT_UPDATE_TEXT;
    require(guest.update() == SCE_IME_EVENT_PRESS_ENTER, "confirmation survives a callback text echo");
    require(guest.input.dismissed(), "callback consumption does not reopen keyboard");
    require(guest.update() == SCE_IME_EVENT_UPDATE_TEXT, "preserve subsequent guest edits");
    for (int frame = 0; frame < 120; ++frame) {
        require(guest.update() == no_event, "no duplicate terminal callbacks");
        require(guest.input.dismissed(), "idle game frames do not reopen a submitted session");
    }
    require(guest.events == std::vector<uint32_t>{ SCE_IME_EVENT_UPDATE_TEXT, SCE_IME_EVENT_PRESS_ENTER, SCE_IME_EVENT_UPDATE_TEXT },
        "expected callback sequence");

    // Opening another field starts an independently editable session.
    guest.input.reset();
    require(!guest.input.dismissed(), "new session opens normally");
    require(guest.input.submit(SCE_IME_EVENT_PRESS_ENTER), "unchanged text can be confirmed");
    require(guest.update() == SCE_IME_EVENT_PRESS_ENTER, "unchanged text confirms without an edit");

    guest.input.reset();
    guest.edit = SCE_IME_EVENT_UPDATE_CARET;
    require(guest.input.submit(SCE_IME_EVENT_PRESS_CLOSE), "cancel accepted");
    require(guest.update() == SCE_IME_EVENT_UPDATE_CARET, "drain caret before cancel");
    require(guest.update() == SCE_IME_EVENT_PRESS_CLOSE, "cancel reaches guest");
    require(guest.input.dismissed(), "cancel remains dismissed");

    // Close/open (or title teardown) discards a terminal event from the old field.
    guest.input.reset();
    guest.input.submit(SCE_IME_EVENT_PRESS_ENTER);
    guest.input.reset();
    require(guest.update() == no_event, "old confirmation cannot leak into new session");
    require(!guest.input.submit(SCE_IME_EVENT_UPDATE_TEXT), "only terminal actions can dismiss");
    require(!guest.input.dismissed(), "invalid action leaves editing enabled");
    guest.edit = SCE_IME_EVENT_UPDATE_TEXT;
    require(guest.update() == SCE_IME_EVENT_UPDATE_TEXT, "ordinary editing still works");

    for (const auto key : { u"\n", u"\r", u"\r\n" }) {
        require(ime::is_submit_key(key, false), "Return and Enter confirm");
        require(!ime::is_submit_key(key, true), "composition candidate selection stays in UIKit");
    }
    for (const auto text : { u"", u"name", u"first\nlast", u"ชื่อ", u"\U0001F600" })
        require(!ime::is_submit_key(text, false), "typing, paste and deletion are not confirmations");
    guest.input.begin();
    guest.edit = SCE_IME_EVENT_UPDATE_TEXT;
    guest.input.submit(SCE_IME_EVENT_PRESS_ENTER);
    require(!guest.input.resume(), "cannot edit over queued confirmation");
    require(guest.update() == SCE_IME_EVENT_OPEN, "early typing cannot overwrite OPEN");
    require(guest.update() == SCE_IME_EVENT_UPDATE_TEXT, "early edit follows OPEN");
    require(guest.input.take_event(guest.edit) == SCE_IME_EVENT_PRESS_ENTER, "terminal selected");
    require(!guest.input.resume(), "cannot edit while callback is still executing");
    guest.input.callback_completed(SCE_IME_EVENT_PRESS_ENTER);
    require(guest.input.dismissed(), "callback return alone does not reopen");
    require(guest.input.resume(), "explicit Edit can resume a session the game keeps open");
    require(!guest.input.dismissed(), "explicit edit restores input");
    require(guest.update() == no_event, "resume does not fabricate another OPEN");
    return EXIT_SUCCESS;
}
