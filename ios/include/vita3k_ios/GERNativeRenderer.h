#pragma once

#import <MetalKit/MetalKit.h>

@class GERNativeRuntimeView;

@interface GERNativeRenderer : NSObject <MTKViewDelegate>
- (instancetype)initWithView:(MTKView *)view;
- (void)start;
@end
