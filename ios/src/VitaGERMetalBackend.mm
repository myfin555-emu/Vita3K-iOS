#import <vita3k_ios/VitaGERMetalBackend.h>

@interface VitaGERMetalBackend () {
    NSLock *_cacheLock;
    NSMutableDictionary<NSString *, id<MTLSamplerState>> *_samplerCache;
    NSMutableDictionary<NSString *, id<MTLTexture>> *_textureCache;
    NSMutableDictionary<NSString *, id<MTLFunction>> *_shaderCache;
    NSMutableDictionary<NSString *, id<MTLRenderPipelineState>> *_pipelineCache;
}
@end

@implementation VitaGERMetalBackend

- (instancetype)initWithDevice:(id<MTLDevice>)device {
    self = [super init];
    if (!self)
        return nil;
    _device = device;
    _commandQueue = [device newCommandQueue];
    if (!_commandQueue)
        return nil;
    _cacheLock = [NSLock new];
    _samplerCache = [NSMutableDictionary dictionary];
    _textureCache = [NSMutableDictionary dictionary];
    _shaderCache = [NSMutableDictionary dictionary];
    _pipelineCache = [NSMutableDictionary dictionary];
    return self;
}

- (id<MTLBuffer>)bufferWithLength:(NSUInteger)length label:(NSString *)label {
    if (!length)
        return nil;
    id<MTLBuffer> buffer =
        [_device newBufferWithLength:length options:MTLResourceStorageModeShared];
    buffer.label = label;
    return buffer;
}

- (id<MTLSamplerState>)samplerWithMinFilter:(MTLSamplerMinMagFilter)minFilter
                                  magFilter:(MTLSamplerMinMagFilter)magFilter {
    NSString *key = [NSString stringWithFormat:@"%ld:%ld",
        (long)minFilter, (long)magFilter];
    [_cacheLock lock];
    id<MTLSamplerState> cached = _samplerCache[key];
    [_cacheLock unlock];
    if (cached)
        return cached;

    MTLSamplerDescriptor *descriptor = [MTLSamplerDescriptor new];
    descriptor.minFilter = minFilter;
    descriptor.magFilter = magFilter;
    descriptor.mipFilter = MTLSamplerMipFilterNotMipmapped;
    id<MTLSamplerState> sampler = [_device newSamplerStateWithDescriptor:descriptor];
    if (!sampler)
        return nil;

    [_cacheLock lock];
    _samplerCache[key] = sampler;
    [_cacheLock unlock];
    return sampler;
}

- (id<MTLTexture>)textureWithWidth:(NSUInteger)width
                             height:(NSUInteger)height
                             format:(MTLPixelFormat)format
                              label:(NSString *)label {
    if (!width || !height)
        return nil;
    NSString *key = [NSString stringWithFormat:@"%lu:%lu:%lu",
        (unsigned long)width, (unsigned long)height, (unsigned long)format];

    [_cacheLock lock];
    id<MTLTexture> cached = _textureCache[key];
    [_cacheLock unlock];
    if (cached)
        return cached;

    MTLTextureDescriptor *descriptor =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
                                                           width:width
                                                          height:height
                                                       mipmapped:NO];
    descriptor.usage = MTLTextureUsageRenderTarget |
                       MTLTextureUsageShaderRead |
                       MTLTextureUsageShaderWrite;
    descriptor.storageMode = MTLStorageModePrivate;

    id<MTLTexture> texture = [_device newTextureWithDescriptor:descriptor];
    if (!texture)
        return nil;
    texture.label = label;

    [_cacheLock lock];
    _textureCache[key] = texture;
    [_cacheLock unlock];
    return texture;
}

- (id<MTLFunction>)functionNamed:(NSString *)name
                           source:(NSString *)source
                            error:(NSError * _Nullable * _Nullable)error {
    [_cacheLock lock];
    id<MTLFunction> cached = _shaderCache[name];
    [_cacheLock unlock];
    if (cached)
        return cached;

    MTLCompileOptions *options = [MTLCompileOptions new];
    id<MTLLibrary> library =
        [_device newLibraryWithSource:source options:options error:error];
    if (!library)
        return nil;
    id<MTLFunction> function = [library newFunctionWithName:name];
    if (!function)
        return nil;

    [_cacheLock lock];
    _shaderCache[name] = function;
    [_cacheLock unlock];
    return function;
}

- (id<MTLRenderPipelineState>)pipelineWithVertexFunction:(id<MTLFunction>)vertex
                                          fragmentFunction:(id<MTLFunction>)fragment
                                           colorPixelFormat:(MTLPixelFormat)colorFormat
                                           depthPixelFormat:(MTLPixelFormat)depthFormat
                                                       error:(NSError * _Nullable * _Nullable)error {
    NSString *key = [NSString stringWithFormat:@"%p:%p:%lu:%lu",
        vertex, fragment, (unsigned long)colorFormat, (unsigned long)depthFormat];

    [_cacheLock lock];
    id<MTLRenderPipelineState> cached = _pipelineCache[key];
    [_cacheLock unlock];
    if (cached)
        return cached;

    MTLRenderPipelineDescriptor *descriptor = [MTLRenderPipelineDescriptor new];
    descriptor.vertexFunction = vertex;
    descriptor.fragmentFunction = fragment;
    descriptor.colorAttachments[0].pixelFormat = colorFormat;
    descriptor.depthAttachmentPixelFormat = depthFormat;

    id<MTLRenderPipelineState> pipeline =
        [_device newRenderPipelineStateWithDescriptor:descriptor error:error];
    if (!pipeline)
        return nil;

    [_cacheLock lock];
    _pipelineCache[key] = pipeline;
    [_cacheLock unlock];
    return pipeline;
}

- (BOOL)encodeClearToTexture:(id<MTLTexture>)texture
                       color:(MTLClearColor)color
                       error:(NSError * _Nullable * _Nullable)error {
    if (!texture)
        return NO;

    id<MTLCommandBuffer> commandBuffer = [_commandQueue commandBuffer];
    if (!commandBuffer) {
        if (error)
            *error = [NSError errorWithDomain:@"VitaGERMetal"
                                         code:1
                                     userInfo:@{NSLocalizedDescriptionKey:
                                         @"Metal command buffer creation failed"}];
        return NO;
    }

    MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = texture;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[0].clearColor = color;

    id<MTLRenderCommandEncoder> encoder =
        [commandBuffer renderCommandEncoderWithDescriptor:pass];
    if (!encoder) {
        if (error)
            *error = [NSError errorWithDomain:@"VitaGERMetal"
                                         code:2
                                     userInfo:@{NSLocalizedDescriptionKey:
                                         @"Metal render encoder creation failed"}];
        return NO;
    }

    [encoder endEncoding];
    [commandBuffer commit];
    return YES;
}

@end
