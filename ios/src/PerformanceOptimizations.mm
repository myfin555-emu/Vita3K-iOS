// iOS performance helpers: memory pressure, thermal state, stability defaults.
//
// Implemented as Objective-C++ so we can read NSProcessInfo.thermalState and
// Mach task info without pulling UIKit into every translation unit.

#include <vita3k_ios/PerformanceOptimizations.h>

#include <util/log.h>

#include <algorithm>
#include <atomic>
#include <mach/mach.h>
#include <os/proc.h>

#define Ptr MacTypesPtr
#import <Foundation/Foundation.h>
#undef Ptr

namespace vita3k_ios {
namespace {

std::atomic<bool> g_gc_requested{ false };

uint64_t task_rss_bytes() {
    task_vm_info_data_t info{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    const kern_return_t kr = task_info(mach_task_self(), TASK_VM_INFO,
        reinterpret_cast<task_info_t>(&info), &count);
    if (kr != KERN_SUCCESS)
        return 0;
    return static_cast<uint64_t>(info.phys_footprint);
}

} // namespace

int MemoryMonitor::get_memory_pressure() {
    const uint64_t available = get_available_bytes();
    if (available == 0)
        return 0;

    // Rough budget: if less than 200 MiB free, pressure climbs quickly.
    constexpr uint64_t soft_limit = 400ull * 1024 * 1024;
    constexpr uint64_t hard_limit = 120ull * 1024 * 1024;

    if (available >= soft_limit)
        return 0;
    if (available <= hard_limit)
        return 100;

    const double t = static_cast<double>(soft_limit - available)
        / static_cast<double>(soft_limit - hard_limit);
    return static_cast<int>(t * 100.0);
}

uint64_t MemoryMonitor::get_rss_mb() {
    return task_rss_bytes() / (1024 * 1024);
}

uint64_t MemoryMonitor::get_available_bytes() {
#if defined(__APPLE__)
    return os_proc_available_memory();
#else
    return 0;
#endif
}

bool MemoryMonitor::is_memory_critical() {
    return get_memory_pressure() >= 85;
}

void MemoryMonitor::request_gc() {
    g_gc_requested.store(true, std::memory_order_release);
    LOG_WARN("iOS MemoryMonitor: GC requested (rss={} MiB, available={} MiB)",
        get_rss_mb(), get_available_bytes() / (1024 * 1024));
}

bool MemoryMonitor::consume_gc_request() {
    return g_gc_requested.exchange(false, std::memory_order_acq_rel);
}

ThermalThrottleManager::ThermalState ThermalThrottleManager::get_thermal_state() {
    @autoreleasepool {
        const NSProcessInfoThermalState state = NSProcessInfo.processInfo.thermalState;
        switch (state) {
        case NSProcessInfoThermalStateNominal:
            return ThermalState::NOMINAL;
        case NSProcessInfoThermalStateFair:
            return ThermalState::FAIR;
        case NSProcessInfoThermalStateSerious:
            return ThermalState::SERIOUS;
        case NSProcessInfoThermalStateCritical:
            return ThermalState::CRITICAL;
        default:
            return ThermalState::NOMINAL;
        }
    }
}

ThermalThrottleManager::ScalingAdvice ThermalThrottleManager::advice_for(
    ThermalState state, const ScalingAdvice &baseline) {
    ScalingAdvice advice = baseline;
    switch (state) {
    case ThermalState::NOMINAL:
        break;
    case ThermalState::FAIR:
        // Mild: keep image quality, prefer async shaders, slightly lower aniso.
        advice.async_pipeline_compilation = true;
        if (advice.anisotropic_filtering > 2)
            advice.anisotropic_filtering = 2;
        break;
    case ThermalState::SERIOUS:
        // Noticeable heat: drop resolution a notch and disable high-accuracy paths.
        advice.resolution_multiplier = std::min(advice.resolution_multiplier, 1.0f) * 0.75f;
        if (advice.resolution_multiplier < 0.5f)
            advice.resolution_multiplier = 0.5f;
        advice.high_accuracy = false;
        advice.surface_sync = false;
        advice.anisotropic_filtering = 1;
        advice.async_pipeline_compilation = true;
        break;
    case ThermalState::CRITICAL:
        // Protect the device: half-res, cheapest GPU path.
        advice.resolution_multiplier = 0.5f;
        advice.high_accuracy = false;
        advice.surface_sync = false;
        advice.anisotropic_filtering = 1;
        advice.async_pipeline_compilation = true;
        break;
    }
    return advice;
}

const char *ThermalThrottleManager::state_name(ThermalState state) {
    switch (state) {
    case ThermalState::NOMINAL: return "nominal";
    case ThermalState::FAIR: return "fair";
    case ThermalState::SERIOUS: return "serious";
    case ThermalState::CRITICAL: return "critical";
    }
    return "unknown";
}

StabilityDefaults recommended_stability_defaults() {
    return {};
}

PerfSample sample_runtime_pressure() {
    PerfSample sample;
    sample.thermal = ThermalThrottleManager::get_thermal_state();
    sample.memory_pressure = MemoryMonitor::get_memory_pressure();
    sample.rss_mb = MemoryMonitor::get_rss_mb();
    sample.available_mb = MemoryMonitor::get_available_bytes() / (1024 * 1024);
    return sample;
}

} // namespace vita3k_ios
