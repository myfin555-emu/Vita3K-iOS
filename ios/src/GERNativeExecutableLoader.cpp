#include <vita3k_ios/GERNativeExecutableLoader.h>
#include <vita3k_ios/GERNativeLogger.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <sstream>
#include <vector>

namespace ger::ios {
namespace {

constexpr std::uint32_t kPtLoad = 1;
constexpr std::uint32_t kPtSceRela = 0x60000000;
constexpr std::uint32_t kRArmNone = 0;
constexpr std::uint32_t kRArmAbs32 = 2;
constexpr std::uint32_t kRArmRel32 = 3;
constexpr std::uint32_t kRArmCall = 28;
constexpr std::uint32_t kRArmJump24 = 29;
constexpr std::uint32_t kRArmTarget1 = 38;
constexpr std::uint32_t kRArmTarget2 = 41;
constexpr std::uint32_t kRArmPrel31 = 42;

std::uint16_t u16(const std::uint8_t *p) {
    return static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
}
std::uint32_t u32(const std::uint8_t *p) {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}
std::uint64_t u64(const std::uint8_t *p) {
    return static_cast<std::uint64_t>(u32(p)) |
           (static_cast<std::uint64_t>(u32(p + 4)) << 32);
}
void put32(std::vector<std::uint8_t> &m, std::size_t off, std::uint32_t v) {
    m[off + 0] = static_cast<std::uint8_t>(v);
    m[off + 1] = static_cast<std::uint8_t>(v >> 8);
    m[off + 2] = static_cast<std::uint8_t>(v >> 16);
    m[off + 3] = static_cast<std::uint8_t>(v >> 24);
}
bool range_ok(std::size_t off, std::size_t len, std::size_t total) {
    return off <= total && len <= total - off;
}
std::size_t raw_segment_offset(const std::vector<std::uint8_t> &data, std::size_t segment_info, std::size_t index) {
    const auto off = segment_info + index * 32;
    if (!range_ok(off, 32, data.size()))
        return std::string::npos;
    const auto raw = u64(data.data() + off);
    const auto size = u64(data.data() + off + 8);
    const auto compression = u32(data.data() + off + 16);
    const auto encryption = u32(data.data() + off + 24);
    if (compression != 1 || encryption != 2 || raw > data.size() || size > data.size() - raw)
        return std::string::npos;
    if (raw > std::numeric_limits<std::size_t>::max())
        return std::string::npos;
    return static_cast<std::size_t>(raw);
}

std::uint32_t segment_address(const NativeExecutableImage &image, std::uint8_t index, std::uint32_t offset) {
    if (index >= image.segments.size())
        return 0;
    return image.segments[index].vaddr + offset;
}

bool write_branch(std::vector<std::uint8_t> &memory, std::size_t off, std::uint32_t pc, std::uint32_t target) {
    if (!range_ok(off, 4, memory.size()))
        return false;
    const auto old = u32(memory.data() + off);
    const auto delta = static_cast<std::int64_t>(static_cast<std::int64_t>(target) - static_cast<std::int64_t>(pc + 8));
    if ((delta & 3) != 0)
        return false;
    const auto imm = delta >> 2;
    if (imm < -0x800000 || imm > 0x7FFFFF)
        return false;
    put32(memory, off, (old & 0xFF000000u) | (static_cast<std::uint32_t>(imm) & 0x00FFFFFFu));
    return true;
}

} // namespace

bool NativeExecutableLoader::load(const std::filesystem::path &self_path, NativeExecutableImage &image, std::string &error) const {
    image = {};
    std::ifstream stream(self_path, std::ios::binary);
    if (!stream) {
        error = "cannot open eboot.bin";
        return false;
    }
    stream.seekg(0, std::ios::end);
    const auto end = stream.tellg();
    if (end <= 0) {
        error = "empty executable";
        return false;
    }
    stream.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(end));
    stream.read(reinterpret_cast<char *>(data.data()), static_cast<std::streamsize>(data.size()));
    if (stream.gcount() != static_cast<std::streamsize>(data.size())) {
        error = "short read of executable";
        return false;
    }

    // SCE SELF header layout used by Vita3K/VitaShell: the SelfHeader starts
    // after the 32-byte SCE header. These offsets are deliberately read from
    // the SELF header rather than trusting ELF e_phoff.
    if (!range_ok(32, 0x58, data.size()) || u32(data.data()) != 0x00454353) {
        error = "not a supported SCE SELF";
        return false;
    }
    const auto elf_offset = static_cast<std::size_t>(u64(data.data() + 32 + 32));
    const auto phdr_offset = static_cast<std::size_t>(u64(data.data() + 32 + 40));
    const auto segment_info = static_cast<std::size_t>(u64(data.data() + 32 + 56));
    if (!range_ok(elf_offset, 0x34, data.size()) || !range_ok(phdr_offset, 32, data.size()) ||
        !range_ok(segment_info, 32, data.size())) {
        error = "SELF metadata is outside the file";
        return false;
    }

