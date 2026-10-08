"""Compile the production presentation input assembly for iOS and desktop."""
import pathlib
import subprocess
import sys
import tempfile

repo = pathlib.Path(__file__).resolve().parents[2]

def assembly(path):
    source = (repo / path).read_text()
    start = source.index('vk::PipelineInputAssemblyStateCreateInfo input_assembly{')
    end = source.index('};', start) + 2
    return source[start:end]

code = r'''
#include <cassert>
namespace vk {
enum class PrimitiveTopology { eTriangleStrip, eLineList, eLineStrip, eTriangleFan };
struct PipelineInputAssemblyStateCreateInfo {
    PrimitiveTopology topology;
    bool primitiveRestartEnable = false;
};
}
auto screen() {
'''
code += assembly('vita3k/renderer/src/vulkan/screen_filters.cpp')
code += '\nreturn input_assembly;\n}\nauto overlay(int i) {\n'
code += '''
    const vk::PrimitiveTopology topologies[] = {
        vk::PrimitiveTopology::eTriangleStrip, vk::PrimitiveTopology::eLineList,
        vk::PrimitiveTopology::eLineStrip, vk::PrimitiveTopology::eTriangleFan
    };
'''
code += assembly('vita3k/renderer/src/vulkan/overlay_renderer.cpp')
code += r'''
return input_assembly;
}
int main() {
#ifdef VITA3K_PLATFORM_IOS
    assert(screen().primitiveRestartEnable);
    assert(overlay(0).primitiveRestartEnable);
    assert(overlay(2).primitiveRestartEnable);
    assert(overlay(3).primitiveRestartEnable);
#else
    assert(!screen().primitiveRestartEnable);
    for (int i = 0; i < 4; ++i)
        assert(!overlay(i).primitiveRestartEnable);
#endif
    assert(!overlay(1).primitiveRestartEnable); // list restart requires an optional Vulkan feature
}
'''
with tempfile.TemporaryDirectory() as tmp:
    source = pathlib.Path(tmp) / 'test.cpp'
    source.write_text(code)
    for defines in ([], ['-DVITA3K_PLATFORM_IOS']):
        binary = pathlib.Path(tmp) / 'test'
        subprocess.run([sys.argv[1], '-std=c++20', '-UNDEBUG', *defines,
                        str(source), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
print('Presentation primitive restart passed for iOS and desktop')
