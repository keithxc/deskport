// SPDX-License-Identifier: GPL-3.0-or-later
// No display, capture, user session or external input. Run through the Python
// wrapper, which checks build storage and keeps all generated files off-source.
#import "host/macos/color-conversion.m"
#include <math.h>
#include <string.h>
#include <stdlib.h>

// Frozen pre-tile GPU reference: keep the transfer functions, left-cosited
// clamp and float accumulation independent of the optimized production shader.
#define REFERENCE_SOURCE(...) @#__VA_ARGS__
static NSString *const referenceShader = @"#include <metal_stdlib>\n" REFERENCE_SOURCE(
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
    float2 uv = float2(0.0f);
    for (int dy = 0; dy < 2; ++dy)
        uv += 0.125f*yuv_at(image, int2(at)+int2(-1,dy), p).yz +
              0.250f*yuv_at(image, int2(at)+int2(0,dy), p).yz +
              0.125f*yuv_at(image, int2(at)+int2(1,dy), p).yz;
    float2 code = round(clamp(512.0f + 896.0f*uv, 64.0f, 960.0f));
    chroma.write(float4(code * 64.0f / 65535.0f, 0.0f, 0.0f), at/2);
}
);
#undef REFERENCE_SOURCE

static void require(BOOL condition, NSString *reason) {
    if (!condition) {
        fprintf(stderr, "%s\n", reason.UTF8String);
        exit(1);
    }
}

static double seconds(void) { return NSProcessInfo.processInfo.systemUptime; }

static id<MTLComputePipelineState> pipeline(id<MTLDevice> device, NSString *source) {
    NSError *error = nil;
    MTLCompileOptions *options = [MTLCompileOptions new];
    if (@available(macOS 15.0, *)) {
        options.mathMode = MTLMathModeSafe;
        options.mathFloatingPointFunctions = MTLMathFloatingPointFunctionsPrecise;
    } else {
        require(NO, @"macOS 15 or later is required for production precise math");
    }
    id<MTLLibrary> library = [device newLibraryWithSource:source options:options error:&error];
    require(library != nil, error.localizedDescription ?: @"Metal library failed");
    id<MTLComputePipelineState> result = [device newComputePipelineStateWithFunction:
        [library newFunctionWithName:@"convert_p010"] error:&error];
    require(result != nil, error.localizedDescription ?: @"Metal pipeline failed");
    return result;
}

static id<MTLTexture> texture(id<MTLDevice> device, MTLPixelFormat format,
                              NSUInteger width, NSUInteger height) {
    MTLTextureDescriptor *desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
        width:width height:height mipmapped:NO];
    desc.storageMode = MTLStorageModeShared;
    desc.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
    id<MTLTexture> result = [device newTextureWithDescriptor:desc];
    require(result != nil, @"Off-screen texture allocation failed");
    return result;
}

static uint16_t halfBits(float value) {
    __fp16 half = value;
    uint16_t bits;
    memcpy(&bits, &half, sizeof(bits));
    return bits;
}

static NSData *pixels(NSUInteger width, NSUInteger height, unsigned pattern) {
    NSMutableData *data = [NSMutableData dataWithLength:width * height * 8];
    uint16_t *out = data.mutableBytes;
    static const float edges[] = {-4, -0.001f, 0, 0.000001f, 0.01799f, 0.018f,
        0.01801f, 0.04044f, 0.04045f, 0.04046f, 0.5f, 0.9995f, 1, 1.001f, 4, 65504};
    for (NSUInteger y = 0; y < height; ++y) {
        for (NSUInteger x = 0; x < width; ++x) {
            for (unsigned c = 0; c < 3; ++c) {
                uint16_t bits;
                if (pattern == 0) {
                    // Independent RGB gradients, including values near code rounding.
                    float t = c == 0 ? (float)x / MAX(width-1, 1ul) :
                        c == 1 ? (float)y / MAX(height-1, 1ul) :
                        (float)(x + 3*y) / MAX(width-1 + 3*(height-1), 1ul);
                    bits = halfBits(t);
                } else if (pattern == 1) {
                    // Impulses/checkers straddle both sides of every tile boundary.
                    BOOL edge = x % 16 == 15 || x % 16 == 0 || y % 16 == 15 || y % 16 == 0;
                    bits = halfBits(edge ? ((x+y+c) & 1 ? 1 : 0) : edges[(x+7*y+5*c) % 16]);
                } else if (pattern == 2) {
                    float value = edges[(x+17*y+3*c) % 16];
                    bits = halfBits(value);
                    // Adjacent half values on either side of each transfer breakpoint.
                    if (value > 0 && value < 1) {
                        if (x % 4 == 1) --bits;
                        if (x % 4 == 2) ++bits;
                    }
                } else {
                    // 257x257 covers every finite half bit pattern in every channel.
                    bits = (uint16_t)(x + width*y + c*21845);
                    if ((bits & 0x7c00) == 0x7c00) bits = 0;
                }
                out[(y*width+x)*4+c] = bits;
            }
            out[(y*width+x)*4+3] = halfBits(1);
        }
    }
    return data;
}

