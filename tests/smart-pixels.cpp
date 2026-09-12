#include "host/macos/pixelmatch.h"
#include <cassert>
#include <chrono>
#include <iostream>
int main() {
    for (auto format : {kCVPixelFormatType_32BGRA, kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange,
                        kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange}) {
        CVPixelBufferRef a{},b{};
        assert(CVPixelBufferCreate(nullptr,2560,1440,format,nullptr,&a)==kCVReturnSuccess);
        assert(CVPixelBufferCreate(nullptr,2560,1440,format,nullptr,&b)==kCVReturnSuccess);
        assert(CVPixelBufferLockBaseAddress(a,0)==kCVReturnSuccess);
        assert(CVPixelBufferLockBaseAddress(b,0)==kCVReturnSuccess);
        const auto planes = CVPixelBufferGetPlaneCount(a);
        for (size_t p=0; p < (planes ? planes : 1); ++p) {
            auto data = [&](CVPixelBufferRef v) { return static_cast<unsigned char*>(planes ? CVPixelBufferGetBaseAddressOfPlane(v,p) : CVPixelBufferGetBaseAddress(v)); };
            auto stride = [&](CVPixelBufferRef v) { return planes ? CVPixelBufferGetBytesPerRowOfPlane(v,p) : CVPixelBufferGetBytesPerRow(v); };
            const auto height=planes ? CVPixelBufferGetHeightOfPlane(a,p) : CVPixelBufferGetHeight(a);
            std::memset(data(a),42,stride(a)*height); std::memset(data(b),42,stride(b)*height);
        }
        assert(deskport::samePixels(a,b));
        // Both chroma and luma must detect a single-byte change at the last row.
        for(size_t p=0;p < (planes ? planes : 1);++p) {
            auto bytes=static_cast<unsigned char*>(planes ? CVPixelBufferGetBaseAddressOfPlane(b,p) : CVPixelBufferGetBaseAddress(b));
            auto stride=planes ? CVPixelBufferGetBytesPerRowOfPlane(b,p) : CVPixelBufferGetBytesPerRow(b);
            auto height=planes ? CVPixelBufferGetHeightOfPlane(b,p) : CVPixelBufferGetHeight(b);
            bytes[stride*(height-1)] ^= 1;assert(!deskport::samePixels(a,b));bytes[stride*(height-1)] ^= 1;
        }
        auto start=std::chrono::steady_clock::now();
        for(int n=0;n<600;++n) assert(deskport::samePixels(a,b));
        auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        std::cout << "format " << format << ": exact 1440p duplicate comparison " << ms/600 << " ms/frame\n";
        CVPixelBufferUnlockBaseAddress(a,0);CVPixelBufferUnlockBaseAddress(b,0);CFRelease(a);CFRelease(b);
    }
    std::cout << "PASS: native BGRA, NV12, P010 duplicate detection and single-byte luma/chroma changes\n";
}
