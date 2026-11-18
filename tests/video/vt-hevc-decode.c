// SPDX-License-Identifier: GPL-3.0-or-later
// Use the production probe's actual hardware-only decode, with a pixel observer.
#include "app/streaming/video/ffmpeg-renderers/vt-hardware.c"
#include <stdio.h>
#include <stdlib.h>

struct writer { FILE *file; OSType type; bool failed; unsigned frames; };
static void write_pixels(void *context, void *source, OSStatus status, VTDecodeInfoFlags flags,
                         CVImageBufferRef image, CMTime pts, CMTime duration) {
    (void)source; (void)flags; (void)pts; (void)duration;
    struct writer *writer = context;
    if (status != noErr || !image || CVPixelBufferGetPixelFormatType(image) != writer->type ||
        CVPixelBufferLockBaseAddress(image, kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess) {
        writer->failed = true; return;
    }
    bool ten = writer->type == kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange;
    size_t width = CVPixelBufferGetWidth(image), height = CVPixelBufferGetHeight(image);
    for (unsigned plane = 0; plane < 3; ++plane) {
        size_t physical = plane ? 1 : 0;
        size_t rows = plane ? height/2 : height, columns = plane ? width/2 : width;
        for (size_t y = 0; y < rows; ++y) {
            const uint8_t *row = (const uint8_t *)CVPixelBufferGetBaseAddressOfPlane(image, physical) + y * CVPixelBufferGetBytesPerRowOfPlane(image, physical);
            for (size_t x = 0; x < columns; ++x) {
                size_t index = plane ? 2*x + plane-1 : x;
                if (ten) {
                    uint16_t value = ((const uint16_t *)row)[index] >> 6;
                    if (fwrite(&value, 2, 1, writer->file) != 1) writer->failed = true;
                } else if (fwrite(row + index, 1, 1, writer->file) != 1) writer->failed = true;
            }
        }
    }
    CVPixelBufferUnlockBaseAddress(image, kCVPixelBufferLock_ReadOnly);
    ++writer->frames;
}
int main(int argc, char **argv) {
    if (argc != 4) return 2;
    FILE *input = fopen(argv[1], "rb");
    if (!input || fseek(input, 0, SEEK_END)) return 2;
    long length = ftell(input);
    if (length <= 0 || length > 8*1024*1024 || fseek(input, 0, SEEK_SET)) { fclose(input); return 2; }
    uint8_t *bytes = malloc((size_t)length);
    if (!bytes || fread(bytes, 1, (size_t)length, input) != (size_t)length) { free(bytes); fclose(input); return 2; }
    fclose(input);
    struct writer writer = {fopen(argv[2], "wb"), atoi(argv[3]) == 10 ? kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange : kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange, false, 0};
    if (!writer.file) { free(bytes); return 2; }
    bool hardware = decode_hardware_image(bytes, (size_t)length, writer.type, write_pixels, &writer);
    fclose(writer.file); free(bytes);
    if (!hardware || writer.failed || writer.frames != 1) return 1;
    puts("PASS: native VT first-frame pixels, hardware required and runtime hardware property verified");
    return 0;
}
