"""Exercise native speed-setting saves and the real guest vblank wait policy.

Optional third argument enables the production YAML serializer roundtrip using
an already-built pinned yaml-cpp static library.
"""
import pathlib
import re
import subprocess
import sys
import tempfile

repo = pathlib.Path(__file__).resolve().parents[2]
source = (repo / 'ios/src/UpstreamMain.cpp').read_text()
def function(text, signature):
    start = text.index(signature)
    brace = text.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

# Cover boundaries that require Apple's ObjC/Swift compiler on device.
bridge = (repo / 'ios/src/TsubomiBridge.mm').read_text()
model = (repo / 'ios/src/Swift/SettingsModel.swift').read_text()
for prop, field in [('fpsHack', 'fps_hack'), ('turboMode', 'turbo_mode')]:
    assert f'_{prop} = core.{field};' in bridge
    assert f'core.{field} = self.{prop};' in bridge
    assert f'{prop} = settings.{prop}' in model
    assert f'settings.{prop} = {prop}' in model
assert 'initWithCoreSettings:core' in function(bridge, '- (id)copyWithZone:')
frontend = (repo / 'ios/src/NativeFrontend.mm').read_text()
assert 'settings.fps_hack = [stored[@"fpsHack"] boolValue]' in frontend
assert '@"fpsHack": @(settings.fps_hack)' in frontend
assert 'current.fps_hack = false' not in source
assert 'display.fps_hack = false' not in source
assert 'emuenv->display.fps_hack = emuenv->cfg.current_config.fps_hack;' in source
config_source = (repo / 'vita3k/config/src/config.cpp').read_text()
config_keys = (repo / 'vita3k/config/include/config/config.h').read_text()
key_defs = re.findall(r'code\(bool, "(?:fps-hack|turbo-mode)", false, (?:fps_hack|turbo_mode)\)', config_keys)
assert len(key_defs) == 2
code = r'''
#include <vita3k_ios/NativeFrontend.h>
#include <algorithm>
#include <atomic>
#include <cassert>
#include <memory>
#include <filesystem>
#include <fstream>
#include <string>
#define LOG_INFO(...) ((void)0)
struct Settings : Vita3KIOSSettings {
    bool disable_surface_sync = false;
    std::string memory_mapping = "disabled", audio_backend = "SDL";
};
struct Config : Settings {
    using CurrentConfig = Settings;
    Settings current_config;
    std::vector<short> controller_binds{0,1,2,3};
    int ios_jit_threads=3, ios_emulator_ram_mb=640, ios_jit_cache_mb=16;
    int check_for_updates_mode=0;
};
using SceUID=int;
struct Thread { uint64_t last_vblank_waited=0; };
struct Display {
    std::atomic<bool> fps_hack{false}, abort{false};
    std::atomic<int> fps_limit{60};
    uint64_t last_setframe_vblank_count=10, vblank_count=10, waited=0;
};
struct Kernel {
    std::shared_ptr<Thread> thread=std::make_shared<Thread>();
    const std::shared_ptr<Thread> &get_thread(int) {return thread;}
};
struct EmuEnvState {
    Config cfg;
    Display display;
    Kernel kernel;
    struct { void set_global_volume(float) {} } audio;
};
namespace app {
struct Result { bool runtime_settings_applied=true; std::vector<int> restart_required_settings; };
Result commit_settings(EmuEnvState &env, const Config &desired) {
    env.cfg=desired;
'''
settings_cpp = (repo / 'vita3k/config/src/settings.cpp').read_text()
code += '    const auto &current=desired.current_config; auto &cfg=env.cfg;\n'
code += re.search(r'    cfg.fps_hack = current.fps_hack;', settings_cpp).group() + '\n'
app_cpp = (repo / 'vita3k/app/src/app_init.cpp').read_text()
code += '    auto &emuenv=env; const auto &cc=env.cfg.current_config;\n'
code += re.search(r'    emuenv.display.fps_hack = cc.fps_hack;', app_cpp).group()
code += r'''
    return {};
}
}
std::string ios_memory_mapping_for(const Vita3KIOSSettings &) { return "disabled"; }
short face_button_physical_for_slot(int slot) {return slot;}
std::string restart_setting_name(int) {return "unused";}
void vita3k_ios_report_settings_result(const std::vector<std::string> &) {}
enum {SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH};
'''
code += function(source, 'void apply_native_settings(')
code += function(source, 'void apply_game_session_settings(')
code += r'''
constexpr int SCE_DISPLAY_ERROR_NO_PIXEL_DATA=-1, SCE_DISPLAY_ERROR_OK=0;
void wait_vblank(Display &display, Kernel &, const std::shared_ptr<Thread> &, uint64_t target, bool) { display.waited=target; }
'''
code += function((repo / 'vita3k/modules/SceDisplay/SceDisplay.cpp').read_text(), 'static int display_wait(')
yaml_enabled = len(sys.argv) > 2
if yaml_enabled:
    code += '\n#include <yaml-cpp/yaml.h>\nnamespace fs { using namespace std::filesystem; using std::ifstream; using std::ofstream; }\n'
    code += '#define CONFIG_LIST(code) ' + ' \\\n'.join(key_defs) + '\n'
    code += 'enum ExitCode {Success, InvalidApplicationPath}; enum {UPDATE_STARTUP_PROMPT, UPDATE_STARTUP_OFF};\n'
    code += 'fs::path check_path(const fs::path &path) {return path;}\nvoid sync_update_preferences(Config &) {}\n'
    code += function(config_source, 'static YAML::Node get(const Config &self) {')
    code += function(config_source, 'static void update_members(')
    code += function(config_source, 'ExitCode serialize_config(')
