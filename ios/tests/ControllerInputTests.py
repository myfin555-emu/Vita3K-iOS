#!/usr/bin/env python3
"""Exercise the production controller mapping with stale SDL device state."""
import pathlib
import subprocess
import sys
import tempfile

repo = pathlib.Path(__file__).resolve().parents[2]
source = (repo / 'vita3k/ctrl/src/ctrl.cpp').read_text()

def function(name):
    start = source.index('static ', source.index(name) - 32)
    # Find the actual signature rather than a call in a comment.
    start = source.rfind('static ', 0, source.index(name))
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

preamble = r'''
#include <ctrl/virtual_pad.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdlib>
using SDL_GamepadButton = int;
using SDL_GamepadAxis = int;
struct SDL_Gamepad { uint32_t id; };
uint32_t SDL_GetGamepadID(SDL_Gamepad *pad) { return pad->id; }
bool SDL_GetGamepadButton(SDL_Gamepad *, int index) { return index == 2; }
int16_t SDL_GetGamepadAxis(SDL_Gamepad *, int index) { return index == 1 ? INT16_MIN : 0; }
struct Binding { int controller; uint32_t button; };
struct EmuEnvState {
    struct {
        std::array<int, 6> controller_axis_binds{0, 1, 2, 3, 4, 5};
        float controller_analog_multiplier = 1;
    } cfg;
};
std::array<Binding, 3> get_controller_bindings(EmuEnvState &) { return {{{0, 1}, {1, 2}, {2, 4}}}; }
auto get_controller_bindings_ext(EmuEnvState &emu) { return get_controller_bindings(emu); }
constexpr uint32_t SCE_CTRL_L2 = 8, SCE_CTRL_R2 = 16;
void require(bool value) { if (!value) std::abort(); }
'''
main = r'''
int main() {
    EmuEnvState emu;
    SDL_Gamepad virtual_pad{42}, physical_pad{43};
    ctrl::virtual_pad.attach(42);
    ctrl::virtual_pad.axis(1, INT16_MAX); // opposite to stale SDL cache
    ctrl::virtual_pad.button(0, true);
    ctrl::virtual_pad.button(1, true);
    uint32_t buttons = 0;
    float axes[4]{};
    apply_controller(emu, &buttons, axes, &virtual_pad, false);
#ifdef VITA3K_PLATFORM_IOS
    require(buttons == 3 && axes[1] == 1.0f);
#else
    require(buttons == 4 && axes[1] == -1.0f);
#endif
    buttons = 0;
    std::fill_n(axes, 4, 0);
    apply_controller(emu, &buttons, axes, &physical_pad, false);
    require(buttons == 4 && axes[1] == -1.0f);
#ifdef VITA3K_PLATFORM_IOS
    ctrl::virtual_pad.axis(4, INT16_MAX);
    buttons = 0;
    std::fill_n(axes, 4, 0);
    apply_controller(emu, &buttons, axes, &virtual_pad, true);
    require(buttons == 11); // both face buttons and L2
    std::swap(emu.cfg.controller_axis_binds[0], emu.cfg.controller_axis_binds[1]);
    buttons = 0;
    std::fill_n(axes, 4, 0);
    apply_controller(emu, &buttons, axes, &virtual_pad, false);
    require(axes[0] > 0.99f && std::abs(axes[1]) < 0.001f);
    ctrl::virtual_pad.release_all();
    buttons = 0;
    std::fill_n(axes, 4, 0);
    apply_controller(emu, &buttons, axes, &virtual_pad, true);
    require(buttons == 0 && std::abs(axes[0]) < 0.001f && std::abs(axes[1]) < 0.001f);
#endif
}
'''
functions = '\n'.join(function(n) for n in ['axis_to_axis(', 'map_circular_stick_to_vita_axes(', 'apply_controller('])
with tempfile.TemporaryDirectory() as tmp:
    cpp = pathlib.Path(tmp) / 'input.cpp'
    cpp.write_text(preamble + functions + main)
    for ios in (False, True):
        exe = pathlib.Path(tmp) / ('ios' if ios else 'desktop')
        command = [sys.argv[1], '-std=c++20', '-pthread', '-I' + str(repo / 'vita3k/ctrl/include'), str(cpp), '-o', str(exe)]
        if ios:
            command.append('-DVITA3K_PLATFORM_IOS=1')
        subprocess.run(command, check=True)
        subprocess.run([str(exe)], check=True)
print('Production controller mapping passed for iOS and desktop')
