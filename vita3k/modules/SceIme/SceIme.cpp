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

#include <module/module.h>

#include <ime/functions.h>
#include <ime/guest_event.h>
#include <ime/text.h>
#include <ime/types.h>
#include <kernel/state.h>

#include <mutex>

#include <util/lock_and_find.h>

#ifdef __ANDROID__
#include <ime/keyboard.h>
#endif

#include <util/tracy.h>
TRACY_MODULE_NAME(SceIme);

EXPORT(void, SceImeEventHandler, Ptr<void> handler, Ptr<void> arg, const SceImeEvent *e) {
    TRACY_FUNC(SceImeEventHandler, arg, e);
    Ptr<SceImeEvent> e1 = Ptr<SceImeEvent>(alloc(emuenv.mem, sizeof(SceImeEvent), "ime2"));
    memcpy(e1.get(emuenv.mem), e, sizeof(SceImeEvent));
    auto thread = emuenv.kernel.get_thread(thread_id);
    thread->run_callback(handler.address(), { arg.address(), e1.address() });
    free(emuenv.mem, e1.address());
}

EXPORT(SceInt32, sceImeClose) {
    TRACY_FUNC(sceImeClose);
    std::unique_lock lock(emuenv.ime.mutex);
    LOG_INFO("IME closed by guest: session={}", emuenv.ime.generation);
    ++emuenv.ime.generation;
    emuenv.ime.native_input.reset();
    emuenv.ime.state = false;

    if (emuenv.ime.param.inputTextBuffer.address())
        free(emuenv.mem, emuenv.ime.param.inputTextBuffer.address());
    emuenv.ime.param.inputTextBuffer = Ptr<SceWChar16>();
    lock.unlock();

#ifdef __ANDROID__
    ime::set_keyboard_active(false);
#endif

    return 0;
}

EXPORT(SceInt32, sceImeOpen, SceImeParam *param) {
    TRACY_FUNC(sceImeOpen, param);
    if (!param || !param->inputTextBuffer || !param->handler)
        return RET_ERROR(SCE_IME_ERROR_INVALID_POINTER);
    if (param->maxTextLength > SCE_IME_MAX_TEXT_LENGTH)
        return RET_ERROR(SCE_IME_ERROR_INVALID_PARAM);
    std::unique_lock lock(emuenv.ime.mutex);
    if (emuenv.ime.state)
        return RET_ERROR(SCE_IME_ERROR_ALREADY_OPENED);
    ++emuenv.ime.generation;
    emuenv.ime.native_input.begin();
    emuenv.ime.caps_level = 0;
    emuenv.ime.caretIndex = 0;
    emuenv.ime.edit_text = {};
    emuenv.ime.enter_label.clear();
    emuenv.ime.str.clear();
    emuenv.ime.param = *param;

    switch (emuenv.ime.param.enterLabel) {
    case SCE_IME_ENTER_LABEL_DEFAULT:
        emuenv.ime.enter_label = "Enter";
        break;
    case SCE_IME_ENTER_LABEL_SEND:
        emuenv.ime.enter_label = "Send";
        break;
    case SCE_IME_ENTER_LABEL_SEARCH:
        emuenv.ime.enter_label = "Search";
        break;
    case SCE_IME_ENTER_LABEL_GO:
        emuenv.ime.enter_label = "Go";
        break;
    default: break;
    }

    emuenv.ime.edit_text.str = emuenv.ime.param.inputTextBuffer;
    emuenv.ime.param.inputTextBuffer = Ptr<SceWChar16>(alloc(emuenv.mem, (SCE_IME_MAX_PREEDIT_LENGTH + emuenv.ime.param.maxTextLength + 1) * sizeof(SceWChar16), "ime_str"));
    emuenv.ime.str = emuenv.ime.param.initialText ? reinterpret_cast<char16_t *>(emuenv.ime.param.initialText.get(emuenv.mem)) : u"";
    emuenv.ime.str.resize(ime::text_length(emuenv.ime.str, emuenv.ime.param.maxTextLength));
    if (!emuenv.ime.str.empty())
        emuenv.ime.caretIndex = emuenv.ime.edit_text.caretIndex = emuenv.ime.edit_text.preeditIndex = static_cast<SceUInt32>(emuenv.ime.str.length());
    else
        emuenv.ime.caps_level = 1;

    emuenv.ime.delivered_text = emuenv.ime.str;
    memcpy(emuenv.ime.edit_text.str.get(emuenv.mem), emuenv.ime.str.c_str(),
        (emuenv.ime.str.size() + 1) * sizeof(SceWChar16));
    emuenv.ime.event_id = SCE_IME_EVENT_OPEN;
    emuenv.ime.state = true;
    LOG_INFO("IME open: session={} max_length={} initial_length={} handler=0x{:X}",
        emuenv.ime.generation, emuenv.ime.param.maxTextLength, emuenv.ime.str.size(),
        emuenv.ime.param.handler.address());
    lock.unlock();

#ifdef __ANDROID__
    ime::set_keyboard_active(true);
#endif

    return 0;
}

