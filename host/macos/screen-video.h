// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#import "src/platform/macos/av_video.h"

// Deferred samples own only conversion state, never a capture/queue/callback.
// The caller owns the returned sample; NULL means conversion failed.
#ifdef __cplusplus
extern "C" {
#endif
CMSampleBufferRef DPCopyCaptureSampleForEncoder(CMSampleBufferRef sample) CF_RETURNS_RETAINED;
// Process-local, asynchronous lifecycle check; never requests or converts a frame.
void DPNudgeCaptureLifecycle(void);
#ifdef __cplusplus
}
#endif

// ScreenCaptureKit with explicit 10-bit color conversion and SDR downgrade.
@interface DPScreenVideo : AVVideo
@property(nonatomic, readonly) BOOL hdrAvailable;
// Set by the native adapter before probing; software/BGRA paths remain SDR.
@property(nonatomic) BOOL hdrCaptureAllowed;
// Only the real running capture supplies this gate. Dummy probes stay eager.
- (dispatch_semaphore_t)capture:(FrameCallbackBlock)callback
             conversionPaused:(BOOL (^)(void))paused;
- (BOOL)captureFailed:(dispatch_semaphore_t)signal;
@end