static NSDictionary *dispatch(id<MTLCommandQueue> queue, id<MTLComputePipelineState> state,
                              id<MTLTexture> input, id<MTLTexture> y, id<MTLTexture> uv,
                              unsigned color) {
    uint32_t parameters[] = {(uint32_t)input.width, (uint32_t)input.height, color, 0};
    double start = seconds();
    id<MTLCommandBuffer> command = [queue commandBuffer];
    id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
    require(encoder != nil, @"Compute encoder allocation failed");
    [encoder setComputePipelineState:state];
    [encoder setTexture:input atIndex:0];
    [encoder setTexture:y atIndex:1];
    [encoder setTexture:uv atIndex:2];
    [encoder setBytes:parameters length:sizeof(parameters) atIndex:0];
    [encoder dispatchThreadgroups:MTLSizeMake((input.width+15)/16, (input.height+15)/16, 1)
        threadsPerThreadgroup:MTLSizeMake(16,16,1)];
    [encoder endEncoding];
    [command commit];
    [command waitUntilCompleted];
    require(command.status == MTLCommandBufferStatusCompleted,
        command.error.localizedDescription ?: @"GPU command failed");
    return @{@"wall_ms": @((seconds()-start)*1000),
             @"gpu_ms": @((command.GPUEndTime-command.GPUStartTime)*1000)};
}

static NSData *bytes(id<MTLTexture> image, NSUInteger channels) {
    NSUInteger row = image.width * channels * sizeof(uint16_t);
    NSMutableData *data = [NSMutableData dataWithLength:row * image.height];
    [image getBytes:data.mutableBytes bytesPerRow:row fromRegion:
        MTLRegionMake2D(0, 0, image.width, image.height) mipmapLevel:0];
    return data;
}

static void equalBytes(NSData *old, NSData *current, NSString *context) {
    require(old.length == current.length, @"Plane byte count changed");
    if (![old isEqualToData:current]) {
        const uint16_t *a = old.bytes, *b = current.bytes;
        for (NSUInteger i = 0; i < old.length/2; ++i)
            if (a[i] != b[i]) require(NO, [NSString stringWithFormat:
                @"%@: word %lu: old=%u new=%u", context, (unsigned long)i, a[i], b[i]]);
    }
}

static CVPixelBufferRef pixelBuffer(NSUInteger width, NSUInteger height, OSType format) {
    CVPixelBufferRef result = NULL;
    NSDictionary *attributes = @{(id)kCVPixelBufferIOSurfacePropertiesKey: @{},
                                 (id)kCVPixelBufferMetalCompatibilityKey: @YES};
    require(CVPixelBufferCreate(NULL, width, height, format,
        (__bridge CFDictionaryRef)attributes, &result) == kCVReturnSuccess,
        @"Synthetic IOSurface allocation failed");
    return result;
}

static void equalPlane(CVPixelBufferRef buffer, size_t plane, NSData *expected, size_t row) {
    require(CVPixelBufferLockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly) == kCVReturnSuccess,
        @"Pixel buffer read lock failed");
    const uint8_t *actual = CVPixelBufferGetBaseAddressOfPlane(buffer, plane);
    size_t stride = CVPixelBufferGetBytesPerRowOfPlane(buffer, plane);
    for (size_t y = 0; y < expected.length/row; ++y) {
        equalBytes([NSData dataWithBytes:(const uint8_t *)expected.bytes+y*row length:row],
                   [NSData dataWithBytes:actual+y*stride length:row], @"CV plane");
    }
    CVPixelBufferUnlockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);
}