    const auto *e = data.data() + elf_offset;
    if (e[0] != 0x7F || e[1] != 'E' || e[2] != 'L' || e[3] != 'F' || e[4] != 1 || e[5] != 1) {
        error = "SELF does not contain a little-endian ELF32 image";
        return false;
    }
    if (u16(e + 0x12) != 40) {
        error = "ELF machine is not ARM32";
        return false;
    }

    const auto entry_raw = u32(e + 0x18);
    const auto phnum = u16(e + 0x2C);
    const auto phentsize = u16(e + 0x2A);
    if (phentsize < 0x20 || phnum == 0 || !range_ok(phdr_offset, static_cast<std::size_t>(phnum) * phentsize, data.size())) {
        error = "invalid Vita program-header table";
        return false;
    }

    std::uint32_t min_va = std::numeric_limits<std::uint32_t>::max();
    std::uint64_t max_va = 0;
    image.segments.reserve(phnum);

    for (std::uint16_t i = 0; i < phnum; ++i) {
        const auto *p = data.data() + phdr_offset + static_cast<std::size_t>(i) * phentsize;
        NativeExecutableSegment seg;
        seg.type = u32(p);
        seg.flags = u32(p + 0x18);
        seg.vaddr = u32(p + 0x08);
        seg.filesz = u32(p + 0x10);
        seg.memsz = u32(p + 0x14);
        const auto si = segment_info + static_cast<std::size_t>(i) * 32;
        if (!range_ok(si, 32, data.size())) {
            error = "missing SELF segment descriptor";
            return false;
        }
        seg.self_offset = u64(data.data() + si);
        seg.self_size = u64(data.data() + si + 8);
        seg.compression = u32(data.data() + si + 16);
        seg.encryption = u32(data.data() + si + 24);
        if (seg.type == kPtLoad) {
            if (seg.memsz < seg.filesz || seg.vaddr + seg.memsz < seg.vaddr) {
                error = "invalid PT_LOAD size";
                return false;
            }
            if (seg.compression != 1 || seg.encryption != 2) {
                error = "encrypted/compressed PT_LOAD requires a SELF extractor before native loading";
                return false;
            }
            if (seg.self_offset > data.size() || seg.self_size < seg.filesz ||
                seg.self_size > data.size() - static_cast<std::size_t>(seg.self_offset)) {
                error = "PT_LOAD SELF range is invalid";
                return false;
            }
            min_va = std::min(min_va, seg.vaddr);
            max_va = std::max(max_va, static_cast<std::uint64_t>(seg.vaddr) + seg.memsz);
        }
        image.segments.push_back(seg);
    }

    if (min_va == std::numeric_limits<std::uint32_t>::max() || max_va <= min_va) {
        error = "ELF has no usable PT_LOAD segments";
        return false;
    }
    if (max_va - min_va > std::numeric_limits<std::size_t>::max()) {
        error = "native memory image is too large";
        return false;
    }
    image.module_base = min_va;
    image.memory_size = static_cast<std::size_t>(max_va - min_va);
    image.memory.assign(image.memory_size, 0);

    for (std::size_t i = 0; i < image.segments.size(); ++i) {
        const auto &seg = image.segments[i];
        if (seg.type != kPtLoad)
            continue;
        const auto dst64 = static_cast<std::uint64_t>(seg.vaddr) - image.module_base;
        if (dst64 > image.memory.size() || seg.memsz > image.memory.size() - static_cast<std::size_t>(dst64)) {
            error = "PT_LOAD does not fit native memory image";
            return false;
        }
        const auto src = raw_segment_offset(data, segment_info, i);
        if (src == std::string::npos || seg.filesz > data.size() - src) {
            error = "PT_LOAD raw SELF mapping is invalid";
            return false;
        }
        std::copy_n(data.begin() + src, seg.filesz, image.memory.begin() + static_cast<std::size_t>(dst64));
        // memory is zero-initialized, so the memsz-filesz tail is the Vita BSS.
    }

    image.entry_rva = entry_raw;
    bool entry_is_va = false;
    for (const auto &seg : image.segments) {
        if (seg.type != kPtLoad || entry_raw < seg.vaddr || entry_raw >= seg.vaddr + seg.memsz)
            continue;
        image.entry = entry_raw;
        entry_is_va = true;
        break;
    }
    if (!entry_is_va) {
        const auto candidate = static_cast<std::uint64_t>(image.module_base) + entry_raw;
        if (candidate > std::numeric_limits<std::uint32_t>::max()) {
            error = "ELF entry RVA overflows module address";
            return false;
        }
        image.entry = static_cast<std::uint32_t>(candidate);
    }

