#include <ime/guest_event.h>

#include <array>
#include <cstddef>
#include <cstdlib>
#include <iostream>

// Guest ABI sizes/offsets from VitaSDK psp2/libime.h (32-bit guest pointers).
static_assert(sizeof(SceImeParam) == 0x40);
static_assert(sizeof(SceImeEditText) == 0x18);
static_assert(sizeof(SceImeEvent) == 0x2c);
static_assert(offsetof(SceImeEvent, param) == 4);
static_assert(offsetof(SceImeEditText, str) == 12);

namespace {
void require(bool value, const char *message) {
    if (!value) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}
} // namespace

int main() {
    // Use the real guest Ptr/IME structures and the production event writer.
    // Guard bytes detect overruns of a guest-owned UTF-16 buffer.
    MemState mem;
    mem.memory = Memory(new uint8_t[256], [](uint8_t *p) { delete[] p; });
    std::memset(mem.memory.get(), 0xa5, 256);
    Ime state;
    state.edit_text.str = Ptr<SceWChar16>(64);
    state.native_input.begin();
    state.str = u"ชื่อ\U0001F600";
    state.caretIndex = state.edit_text.caretIndex = state.edit_text.preeditIndex = state.str.size();
    state.event_id = SCE_IME_EVENT_UPDATE_TEXT;
    state.native_input.submit(SCE_IME_EVENT_PRESS_ENTER);

    const auto opened = ime::take_guest_event(state, mem);
    require(opened && opened->id == SCE_IME_EVENT_OPEN, "deliver OPEN before early typing");
    const auto edited = ime::take_guest_event(state, mem);
    require(edited && edited->id == SCE_IME_EVENT_UPDATE_TEXT, "deliver final text");
    require(edited->param.text.editLengthChange == static_cast<int>(state.str.size()), "report inserted UTF-16 code units");
    require(edited->param.text.editIndex == 0, "initial insertion starts at zero");
    require(edited->param.text.str.address() == 64, "payload points to the guest-owned buffer");
    require(std::memcmp(edited->param.text.str.get(mem), state.str.c_str(), (state.str.size() + 1) * 2) == 0,
        "guest buffer contains final text and terminator before Enter");
    require(mem.memory[63] == 0xa5 && mem.memory[64 + (state.str.size() + 1) * 2] == 0xa5, "guest buffer boundaries preserved");
    // Read the callback as raw guest words, not only through host fields.
    std::array<uint32_t, 11> words{};
    std::memcpy(words.data(), &*edited, sizeof(*edited));
    require(words[0] == 1 && words[4] == 64 && words[6] == state.str.size(), "serialized callback ABI matches VitaSDK");

    const auto entered = ime::take_guest_event(state, mem);
    require(entered && entered->id == SCE_IME_EVENT_PRESS_ENTER, "Enter follows committed text");
    state.native_input.callback_completed(entered->id);
    require(!ime::take_guest_event(state, mem), "event consumed only once");
    require(state.native_input.resume(), "explicit editing is available after callback completion");

    state.delivered_text = u"ABCD";
    state.str = u"AD";
    state.event_id = SCE_IME_EVENT_UPDATE_TEXT;
    const auto removed = ime::take_guest_event(state, mem);
    require(removed->param.text.editIndex == 1 && removed->param.text.editLengthChange == -2, "coalesced selection deletion reports negative delta");
    state.str = u"AXYZD";
    state.event_id = SCE_IME_EVENT_UPDATE_TEXT;
    const auto pasted = ime::take_guest_event(state, mem);
    require(pasted->param.text.editIndex == 1 && pasted->param.text.editLengthChange == 3, "paste reports net inserted length");
    state.str = u"AXYQD";
    state.event_id = SCE_IME_EVENT_UPDATE_TEXT;
    const auto replaced = ime::take_guest_event(state, mem);
    require(replaced->param.text.editIndex == 3 && replaced->param.text.editLengthChange == 0, "same-length replacement retains changed index");

    state.delivered_text = u"\U0001F600";
    state.str = u"\U0001F601";
    state.event_id = SCE_IME_EVENT_UPDATE_TEXT;
    const auto emoji = ime::take_guest_event(state, mem);
    require(emoji->param.text.editIndex == 0, "edit index never splits a surrogate pair");
    state.event_id = SCE_IME_EVENT_UPDATE_CARET;
    state.caretIndex = 1;
    const auto caret = ime::take_guest_event(state, mem);
    require(caret->param.caretIndex == 1, "caret union is not overwritten by text payload");

    state.deinit();
    require(!ime::take_guest_event(state, mem), "teardown discards all pending events without writing a freed buffer");
    return EXIT_SUCCESS;
}
