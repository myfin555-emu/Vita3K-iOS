"""Exercise production compiler-counter drain and cache-save scheduling."""
import pathlib
import re
import subprocess
import sys
import tempfile

repo = pathlib.Path(__file__).resolve().parents[2]
state = (repo / 'vita3k/renderer/include/renderer/state.h').read_text()
header = (repo / 'vita3k/renderer/include/renderer/vulkan/pipeline_cache.h').read_text()
renderer = (repo / 'vita3k/renderer/src/renderer.cpp').read_text()
vulkan = (repo / 'vita3k/renderer/src/vulkan/renderer.cpp').read_text()
counter = re.search(r'^\s*std::atomic<uint32_t> shaders_count_compiled[^\n]+', state, re.M).group()
deadline = re.search(r'^\s*std::atomic<uint64_t> next_pipeline_cache_save[^\n]+', header, re.M).group()
drain = re.search(r'const uint32_t newly_compiled = [^;]+;', renderer).group()
start = vulkan.index('    auto save_at = pipeline_cache.next_pipeline_cache_save')
end = vulkan.index('\n}', start)
schedule = vulkan[start:end]
code = r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <limits>
#include <thread>
#include <vector>
struct Counter {
'''
code += counter + '\nuint32_t drain() { ' + drain + ' return newly_compiled; }\n};\n'
code += 'struct Cache {\n' + deadline + r'''
    int saves = 0;
    bool compile_during_save = false;
    void save_pipeline_cache() {
        ++saves;
        if (compile_during_save) {
            std::thread compiler([&] { next_pipeline_cache_save = 200; });
            compiler.join();
        }
    }
};
void swap(Cache &pipeline_cache, uint64_t time_s) {
'''
code += schedule + '\n}\n'
code += r'''
int main() {
    Counter counter;
    constexpr unsigned jobs_per_worker = 100000;
    std::atomic<int> active{2};
    std::vector<std::thread> workers;
    for (int i = 0; i < 2; ++i)
        workers.emplace_back([&] {
            for (unsigned job = 0; job < jobs_per_worker; ++job)
                counter.shaders_count_compiled++;
            --active;
        });
    uint64_t observed = 0;
    while (active != 0) observed += counter.drain();
    for (auto &worker : workers) worker.join();
    observed += counter.drain();
    assert(observed == 2 * jobs_per_worker);
    assert(counter.drain() == 0);
    Cache cache;
    swap(cache, 100);
    assert(cache.saves == 0);
    cache.next_pipeline_cache_save = 100;
    swap(cache, 99);
    assert(cache.saves == 0);
    cache.compile_during_save = true;
    swap(cache, 100);
    assert(cache.saves == 1 && cache.next_pipeline_cache_save == 200);
    swap(cache, 199);
    assert(cache.saves == 1);
    cache.compile_during_save = false;
    swap(cache, 200);
    assert(cache.saves == 2);
    swap(cache, 201);
    assert(cache.saves == 2);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    path = pathlib.Path(tmp)
    (path / 'test.cpp').write_text(code)
    subprocess.run([sys.argv[1], '-std=c++17', '-pthread', '-UNDEBUG',
                    str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True, timeout=5)
print('Production shader counter and cache-save scheduling checks passed')
