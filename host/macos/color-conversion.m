// SPDX-License-Identifier: GPL-3.0-or-later
#import "color-conversion.h"
#import <Metal/Metal.h>
#import <CoreVideo/CVMetalTextureCache.h>

// RGB half-float -> video-range P010. PQ is absolute (10,000 cd/m² at 1),
// with no display tone mapping. P3-D65 -> BT.2020 is a linear-light transform.
// Matrix derived from the standard primary xy coordinates and D65 white.
#define DP_METAL_SOURCE(...) @#__VA_ARGS__
static NSString *const shader = @"#include <metal_stdlib>\n" DP_METAL_SOURCE(
using namespace metal;
struct Parameters { uint width; uint height; uint source; uint unused; };
float3 pq_decode(float3 x) {
    float3 p = pow(clamp(x, 0.0f, 1.0f), float3(1.0f / 78.84375f));
    return 10000.0f * pow(max(p - 0.8359375f, 0.0f) / (18.8515625f - 18.6875f * p), float3(1.0f / 0.1593017578125f));
}
float3 pq_encode(float3 nits) {
    float3 p = pow(clamp(nits / 10000.0f, 0.0f, 1.0f), float3(0.1593017578125f));
    return pow((0.8359375f + 18.8515625f*p) / (1.0f + 18.6875f*p), float3(78.84375f));
}
float3 converted_rgb(texture2d<float, access::read> image, int2 position, constant Parameters &p) {
    uint2 at = uint2(clamp(position, int2(0), int2(p.width-1, p.height-1)));
    float3 rgb = image.read(at).rgb;
    if (p.source == 2 || p.source == 3) {
        float3 linear = pq_decode(rgb);
        if (p.source == 2) linear = float3(
            dot(linear, float3(0.75383303f, 0.19859737f, 0.04756960f)),
            dot(linear, float3(0.04574385f, 0.94177722f, 0.01247893f)),
            dot(linear, float3(-0.00121034f, 0.01760172f, 0.98360862f)));
        return pq_encode(linear);
    }
    rgb = clamp(rgb, 0.0f, 1.0f);
    float3 linear = p.source == 4 ? rgb : select(rgb / 12.92f, pow((rgb + 0.055f) / 1.055f, float3(2.4f)), rgb > 0.04045f);
    return select(4.5f * linear, 1.099f * pow(linear, float3(0.45f)) - 0.099f, linear >= 0.018f);
}
float3 yuv_at(texture2d<float, access::read> image, int2 position, constant Parameters &p) {
    float3 rgb = converted_rgb(image, position, p);
    bool hdr = p.source == 2 || p.source == 3;
    float kr = hdr ? 0.2627f : 0.2126f;
    float kb = hdr ? 0.0593f : 0.0722f;
    float y = dot(rgb, float3(kr, 1.0f-kr-kb, kb));
    return float3(y, (rgb.b-y)/(2.0f*(1.0f-kb)), (rgb.r-y)/(2.0f*(1.0f-kr)));
}
kernel void convert_p010(texture2d<float, access::read> image [[texture(0)]],
                         texture2d<float, access::write> luma [[texture(1)]],
                         texture2d<float, access::write> chroma [[texture(2)]],
                         constant Parameters &p [[buffer(0)]], uint2 at [[thread_position_in_grid]]) {
    if (at.x >= p.width || at.y >= p.height) return;
    float y = round(clamp(64.0f + 876.0f*yuv_at(image, int2(at), p).x, 64.0f, 940.0f));
    luma.write(float4(y * 64.0f / 65535.0f), at);
    if ((at.x & 1) || (at.y & 1)) return;
    // Horizontally left-cosited, vertically centered (the encoder's LEFT flag).
    // Low-pass around that location, rather than shifting chroma by half a pixel.
    float2 uv = float2(0.0f);
    for (int dy = 0; dy < 2; ++dy)
        uv += 0.125f*yuv_at(image, int2(at)+int2(-1,dy), p).yz +
              0.250f*yuv_at(image, int2(at)+int2(0,dy), p).yz +
              0.125f*yuv_at(image, int2(at)+int2(1,dy), p).yz;
    float2 code = round(clamp(512.0f + 896.0f*uv, 64.0f, 960.0f));
    chroma.write(float4(code * 64.0f / 65535.0f, 0.0f, 0.0f), at/2);
}
);
#undef DP_METAL_SOURCE