EXPORT(SceInt32, sceImeSetCaret, const SceImeCaret *caret) {
    TRACY_FUNC(sceImeSetCaret, caret);
    if (!emuenv.ime.state)
        return RET_ERROR(SCE_IME_ERROR_NOT_OPENED);

    Ptr<SceImeEvent> event = Ptr<SceImeEvent>(alloc(emuenv.mem, sizeof(SceImeEvent), "ime_event"));
    SceImeEvent *e = event.get(emuenv.mem);
    *e = {};
    e->id = SCE_IME_EVENT_UPDATE_CARET;
    {
        std::lock_guard lock(emuenv.ime.mutex);
        emuenv.ime.caretIndex = emuenv.ime.edit_text.caretIndex = static_cast<uint32_t>(ime::text_length(emuenv.ime.str, caret->index));
        e->param.caretIndex = emuenv.ime.caretIndex;
    }
    CALL_EXPORT(SceImeEventHandler, emuenv.ime.param.handler, emuenv.ime.param.arg, e);
    free(emuenv.mem, event.address());

    return 0;
}

EXPORT(SceInt32, sceImeSetPreeditGeometry, const SceImePreeditGeometry *preedit) {
    TRACY_FUNC(sceImeSetPreeditGeometry, preedit);
    if (!emuenv.ime.state)
        return RET_ERROR(SCE_IME_ERROR_NOT_OPENED);

    Ptr<SceImeEvent> event = Ptr<SceImeEvent>(alloc(emuenv.mem, sizeof(SceImeEvent), "ime_event"));
    SceImeEvent *e = event.get(emuenv.mem);
    *e = {};
    e->id = SCE_IME_EVENT_CHANGE_SIZE;
    e->param.rect.height = preedit->height;
    e->param.rect.x = preedit->x;
    e->param.rect.y = preedit->y;
    CALL_EXPORT(SceImeEventHandler, emuenv.ime.param.handler, emuenv.ime.param.arg, e);
    free(emuenv.mem, event.address());

    return 0;
}

EXPORT(int, sceImeSetText, const SceWChar16 *text, SceUInt32 length) {
    TRACY_FUNC(sceImeSetText, text, length);
    std::lock_guard lock(emuenv.ime.mutex);
    if (!emuenv.ime.state)
        return RET_ERROR(SCE_IME_ERROR_NOT_OPENED);
    if (!text && length != 0)
        return RET_ERROR(SCE_IME_ERROR_INVALID_POINTER);
    if (length > emuenv.ime.param.maxTextLength)
        return RET_ERROR(SCE_IME_ERROR_INVALID_PARAM);
    emuenv.ime.str = length ? std::u16string(reinterpret_cast<const char16_t *>(text), length) : u"";
    emuenv.ime.caretIndex = emuenv.ime.edit_text.caretIndex = length;
    emuenv.ime.edit_text.preeditIndex = length;
    emuenv.ime.edit_text.preeditLength = 0;
    // The game already owns this text; use it as the baseline for the next
    // native edit rather than reporting a zero/incorrect length change.
    emuenv.ime.delivered_text = emuenv.ime.str;
    emuenv.ime.event_id = SCE_IME_EVENT_UPDATE_TEXT;
    return 0;
}

EXPORT(SceInt32, sceImeUpdate) {
    TRACY_FUNC(sceImeUpdate);
    std::unique_lock lock(emuenv.ime.mutex);
    if (!emuenv.ime.state)
        return RET_ERROR(SCE_IME_ERROR_NOT_OPENED);

    const auto next_event = ime::take_guest_event(emuenv.ime, emuenv.mem);
    if (!next_event)
        return 0;

    Ptr<SceImeEvent> event = Ptr<SceImeEvent>(alloc(emuenv.mem, sizeof(SceImeEvent), "ime_event"));
    SceImeEvent *e = event.get(emuenv.mem);
    *e = *next_event;
    const auto generation = emuenv.ime.generation;
    const auto handler = emuenv.ime.param.handler;
    const auto arg = emuenv.ime.param.arg;
    LOG_INFO("IME dispatch: session={} event={} length={} edit_index={} length_change={}",
        generation, e->id, emuenv.ime.str.size(),
        e->id == SCE_IME_EVENT_UPDATE_TEXT ? e->param.text.editIndex : 0,
        e->id == SCE_IME_EVENT_UPDATE_TEXT ? e->param.text.editLengthChange : 0);
    lock.unlock(); // Guest callbacks can call sceImeSetText/Close again.
    CALL_EXPORT(SceImeEventHandler, handler, arg, e);
    lock.lock();
    if (emuenv.ime.generation == generation)
        emuenv.ime.native_input.callback_completed(e->id);
    LOG_INFO("IME callback returned: session={} event={} guest_open={} current_session={}",
        generation, e->id, emuenv.ime.state, emuenv.ime.generation);
    lock.unlock();
    free(emuenv.mem, event.address());

    return 0;
}
