// Synthetic VideoToolbox final-picture regression. No capture, host or input.
#include "host/macos/vtlatency.h"
#include "host/macos/vtbitrate.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

extern "C" {
#include <libavutil/opt.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_videotoolbox.h>
}

namespace {
constexpr int Width = 2560, Height = 1440, Frames = 12;

bool run(const char* name, bool noDelay, bool lowLatency = false, bool sparse = false) {
    const AVCodec* codec = avcodec_find_encoder_by_name(name);
    AVCodecContext* ctx = avcodec_alloc_context3(codec);
    if (!codec || !ctx) return false;
    ctx->width = Width; ctx->height = Height; ctx->pix_fmt = AV_PIX_FMT_VIDEOTOOLBOX;
    AVBufferRef* device = nullptr;
    if (av_hwdevice_ctx_create(&device, AV_HWDEVICE_TYPE_VIDEOTOOLBOX, nullptr, nullptr, 0) < 0) return false;
    auto frames = av_hwframe_ctx_alloc(device);
    auto framesCtx = reinterpret_cast<AVHWFramesContext*>(frames->data);
    framesCtx->format = AV_PIX_FMT_VIDEOTOOLBOX; framesCtx->sw_format = AV_PIX_FMT_NV12;
    framesCtx->width = Width; framesCtx->height = Height;
    if (av_hwframe_ctx_init(frames) < 0) return false;
    ctx->hw_frames_ctx = av_buffer_ref(frames);
    ctx->time_base = {1, 60}; ctx->framerate = {60, 1};
    ctx->gop_size = 32767; ctx->max_b_frames = 0;
    ctx->bit_rate = ctx->rc_max_rate = 15000000;
    ctx->flags |= AV_CODEC_FLAG_CLOSED_GOP;
    if (lowLatency) ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
    AVDictionary* options = nullptr;
    av_dict_set(&options, "realtime", "1", 0);
    av_dict_set(&options, "prio_speed", "1", 0);
    if (!std::strcmp(name, "hevc_videotoolbox")) av_dict_set(&options, "max_ref_frames", "1", 0);
    const int opened = avcodec_open2(ctx, codec, &options);
    av_dict_free(&options);
    if (opened < 0) { avcodec_free_context(&ctx); return false; }
    auto vt = deskport::vt::session(ctx);
    if (!vt) {
        std::fprintf(stderr, "%s native session accessor failed\n", name);
        avcodec_free_context(&ctx); return false;
    }
    AVFrame* frame = av_frame_alloc();
    AVPacket* packet = av_packet_alloc();
    frame->format = ctx->pix_fmt; frame->width = Width; frame->height = Height;
    bool ok = true;
    int currentPictures = 0;
    long long maxUs = 0;
    for (int i = 0; ok && i < Frames; ++i) {
        av_frame_unref(frame);
        ok = av_hwframe_get_buffer(frames, frame, 0) >= 0;
        if (!ok) break;
        auto buffer = reinterpret_cast<CVPixelBufferRef>(frame->data[3]);
        CVPixelBufferLockBaseAddress(buffer, 0);
        for (int y = 0; y < Height; ++y)
            std::memset(static_cast<unsigned char*>(CVPixelBufferGetBaseAddressOfPlane(buffer, 0)) + y * CVPixelBufferGetBytesPerRowOfPlane(buffer, 0), (y / 12 + i / 4) % 2 ? 220 : 30, Width);
        for (int y = 0; y < Height / 2; ++y)
            std::memset(static_cast<unsigned char*>(CVPixelBufferGetBaseAddressOfPlane(buffer, 1)) + y * CVPixelBufferGetBytesPerRowOfPlane(buffer, 1), 128, Width);
        CVPixelBufferUnlockBaseAddress(buffer, 0);
        frame->pts = sparse ? i * 300 : i;
        frame->flags = i == 0 || i == 8 ? AV_FRAME_FLAG_KEY : 0;
        const auto start = std::chrono::steady_clock::now();
        deskport::vt::ScopedFrameCompletion completion(noDelay);
        ok = avcodec_send_frame(ctx, frame) >= 0;
        bool current = false;
        int ret = 0;
        while (ok && (ret = avcodec_receive_packet(ctx, packet)) >= 0) {
            current = current || packet->pts == frame->pts;
            av_packet_unref(packet);
        }
        ok = ok && ret == AVERROR(EAGAIN);
        if (current) ++currentPictures;
        const auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
        maxUs = std::max(maxUs, static_cast<long long>(us));
        // There is deliberately no next frame or flush to deliver this picture.
        if (noDelay && !current) {
            std::fprintf(stderr, "%s withheld frame %d until another submission\n", name, i);
            ok = false;
        }
        // Sparse timestamps represent the five-second idle cadence; short real
        // pauses also exercise callback completion across separate submissions.
        if (sparse) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    std::printf("%s mode=%s low_latency=%d sparse=%d current=%d/%d max_encode_us=%lld\n", name,
                noDelay ? "scoped-completion" : "baseline", lowLatency, sparse, currentPictures, Frames, maxUs);
    av_packet_free(&packet); av_frame_free(&frame); avcodec_free_context(&ctx); av_buffer_unref(&frames); av_buffer_unref(&device);
    return ok;
}
}

int main() {
    if (deskport::vt::completeSubmittedFrame) return 1;
    {
        deskport::vt::ScopedFrameCompletion outer(true);
        { deskport::vt::ScopedFrameCompletion inner(false); if (deskport::vt::completeSubmittedFrame) return 1; }
        if (!deskport::vt::completeSubmittedFrame) return 1;
    }
    if (deskport::vt::completeSubmittedFrame) return 1;
    for (const char* name : {"h264_videotoolbox", "hevc_videotoolbox"}) {
        if (!run(name, false) || !run(name, true) || !run(name, true, true) || !run(name, true, true, true)) return 1;
    }
    return 0;
}
