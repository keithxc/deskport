// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <CoreVideo/CoreVideo.h>
#include "../common/smartstream.h"
namespace deskport {
inline bool samePixels(CVPixelBufferRef a, CVPixelBufferRef b) {
      const auto format = CVPixelBufferGetPixelFormatType(a);
      if (format != CVPixelBufferGetPixelFormatType(b) ||
          CVPixelBufferGetWidth(a) != CVPixelBufferGetWidth(b) ||
          CVPixelBufferGetHeight(a) != CVPixelBufferGetHeight(b)) return false;
      if (format == kCVPixelFormatType_32BGRA) {
        return deskport::samePlane(static_cast<unsigned char *>(CVPixelBufferGetBaseAddress(a)), CVPixelBufferGetBytesPerRow(a),
            static_cast<unsigned char *>(CVPixelBufferGetBaseAddress(b)), CVPixelBufferGetBytesPerRow(b),
            CVPixelBufferGetWidth(a) * 4, CVPixelBufferGetHeight(a));
      }
      const bool ten = format == kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange;
      if ((!ten && format != kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange) ||
          CVPixelBufferGetPlaneCount(a) != 2 || CVPixelBufferGetPlaneCount(b) != 2) return false;
      for (size_t plane = 0; plane < 2; ++plane) {
        const auto width = CVPixelBufferGetWidthOfPlane(a, plane);
        const auto height = CVPixelBufferGetHeightOfPlane(a, plane);
        if (width != CVPixelBufferGetWidthOfPlane(b, plane) || height != CVPixelBufferGetHeightOfPlane(b, plane) ||
            !deskport::samePlane(static_cast<unsigned char *>(CVPixelBufferGetBaseAddressOfPlane(a, plane)), CVPixelBufferGetBytesPerRowOfPlane(a, plane),
                static_cast<unsigned char *>(CVPixelBufferGetBaseAddressOfPlane(b, plane)), CVPixelBufferGetBytesPerRowOfPlane(b, plane),
                width * (ten ? 2 : 1) * (plane ? 2 : 1), height)) return false;
      }
      return true;
    }

}
