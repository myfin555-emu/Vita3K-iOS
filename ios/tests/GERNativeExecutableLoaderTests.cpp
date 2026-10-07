#include <vita3k_ios/GERNativeExecutableLoader.h>

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace ger::ios;

static void put16(std::vector<std::uint8_t> &d, std::size_t o, std::uint16_t v) {
    d[o] = static_cast<std::uint8_t>(v);
    d[o + 1] = static_cast<std::uint8_t>(v >> 8);
}
static void put32(std::vector<std::uint8_t> &d, std::size_t o, std::uint32_t v) {
    d[o] = static_cast<std::uint8_t>(v);
    d[o + 1] = static_cast<std::uint8_t>(v >> 8);
    d[o + 2] = static_cast<std::uint8_t>(v >> 16);
    d[o + 3] = static_cast<std::uint8_t>(v >> 24);
}
static void put64(std::vector<std::uint8_t> &d, std::size_t o, std::uint64_t v) {
    put32(d, o, static_cast<std::uint32_t>(v));
    put32(d, o + 4, static_cast<std::uint32_t>(v >> 32));
}

int main() {
    std::vector<std::uint8_t> data(0x400, 0);
    // SCE header magic + SelfHeader offsets.
    put32(data, 0x00, 0x00454353);
    put64(data, 0x40, 0xA0); // elf_offset
    put64(data, 0x48, 0xE0); // phdr_offset
    put64(data, 0x58, 0x180); // segment_info_offset

    const std::size_t e = 0xA0;
    data[e + 0] = 0x7F; data[e + 1] = 'E'; data[e + 2] = 'L'; data[e + 3] = 'F';
    data[e + 4] = 1; data[e + 5] = 1;
    put16(data, e + 0x10, 0xFE04);
    put16(data, e + 0x12, 40);
    put32(data, e + 0x18, 0x8); // module-relative entry RVA
    put32(data, e + 0x1C, 0x34);
    put16(data, e + 0x2A, 0x20);
    put16(data, e + 0x2C, 3);

    // PT_LOAD RX: VA 0x81000000, 16 bytes.
    put32(data, 0xE0 + 0x00, 1);
    put32(data, 0xE0 + 0x08, 0x81000000);
    put32(data, 0xE0 + 0x10, 16);
    put32(data, 0xE0 + 0x14, 16);
    put32(data, 0xE0 + 0x18, 5);
    // PT_LOAD RW: VA 0x81001000, 4 file / 8 memory.
    put32(data, 0x100 + 0x00, 1);
    put32(data, 0x100 + 0x08, 0x81001000);
    put32(data, 0x100 + 0x10, 4);
    put32(data, 0x100 + 0x14, 8);
    put32(data, 0x100 + 0x18, 6);
    // PT_SCE_RELA.
    put32(data, 0x120 + 0x00, 0x60000000);
    put32(data, 0x120 + 0x10, 8);
    // Segment descriptors.
    put64(data, 0x180 + 0 * 32, 0x200); put64(data, 0x188 + 0 * 32, 16);
    put32(data, 0x190 + 0 * 32, 1); put32(data, 0x198 + 0 * 32, 2);
    put64(data, 0x180 + 1 * 32, 0x210); put64(data, 0x188 + 1 * 32, 4);
    put32(data, 0x190 + 1 * 32, 1); put32(data, 0x198 + 1 * 32, 2);
    put64(data, 0x180 + 2 * 32, 0x220); put64(data, 0x188 + 2 * 32, 8);
    put32(data, 0x190 + 2 * 32, 1); put32(data, 0x198 + 2 * 32, 2);

    for (std::size_t i = 0; i < 16; ++i) data[0x200 + i] = static_cast<std::uint8_t>(i);
    put32(data, 0x210, 0);
    // short SCE relocation: short=1, symseg=0, ABS32, datseg=1, offset=0, addend=4.
    put32(data, 0x220, 0x00010201);
    put32(data, 0x224, 0x00400000);

    const auto path = std::filesystem::temp_directory_path() / "ger-native-loader-test-eboot.bin";
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));

    NativeExecutableImage image;
    std::string error;
    const bool ok = NativeExecutableLoader{}.load(path, image, error);
    std::filesystem::remove(path);

    assert(ok && error.empty());
    assert(image.module_base == 0x81000000);
    assert(image.entry == 0x81000008);
    assert(image.memory_size == 0x1008);
    assert(image.memory[0] == 0 && image.memory[15] == 15);
    assert(image.memory[0x1000] == 0);
    assert(image.memory[0x1004] == 0); // BSS
    assert(image.relocations.size() == 1);
    assert(image.relocations_applied == 1);
    const std::size_t relocated = 0x1000;
    assert(image.memory[relocated] == 0x04);
    assert(image.memory[relocated + 1] == 0x00);
    assert(image.memory[relocated + 2] == 0x00);
    assert(image.memory[relocated + 3] == 0x81);
    return 0;
}
