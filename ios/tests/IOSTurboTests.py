"""Exercise the production worker registry with a controlled Apple QoS API."""
import pathlib
import subprocess
import sys
import tempfile
repo=pathlib.Path(__file__).resolve().parents[2]
code=r'''
#include <cassert>
#include <future>
#include <thread>
#include <vector>
#include <pthread.h>
struct pthread_override_s { pthread_t target; };
using pthread_override_t=pthread_override_s*;
using qos_class_t=unsigned;
constexpr unsigned QOS_CLASS_USER_INITIATED=25, QOS_CLASS_USER_INTERACTIVE=33;
int starts=0,ends=0,failures=0;
bool reject=false;
pthread_override_t pthread_override_qos_class_start_np(pthread_t thread,unsigned qos,int priority) {
    assert((qos==QOS_CLASS_USER_INITIATED || qos==QOS_CLASS_USER_INTERACTIVE) && priority==0);
    if (reject) { ++failures; return nullptr; }
    ++starts; return new pthread_override_s{thread};
}
int pthread_override_qos_class_end_np(pthread_override_t handle) {
    assert(handle); ++ends; delete handle; return 0;
}
#include <util/ios_performance.h>
int main() {
    {
        util::IOSPerformanceThread worker;
        assert(starts==0);
        util::set_ios_performance_mode(true);
        util::set_ios_performance_mode(true);
        assert(starts==1 && ends==0);
        util::set_ios_performance_mode(false);
        assert(ends==1);
        reject=true;
        util::set_ios_performance_mode(true);
        assert(starts==1 && failures==1);
        reject=false;
        util::set_ios_performance_mode(true);
        assert(starts==2);
    }
    assert(starts==ends && util::ios_performance_registry.threads.empty());
    // A newly created worker inherits the global enabled request.
    {
        util::IOSPerformanceThread worker;
        assert(starts==3 && ends==2);
    }
    {
        util::IOSPerformanceThread compiler(QOS_CLASS_USER_INITIATED);
        assert(starts==4 && ends==3);
    }
    // Exercise toggles racing with dedicated worker creation and destruction.
    std::vector<std::thread> workers;
    for (int i=0;i<4;++i) workers.emplace_back([] {
        for (int n=0;n<500;++n) { util::IOSPerformanceThread worker; std::this_thread::yield(); }
    });
    std::thread toggler([] { for(int i=0;i<500;++i) util::set_ios_performance_mode(i%2); });
    for(auto &worker:workers) worker.join();
    toggler.join();
    util::set_ios_performance_mode(false);
    assert(starts==ends && util::ios_performance_registry.threads.empty());
}
'''
# Ensure every dedicated worker and both boot/runtime settings paths are wired.
for file, signature in [('vita3k/kernel/src/kernel.cpp','static int SDLCALL thread_function('),('vita3k/renderer/src/batch.cpp','static void render_loop('),('vita3k/renderer/src/vulkan/pipeline_cache.cpp','void PipelineCache::compiler_thread(')]:
    text=(repo/file).read_text()
    assert 'util::IOSPerformanceThread performance' in text[text.index(signature):]
app=(repo/'vita3k/app/src/app_init.cpp').read_text()
assert app.count('util::set_ios_performance_mode(emuenv.cfg.turbo_mode);')==3
assert app.index('    emuenv.cfg = persisted_cfg;') < app.index('    // Turbo is global') < app.index('    if (!active_profile)', app.index('    emuenv.cfg = persisted_cfg;'))
with tempfile.TemporaryDirectory() as tmp:
    path=pathlib.Path(tmp)
    (path/'pthread').mkdir(); (path/'util').mkdir()
    (path/'pthread/qos.h').write_text('// Apple API test double declared in the test translation unit\n')
    (path/'util/log.h').write_text('#define LOG_WARN(...) ((void)0)\n')
    (path/'test.cpp').write_text(code)
    subprocess.run([sys.argv[1],'-std=c++20','-pthread','-UNDEBUG','-DVITA3K_PLATFORM_IOS','-I'+tmp,'-I'+str(repo/'vita3k/util/include'),str(path/'test.cpp'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True,timeout=10)
print('iOS Turbo worker registration, live toggles, rejection and concurrent cleanup passed')
