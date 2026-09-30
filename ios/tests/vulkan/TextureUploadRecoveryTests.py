"""Execute production staging rollover and copy recording without dropping mips."""
import pathlib
import subprocess
import sys
import tempfile

repo = pathlib.Path(__file__).resolve().parents[3]
source = (repo / 'vita3k/renderer/src/vulkan/texture.cpp').read_text()
start = source.index('    if (staging_buffer.used_so_far + upload_size')
end = source.index('\n}\n\nvoid VKTextureCache::upload_done', start)
code = r'''
#include <vkutil/vulkan.h>
#include <util/align.h>
#include <algorithm>
#include <cassert>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>
namespace vkutil { constexpr int vma_mapped_alloc = 0; }
struct Buffer {
    vk::DeviceSize size = 0;
    std::shared_ptr<std::vector<unsigned char>> data;
    void *mapped_data = nullptr;
    unsigned buffer = 0;
    bool fail = false;
    void init_buffer(vk::BufferUsageFlagBits, int) {
        if (fail) throw std::runtime_error("allocation failed");
        data = std::make_shared<std::vector<unsigned char>>(size);
        mapped_data = data->data();
        ++buffer;
    }
};
struct Test {
    struct { Buffer buffer; vk::DeviceSize used_so_far = 0; } staging_buffer;
    struct Queue {
        std::vector<std::shared_ptr<std::vector<unsigned char>>> retired;
        void add_buffer(Buffer &buffer) { retired.push_back(buffer.data); }
    };
    struct Frame { Queue destroy_queue; } frame;
    struct State { Frame *value; Frame &frame() { return *value; } } state{&frame};
    struct Texture { uint32_t memory_needed = 16; } texture;
    Texture *current_texture = &texture;
    struct { int image = 1; } image;
    struct Command {
        std::vector<vk::BufferImageCopy> regions;
        void copyBufferToImage(unsigned, int, vk::ImageLayout, const vk::BufferImageCopy &region) {
            regions.push_back(region);
        }
    } cmd_buffer;
    void upload(const std::vector<unsigned char> &bytes, unsigned mip_index) {
        const vk::DeviceSize upload_size = bytes.size();
        const void *text_data = bytes.data();
        const unsigned face = 0, pixels_per_stride = bytes.size(), buffer_height = 1, width = bytes.size(), height = 1;
'''
code += source[start:end] + '\n    }\n};\n'
code += r'''
int main() {
    Test test;
    test.staging_buffer.buffer.size = 16;
    test.staging_buffer.buffer.init_buffer(vk::BufferUsageFlagBits::eTransferSrc, 0);
    test.upload(std::vector<unsigned char>(16, 1), 0);
    test.upload(std::vector<unsigned char>(8, 2), 1);
    assert(test.frame.destroy_queue.retired.size() == 1);
    assert(*test.frame.destroy_queue.retired[0] == std::vector<unsigned char>(16, 1));
    test.upload(std::vector<unsigned char>(8, 3), 2);
    assert(test.frame.destroy_queue.retired.size() == 1);
    test.upload(std::vector<unsigned char>(37, 4), 3);
    assert(test.staging_buffer.buffer.size == 48);
    assert(test.frame.destroy_queue.retired.size() == 2);
    assert(test.cmd_buffer.regions.size() == 4);
    for (unsigned i = 0; i < 4; ++i) assert(test.cmd_buffer.regions[i].imageSubresource.mipLevel == i);
    assert(test.cmd_buffer.regions[2].bufferOffset == 8);
    assert(test.cmd_buffer.regions[3].bufferOffset == 0);
    assert(std::count(test.staging_buffer.buffer.data->begin(), test.staging_buffer.buffer.data->end(), 4) == 37);
    test.staging_buffer.buffer.fail = true;
    bool threw = false;
    try { test.upload(std::vector<unsigned char>(64, 5), 4); } catch (const std::runtime_error &) { threw = true; }
    assert(threw && test.cmd_buffer.regions.size() == 4);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    path = pathlib.Path(tmp)
    (path / 'test.cpp').write_text(code)
    subprocess.run([sys.argv[1], '-std=c++20', '-UNDEBUG', str(path / 'test.cpp'),
        '-I' + str(repo / 'vita3k/vkutil/include'), '-I' + str(repo / 'vita3k/util/include'),
        '-I' + sys.argv[2], '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True, timeout=5)
print('Production texture rollover preserves prior mip uploads and records every copy')