static NSDictionary *medianTimings(NSArray<NSDictionary *> *samples) {
    NSArray *gpu = [[samples valueForKey:@"gpu_ms"] sortedArrayUsingSelector:@selector(compare:)];
    NSArray *wall = [[samples valueForKey:@"wall_ms"] sortedArrayUsingSelector:@selector(compare:)];
    return @{@"gpu_ms": gpu[gpu.count/2], @"wall_ms": wall[wall.count/2]};
}

int main(void) {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        require(device != nil && device.hasUnifiedMemory, @"A native unified-memory Metal GPU is required");
        id<MTLCommandQueue> queue = [device newCommandQueue];
        double start = seconds();
        id<MTLComputePipelineState> old = pipeline(device, referenceShader);
        double oldInit = (seconds()-start)*1000;
        start = seconds();
        id<MTLComputePipelineState> current = pipeline(device, shader);
        double newInit = (seconds()-start)*1000;
        require(current.maxTotalThreadsPerThreadgroup >= 256, @"16x16 threadgroup unsupported");
        const NSUInteger sizes[][2] = {{1920,1080}, {1,1}, {1,17}, {17,1}, {2,2}, {15,15}, {16,16},
            {17,17}, {31,33}, {32,32}, {33,31}, {63,65}, {257,257}, {1921,1081}};
        NSUInteger cases = 0, planeBytes = 0;
        NSMutableDictionary *first = [NSMutableDictionary dictionary];
        for (NSUInteger s = 0; s < sizeof(sizes)/sizeof(*sizes); ++s) {
            @autoreleasepool {
                NSUInteger w = sizes[s][0], h = sizes[s][1];
                id<MTLTexture> input = texture(device, MTLPixelFormatRGBA16Float, w, h);
                id<MTLTexture> oldY = texture(device, MTLPixelFormatR16Unorm, w, h);
                id<MTLTexture> newY = texture(device, MTLPixelFormatR16Unorm, w, h);
                id<MTLTexture> oldUV = texture(device, MTLPixelFormatRG16Unorm, (w+1)/2, (h+1)/2);
                id<MTLTexture> newUV = texture(device, MTLPixelFormatRG16Unorm, (w+1)/2, (h+1)/2);
                for (unsigned pattern = 0; pattern < 4; ++pattern) {
                    NSData *data = pixels(w, h, pattern);
                    [input replaceRegion:MTLRegionMake2D(0,0,w,h) mipmapLevel:0
                        withBytes:data.bytes bytesPerRow:w*8];
                    for (unsigned color = 1; color <= 4; ++color) {
                        NSDictionary *a = dispatch(queue, old, input, oldY, oldUV, color);
                        NSDictionary *b = dispatch(queue, current, input, newY, newUV, color);
                        if (!cases) { first[@"old"] = a; first[@"new"] = b; }
                        NSString *context = [NSString stringWithFormat:@"%lux%lu pattern=%u color=%u",
                            (unsigned long)w, (unsigned long)h, pattern, color];
                        NSData *y = bytes(oldY, 1), *uv = bytes(oldUV, 2);
                        equalBytes(y, bytes(newY, 1), [context stringByAppendingString:@" luma"]);
                        equalBytes(uv, bytes(newUV, 2), [context stringByAppendingString:@" chroma"]);
                        planeBytes += y.length + uv.length;
                        ++cases;
                    }
                }
            }
        }

        // Exercise the actual CVMetalTextureCache path across several flush
        // intervals, with fresh buffers and changing extents. The enclosing pool
        // deliberately survives all conversions to catch autorelease retention.
        DPColorConverter *converter = [DPColorConverter new];
        require(converter != nil, @"Production converter initialization failed");
        for (unsigned frame = 0; frame < 125; ++frame) {
            NSUInteger w = frame & 1 ? 34 : 64, h = frame & 1 ? 18 : 32;
            unsigned color = frame % 4 + 1;
            NSData *data = pixels(w, h, frame % 4);
            CVPixelBufferRef source = pixelBuffer(w, h, kCVPixelFormatType_64RGBAHalf);
            CVPixelBufferRef output = pixelBuffer(w, h, kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange);
            require(CVPixelBufferLockBaseAddress(source, 0) == kCVReturnSuccess, @"Source lock failed");
            for (NSUInteger y = 0; y < h; ++y)
                memcpy((uint8_t *)CVPixelBufferGetBaseAddress(source)+y*CVPixelBufferGetBytesPerRow(source),
                       (const uint8_t *)data.bytes+y*w*8, w*8);
            CVPixelBufferUnlockBaseAddress(source, 0);
            require([converter convert:source toP010:output sourceColor:(DPColorSource)color],
                @"Production CV conversion failed");
            id<MTLTexture> input = texture(device, MTLPixelFormatRGBA16Float, w, h);
            id<MTLTexture> y = texture(device, MTLPixelFormatR16Unorm, w, h);
            id<MTLTexture> uv = texture(device, MTLPixelFormatRG16Unorm, w/2, h/2);
            [input replaceRegion:MTLRegionMake2D(0,0,w,h) mipmapLevel:0 withBytes:data.bytes bytesPerRow:w*8];
            dispatch(queue, old, input, y, uv, color);
            equalPlane(output, 0, bytes(y,1), w*2);
            equalPlane(output, 1, bytes(uv,2), w*2);
            CFRelease(source); CFRelease(output);
            require(converter.cacheUses == (frame+1)%60, @"Cache flush interval changed");
        }

        NSMutableArray *timings = [NSMutableArray array];
        for (NSNumber *width in @[@1920, @3840]) {
            @autoreleasepool {
                NSUInteger w = width.unsignedIntegerValue, h = w*9/16;
                id<MTLTexture> input = texture(device, MTLPixelFormatRGBA16Float, w, h);
                NSData *data = pixels(w, h, 0);
                [input replaceRegion:MTLRegionMake2D(0,0,w,h) mipmapLevel:0 withBytes:data.bytes bytesPerRow:w*8];
                id<MTLTexture> y = texture(device, MTLPixelFormatR16Unorm, w, h);
                id<MTLTexture> uv = texture(device, MTLPixelFormatRG16Unorm, w/2, h/2);
                for (unsigned color = 1; color <= 4; ++color) {
                    NSMutableArray *a = [NSMutableArray array], *b = [NSMutableArray array];
                    for (unsigned i = 0; i < 24; ++i) {
                        @autoreleasepool {
                            NSDictionary *x, *z;
                            if (i & 1) {
                                z = dispatch(queue, current, input, y, uv, color);
                                x = dispatch(queue, old, input, y, uv, color);
                            } else {
                                x = dispatch(queue, old, input, y, uv, color);
                                z = dispatch(queue, current, input, y, uv, color);
                            }
                            if (i >= 4) { [a addObject:x]; [b addObject:z]; }
                        }
                    }
                    [timings addObject:@{@"width": @(w), @"height": @(h), @"source": @(color),
                        @"old": medianTimings(a), @"new": medianTimings(b)}];
                }
            }
        }
        NSDictionary *report = @{@"scope": @"offline synthetic Metal/CV buffers; no display or capture",
            @"status": @"pass", @"gpu": device.name, @"byte_exact_cases": @(cases),
            @"compared_plane_bytes": @(planeBytes), @"cv_cache_frames": @125,
            @"pipeline_init_ms": @{@"old": @(oldInit), @"new": @(newInit)},
            @"first_dispatch": first, @"steady_state_medians": timings,
            @"timing_note": @"Alternating old/new, four warmups, twenty samples; compiler/driver caches may be warm"};
        NSData *json = [NSJSONSerialization dataWithJSONObject:report options:NSJSONWritingPrettyPrinted error:NULL];
        require(json != nil, @"JSON serialization failed");
        fwrite(json.bytes, 1, json.length, stdout);
        fputc('\n', stdout);
    }
    return 0;
}
