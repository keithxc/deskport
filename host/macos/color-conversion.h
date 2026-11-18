// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#import <Foundation/Foundation.h>
#import <CoreVideo/CoreVideo.h>

typedef NS_ENUM(uint32_t, DPColorSource) {
    DPColorSourceUnsupported = 0,
    DPColorSourceSRGB = 1,
    DPColorSourceP3PQ = 2,
    DPColorSource2020PQ = 3,
    DPColorSourceLinear709 = 4
};
@interface DPColorConverter : NSObject
- (BOOL)convert:(CVPixelBufferRef)source toP010:(CVPixelBufferRef)destination sourceColor:(DPColorSource)color;
@end
