"""Compile the production native settings snapshot against distinct saved/live settings."""
import pathlib
import re
import subprocess
import sys
import tempfile
repo = pathlib.Path(__file__).resolve().parents[2]
path = 'ios/src/UpstreamMain.cpp'
source = (subprocess.check_output(['git', 'show', 'HEAD:' + path], cwd=repo, text=True)
          if '--baseline' in sys.argv else (repo / path).read_text())
start = source.index('Vita3KIOSSettings native_settings(')
end = source.index('\nstruct ImportJob', start)
code = r'''
#include <vita3k_ios/NativeFrontend.h>
#include <cassert>
#include <utility>
struct Settings : Vita3KIOSSettings {
    bool disable_surface_sync = true;
    std::string memory_mapping = "disabled";
};
struct Config : Settings {
    Settings current_config;
    std::vector<short> controller_binds;
    int ios_jit_threads = 3, ios_emulator_ram_mb = 512, ios_jit_cache_mb = 16;
};
struct EmuEnvState { Config cfg; std::string vita_fs_path; };
namespace app {
struct Firmware { bool font_package = true, main_firmware = true, preinstalled_package = true; };
Firmware get_firmware_state(EmuEnvState &) {return {};}
}
namespace config {
std::vector<std::pair<std::string, bool>> get_modules_list(const std::string &, const std::vector<std::string> &) {return {};}
}
std::string firmware_version_display(EmuEnvState &) { return "3.74"; }
int face_button_slot_for_physical(short n) { return n; }
enum {SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH};
'''
code += source[start:end]
app = (repo / 'vita3k/app/src/app_init.cpp').read_text()
match = re.search(r'r\.set_surface_sync_state\(cc\.disable_surface_sync\);', app)
assert match, 'expected independent surface-sync apply in app_init.cpp'
code += (
    'void apply_sync(const Config &cc, Config &r) {\n'
    '    r.disable_surface_sync = cc.disable_surface_sync;\n'
    '}\n'
)
assignments = [
        assignment
        for assignment in re.findall(
            r'(?:current|desired)\.disable_surface_sync = [^;]+;', source
        )
        if assignment != 'current.disable_surface_sync = false;'
    ]
assert len(assignments) == 3, assignments
for i, assignment in enumerate(assignments):
    code += f'bool save_sync_{i}(const Vita3KIOSSettings &settings) {{ Config current, desired; ' + assignment + ' return ' + assignment.split(' = ')[0] + '; }\n'
code += r'''
int main() {
    EmuEnvState env;
    env.cfg.resolution_multiplier = 1.f;
    env.cfg.current_config.resolution_multiplier = .75f;
    env.cfg.fps_hack = true;
    env.cfg.turbo_mode = true;
    env.cfg.current_config.fps_hack = false;
    env.cfg.high_accuracy = true;
    env.cfg.current_config.high_accuracy = false;
    env.cfg.lle_modules = {"saved"};
    env.cfg.current_config.lle_modules = {"game-override"};
    // High accuracy must not force surface sync on; honour disable_surface_sync.
    env.cfg.disable_surface_sync = true;
    auto saved = native_settings(env);
    assert(saved.fps_hack && saved.turbo_mode);
    assert(saved.resolution_multiplier == 1.f);
    assert(saved.high_accuracy);
    assert(!saved.surface_sync); // disable_surface_sync=true → surface_sync off
    assert(saved.lle_modules == std::vector<std::string>{"saved"});
    Config applied;
    apply_sync(env.cfg, applied);
    assert(applied.disable_surface_sync);

    // Saving surface_sync=false writes disable_surface_sync=true even if high_accuracy stays on.
    saved.surface_sync = false;
    assert(save_sync_0(saved) && save_sync_1(saved) && save_sync_2(saved));
    // Saving surface_sync=true writes disable_surface_sync=false.
    saved.surface_sync = true;
    assert(!save_sync_0(saved) && !save_sync_1(saved) && !save_sync_2(saved));

    env.cfg.disable_surface_sync = false;
    apply_sync(env.cfg, applied);
    assert(!applied.disable_surface_sync);

    // Saving an unrelated volume edit must not erase the pending resolution.
    saved.audio_volume = 75;
    env.cfg.resolution_multiplier = saved.resolution_multiplier;
    assert(env.cfg.resolution_multiplier == 1.f);
    assert(env.cfg.current_config.resolution_multiplier == .75f);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    path = pathlib.Path(tmp)
    (path / 'test.cpp').write_text(code)
    subprocess.run([sys.argv[1], '-std=c++20', '-UNDEBUG', '-DVITA3K_PLATFORM_IOS', '-I' + str(repo / 'ios/include'), str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('Production settings snapshot checks passed')
