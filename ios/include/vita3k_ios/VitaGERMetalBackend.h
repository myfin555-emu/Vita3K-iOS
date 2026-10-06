#pragma once

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

NS_ASSUME_NONNULL_BEGIN

// Isolated GE:R native-Metal foundation. It is compiled only by the GE:R
// build and is not selected by the normal Vita3K renderer yet.
@interface VitaGERMetalBackend : NSObject

@property(nonatomic, readonly) id<MTLDevice> device;
@property(nonatomic, readonly) id<MTLCommandQueue> commandQueue;

- (instancetype)initWithDevice:(id<MTLDevice>)device;

// Resource path.
- (nullable id<MTLBuffer>)bufferWithLength:(NSUInteger)length
                                     label:(NSString *)label;
- (nullable id<MTLSamplerState>)samplerWithMinFilter:(MTLSamplerMinMagFilter)minFilter
                                           magFilter:(MTLSamplerMinMagFilter)magFilter;

// Texture path.
- (nullable id<MTLTexture>)textureWithWidth:(NSUInteger)width
                                     height:(NSUInteger)height
                                     format:(MTLPixelFormat)format
                                      label:(NSString *)label;

// Shader path.
- (nullable id<MTLFunction>)functionNamed:(NSString *)name
                                   source:(NSString *)source
                                    error:(NSError * _Nullable * _Nullable)error;

// Pipeline path.
- (nullable id<MTLRenderPipelineState>)pipelineWithVertexFunction:(id<MTLFunction>)vertex
                                                   fragmentFunction:(id<MTLFunction>)fragment
                                                    colorPixelFormat:(MTLPixelFormat)colorFormat
                                                    depthPixelFormat:(MTLPixelFormat)depthFormat
                                                                error:(NSError * _Nullable * _Nullable)error;

// Command path.
- (BOOL)encodeClearToTexture:(id<MTLTexture>)texture
                       color:(MTLClearColor)color
                       error:(NSError * _Nullable * _Nullable)error;

@end

NS_ASSUME_NONNULL_END
