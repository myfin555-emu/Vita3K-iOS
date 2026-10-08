#import <Foundation/Foundation.h>
#include <sys/mman.h>
#include <unistd.h>
#include <algorithm>
#include <cstddef>
#include <vita3k_ios/IOSJIT.h>

bool vita3k_ios_can_allocate_jit() {
    // iOS arm64 user-space uses a 16 KiB page granularity. Query the
    // runtime rather than assuming a desktop 4 KiB page, and keep the probe
    // large enough for both current and future Apple page configurations.
    const long runtime_page = sysconf(_SC_PAGESIZE);
    const size_t size = std::max<size_t>(runtime_page > 0 ? static_cast<size_t>(runtime_page) : 16 * 1024, 16 * 1024);
    void *memory = mmap(nullptr, size, PROT_READ | PROT_WRITE | PROT_EXEC,
        MAP_PRIVATE | MAP_ANON, -1, 0);
    if (memory == MAP_FAILED)
        return false;
    // Exercise the same protection transitions as Oaknut, without executing
    // untrusted memory or taking a signal when the process lacks permission.
    const bool available = mprotect(memory, size, PROT_READ | PROT_EXEC) == 0
        && mprotect(memory, size, PROT_READ | PROT_WRITE) == 0
        && mprotect(memory, size, PROT_READ | PROT_EXEC) == 0;
    munmap(memory, size);
    return available;
}

bool vita3k_ios_supports_universal_jit() {
    if (@available(iOS 26.0, *))
        return true;
    return false;
}

#import <vita3k_ios/JITBridge.h>
extern "C" int csops(pid_t, unsigned int, void *, size_t);
@implementation TsubomiJITBridge
+ (BOOL)hasDebugEntitlement {
    uint32_t flags = 0;
    constexpr unsigned int status = 0;
    constexpr uint32_t getTaskAllow = 0x4;
    return csops(getpid(), status, &flags, sizeof(flags)) == 0 && (flags & getTaskAllow) != 0;
}
@end

#if defined(__aarch64__)
__attribute__((noinline, optnone, naked)) void vita3k_ios_detach_jit_debugger() {
    __asm__("mov x16, #0\n"
            "brk #0xf00d\n"
            "ret");
}
#else
void vita3k_ios_detach_jit_debugger() {}
#endif
