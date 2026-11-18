// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#import "src/platform/macos/av_video.h"

// ScreenCaptureKit with explicit 10-bit color conversion and SDR downgrade.
@interface DPScreenVideo : AVVideo
@property(nonatomic, readonly) BOOL hdrAvailable;
// Set by the native adapter before probing; software/BGRA paths remain SDR.
@property(nonatomic) BOOL hdrCaptureAllowed;
- (BOOL)captureFailed:(dispatch_semaphore_t)signal;
@end
