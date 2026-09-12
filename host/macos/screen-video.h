// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#import "src/platform/macos/av_video.h"

// ScreenCaptureKit for BGRA/NV12; preserve the legacy P010 capture path.
@interface DPScreenVideo : AVVideo
- (BOOL)captureFailed:(dispatch_semaphore_t)signal;
@end