@interface DPColorConverter ()
@property(nonatomic, strong) id<MTLDevice> device;
@property(nonatomic, strong) id<MTLCommandQueue> queue;
@property(nonatomic, strong) id<MTLComputePipelineState> pipeline;
@property(nonatomic) CVMetalTextureCacheRef cache;
@end
@implementation DPColorConverter
- (instancetype)init {
    self = [super init]; if (!self) return nil;
    _device = MTLCreateSystemDefaultDevice(); if (!_device) return nil;
    _queue = [_device newCommandQueue]; if (!_queue) return nil;
    NSError *error = nil;
    MTLCompileOptions *options = [MTLCompileOptions new];
    if (@available(macOS 15.0, *)) {
        options.mathMode = MTLMathModeSafe;
        options.mathFloatingPointFunctions = MTLMathFloatingPointFunctionsPrecise;
    } else return nil;
    id<MTLLibrary> library = [_device newLibraryWithSource:shader options:options error:&error];
    if (!library) { fprintf(stderr, "DeskPort color shader: %s\n", error.localizedDescription.UTF8String); return nil; }
    _pipeline = [_device newComputePipelineStateWithFunction:[library newFunctionWithName:@"convert_p010"] error:&error];
    if (!_pipeline || CVMetalTextureCacheCreate(NULL, NULL, _device, NULL, &_cache) != kCVReturnSuccess) return nil;
    return self;
}
- (void)dealloc { if (_cache) CFRelease(_cache); }
- (BOOL)convert:(CVPixelBufferRef)source toP010:(CVPixelBufferRef)destination sourceColor:(DPColorSource)color {
    const size_t width = CVPixelBufferGetWidth(source), height = CVPixelBufferGetHeight(source);
    if (!color || CVPixelBufferGetPixelFormatType(source) != kCVPixelFormatType_64RGBAHalf ||
        CVPixelBufferGetPixelFormatType(destination) != kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange ||
        width != CVPixelBufferGetWidth(destination) || height != CVPixelBufferGetHeight(destination)) return NO;
    CVMetalTextureRef input = NULL, y = NULL, uv = NULL; BOOL success = NO;
    if (CVMetalTextureCacheCreateTextureFromImage(NULL, _cache, source, NULL, MTLPixelFormatRGBA16Float, width, height, 0, &input) != kCVReturnSuccess ||
        CVMetalTextureCacheCreateTextureFromImage(NULL, _cache, destination, NULL, MTLPixelFormatR16Unorm, width, height, 0, &y) != kCVReturnSuccess ||
        CVMetalTextureCacheCreateTextureFromImage(NULL, _cache, destination, NULL, MTLPixelFormatRG16Unorm,
            CVPixelBufferGetWidthOfPlane(destination, 1), CVPixelBufferGetHeightOfPlane(destination, 1), 1, &uv) != kCVReturnSuccess) goto done;
    {
        uint32_t parameters[] = {(uint32_t)width, (uint32_t)height, color, 0};
        id<MTLCommandBuffer> command = [_queue commandBuffer];
        id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
        if (!encoder) goto done;
        [encoder setComputePipelineState:_pipeline];
        [encoder setTexture:CVMetalTextureGetTexture(input) atIndex:0];
        [encoder setTexture:CVMetalTextureGetTexture(y) atIndex:1];
        [encoder setTexture:CVMetalTextureGetTexture(uv) atIndex:2];
        [encoder setBytes:parameters length:sizeof(parameters) atIndex:0];
        [encoder dispatchThreadgroups:MTLSizeMake((width+15)/16, (height+15)/16, 1) threadsPerThreadgroup:MTLSizeMake(16,16,1)];
        [encoder endEncoding]; [command commit]; [command waitUntilCompleted];
        success = command.status == MTLCommandBufferStatusCompleted;
    }
done:
    if (input) CFRelease(input); if (y) CFRelease(y); if (uv) CFRelease(uv);
    return success;
}
@end