code += r'''
int main(int argc, char **argv) {
    EmuEnvState env;
    Vita3KIOSSettings settings;
    for (bool enabled : {true, false, true, false}) {
        settings.fps_hack=enabled;
        settings.turbo_mode=enabled;
        apply_native_settings(env, settings);
        assert(env.cfg.fps_hack==enabled && env.cfg.current_config.fps_hack==enabled);
        assert(env.display.fps_hack==enabled && env.cfg.turbo_mode==enabled);
'''
if yaml_enabled:
    code += r'''
        const fs::path file=fs::path(argv[1])/"config.yml";
        assert(serialize_config(env.cfg,file)==Success);
        YAML::Node saved=YAML::LoadFile(file.string());
        assert(saved["fps-hack"].as<bool>()==enabled && saved["turbo-mode"].as<bool>()==enabled);
        Config reopened;
        update_members(reopened,saved);
        assert(reopened.fps_hack==enabled && reopened.turbo_mode==enabled);
'''
code += r'''
        for (int interval : {1,2,3}) {
            env.kernel.thread->last_vblank_waited=10;
            assert(display_wait(env,1,interval,false,false)==0);
            assert(env.display.waited==10+(enabled ? 1 : interval));
            assert(display_wait(env,1,interval,true,true)==0);
            assert(env.display.waited==10+(enabled ? 1 : interval));
        }
    }
    // Per-game edits affect current config, never overwrite the global YAML values.
    settings.fps_hack=true;
    apply_game_session_settings(env,settings);
    assert(env.cfg.current_config.fps_hack && !env.cfg.fps_hack && !env.cfg.turbo_mode);
    env.display.abort=true;
    assert(display_wait(env,1,1,false,false)==SCE_DISPLAY_ERROR_NO_PIXEL_DATA);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    path=pathlib.Path(tmp)
    (path/'test.cpp').write_text(code)
    command=[sys.argv[1], '-std=c++20', '-UNDEBUG', '-I'+str(repo/'ios/include'), str(path/'test.cpp'), '-o', str(path/'test')]
    if yaml_enabled:
        command += ['-I'+str(repo/'external/yaml-cpp/include'), sys.argv[2]]
    subprocess.run(command,check=True)
    subprocess.run([str(path/'test'),tmp],check=True,timeout=10)
print('Native settings, bridge wiring and guest vblank policy passed' + ('; production YAML on/off roundtrip passed' if yaml_enabled else ''))
