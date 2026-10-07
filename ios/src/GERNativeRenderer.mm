#import <vita3k_ios/GERNativeRenderer.h>

#include <vita3k_ios/GERNativeRuntime.h>

#include <chrono>
#include <memory>
#include <string>

@interface GERNativeRenderer ()
@property(nonatomic, weak) MTKView *view;
@property(nonatomic, strong) id<MTLCommandQueue> queue;
@property(nonatomic, strong) id<MTLDevice> device;
@end

@implementation GERNativeRenderer {
    std::unique_ptr<ger::ios::NativeRuntime> _runtime;
    std::chrono::steady_clock::time_point _lastTick;
}

- (instancetype)initWithView:(MTKView *)view {
    self = [super init];
    if (self) {
        _view = view;
        _device = view.device;
        _queue = [_device newCommandQueue];
        _runtime = std::make_unique<ger::ios::NativeRuntime>();
        _lastTick = std::chrono::steady_clock::now();
        view.delegate = self;
        view.enableSetNeedsDisplay = NO;
        view.paused = NO;
        view.preferredFramesPerSecond = 60;
    }
    return self;
}

- (void)start {
    std::string error;
    if (!_runtime->start(error)) {
        _runtime->scan();
        return;
    }
    _lastTick = std::chrono::steady_clock::now();
}

- (void)drawInMTKView:(MTKView *)view {
    if (!_queue || !view.currentDrawable || !view.currentRenderPassDescriptor)
        return;

    const auto now = std::chrono::steady_clock::now();
    const std::chrono::duration<double> delta = now - _lastTick;
    _lastTick = now;
    _runtime->tick(delta.count());

    MTLRenderPassDescriptor *pass = view.currentRenderPassDescriptor;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;

    const auto &status = _runtime->status();
    const double pulse = status.ready ? 0.10 : 0.025;
    pass.colorAttachments[0].clearColor =
        MTLClearColorMake(pulse, status.ready ? 0.08 : 0.025, 0.02, 1.0);

    id<MTLCommandBuffer> commandBuffer = [_queue commandBuffer];
    id<MTLRenderCommandEncoder> encoder =
        [commandBuffer renderCommandEncoderWithDescriptor:pass];
    [encoder endEncoding];
    [commandBuffer presentDrawable:view.currentDrawable];
    [commandBuffer commit];
}

@end