    // Decode every SCE_RELA program segment. Vita uses packed 8-byte short
    // and 12-byte long entries rather than the standard ELF RELA structure.
    for (std::size_t i = 0; i < image.segments.size(); ++i) {
        const auto &seg = image.segments[i];
        if (seg.type != kPtSceRela)
            continue;
        const auto src = raw_segment_offset(data, segment_info, i);
        if (src == std::string::npos || seg.filesz > data.size() - src) {
            error = "SCE_RELA raw SELF mapping is invalid";
            return false;
        }
        std::size_t at = src;
        const auto end_rel = src + seg.filesz;
        while (at < end_rel) {
            if (end_rel - at < 8) {
                error = "truncated SCE short relocation";
                return false;
            }
            const auto w1 = u32(data.data() + at);
            const bool is_short = (w1 & 0xF) == 1;
            NativeRelocation rel;
            rel.is_short = is_short;
            rel.sym_segment = static_cast<std::uint8_t>((w1 >> 4) & 0xF);
            rel.code = static_cast<std::uint8_t>((w1 >> 8) & 0xFF);
            rel.data_segment = static_cast<std::uint8_t>((w1 >> 16) & 0xF);
            if (is_short) {
                const auto w2 = u32(data.data() + at + 4);
                rel.offset = ((w1 >> 20) & 0xFFF) | ((w2 & 0xFFFFF) << 12);
                auto addend = static_cast<std::int32_t>((w2 >> 20) & 0xFFF);
                if (addend & 0x800)
                    addend |= ~0xFFF;
                rel.addend = addend;
                at += 8;
            } else {
                if (end_rel - at < 12) {
                    error = "truncated SCE long relocation";
                    return false;
                }
                rel.second_code = static_cast<std::uint8_t>((w1 >> 24) & 0xFF);
                rel.second_distance = static_cast<std::uint8_t>((w1 >> 28) & 0xF);
                rel.addend = static_cast<std::int32_t>(u32(data.data() + at + 4));
                rel.offset = u32(data.data() + at + 8);
                at += 12;
            }
            image.relocations.push_back(rel);
        }
    }

    // Apply relocations only within the constructed native memory image. This
    // is an image-building step; it does not execute the ARM32 guest code.
    for (const auto &rel : image.relocations) {
        if (rel.data_segment >= image.segments.size() || rel.sym_segment >= image.segments.size()) {
            ++image.relocations_unsupported;
            continue;
        }
        const auto &dst_seg = image.segments[rel.data_segment];
        if (dst_seg.type != kPtLoad)
            continue;
        const auto target_va64 = static_cast<std::uint64_t>(dst_seg.vaddr) + rel.offset;
        if (target_va64 < image.module_base || target_va64 + 4 > static_cast<std::uint64_t>(image.module_base) + image.memory.size()) {
            ++image.relocations_unsupported;
            continue;
        }
        const auto dst = static_cast<std::size_t>(target_va64 - image.module_base);
        const auto sym_base = segment_address(image, rel.sym_segment, 0);
        const auto target = sym_base + static_cast<std::uint32_t>(rel.addend);
        const auto pc = static_cast<std::uint32_t>(target_va64);
        bool applied = false;
        switch (rel.code) {
        case kRArmNone:
            applied = true;
            break;
        case kRArmAbs32:
        case kRArmTarget1:
            put32(image.memory, dst, target);
            applied = true;
            break;
        case kRArmRel32:
        case kRArmTarget2:
            put32(image.memory, dst, target - pc);
            applied = true;
            break;
        case kRArmPrel31:
            put32(image.memory, dst, (target - pc) & 0x7FFFFFFFu);
            applied = true;
            break;
        case kRArmCall:
        case kRArmJump24:
            applied = write_branch(image.memory, dst, pc, target);
            break;
        default:
            break;
        }
        if (applied)
            ++image.relocations_applied;
        else
            ++image.relocations_unsupported;
    }

    NativeLogger::write(
        "loader: PT_LOAD image base=0x" + [&] { std::ostringstream s; s << std::hex << image.module_base; return s.str(); }() +
        " size=0x" + [&] { std::ostringstream s; s << std::hex << image.memory_size; return s.str(); }() +
        " entry=0x" + [&] { std::ostringstream s; s << std::hex << image.entry; return s.str(); }() +
        " relocations=" + std::to_string(image.relocations.size()) +
        " applied=" + std::to_string(image.relocations_applied) +
        " unsupported=" + std::to_string(image.relocations_unsupported));
    return true;
}

} // namespace ger::ios
