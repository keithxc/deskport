// Synthetic VideoToolbox probe for the DeskPort live-rate adapter.
// Uses the same prebuilt FFmpeg as the macOS host; no capture or input.
#include "host/macos/vtbitrate.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string_view>
extern "C" {
#include <libavutil/opt.h>
}

namespace {
constexpr int W = 2560, H = 1440, FPS = 60, FRAMES = 540;

// Fine "text-like" detail; seed changes model a full-window content switch.
bool scrolling = false, noisy = false;
void paint(AVFrame* f, std::uint32_t seed, int tick) {
    const int shift = scrolling ? tick * 4 : 0; // Continuous scroll keeps the target binding.
    for (int yy = 0; yy < H; ++yy) {
        const int y = (yy + shift) % H;
        auto* row = f->data[0] + yy * f->linesize[0];
        for (int x = 0; x < W; x += 8) {
            std::uint32_t h = (std::uint32_t(y) * 73856093u) ^ (std::uint32_t(x) * 19349663u) ^ (seed * 83492791u);
            h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
            const std::uint8_t v = h & 0x80 ? 230 : 30;
            std::fill(row + x, row + x + 8, (y & 15) < 11 ? v : 128);
        }
    }
    for (int y = 0; y < H / 2; ++y) std::fill(f->data[1] + y * f->linesize[1], f->data[1] + y * f->linesize[1] + W, 128);
    if (noisy) { // Video-like region that changes completely every frame.
        std::uint32_t h = 2166136261u ^ std::uint32_t(tick);
        for (int y = 400; y < 760; ++y) for (int x = 800; x < 1440; ++x) {
            h ^= h << 13; h ^= h >> 17; h ^= h << 5;
            f->data[0][y * f->linesize[0] + x] = std::uint8_t(h);
        }
    }
    const int x0 = (tick * 7) % (W - 40); // Small moving cursor-sized change.
    for (int y = 100; y < 120; ++y) std::fill(f->data[0] + y * f->linesize[0] + x0, f->data[0] + y * f->linesize[0] + x0 + 40, 255);
}

struct Result {
    std::vector<int> sizes = std::vector<int>(FRAMES);
    int packets = 0, decoded = 0, keys = 0;
    bool applied = false, hardware = false;
};

Result run(const char* codecName, bool live, int startKbps, int lowKbps) {
    Result out;
    const AVCodec* codec = avcodec_find_encoder_by_name(codecName);
    if (!codec) { std::fprintf(stderr, "missing %s\n", codecName); std::exit(2); }
    AVCodecContext* ctx = avcodec_alloc_context3(codec);
    ctx->width = W; ctx->height = H; ctx->pix_fmt = AV_PIX_FMT_NV12;
    ctx->time_base = {1, FPS}; ctx->framerate = {FPS, 1};
    ctx->gop_size = INT16_MAX; ctx->keyint_min = INT16_MAX; ctx->max_b_frames = 0;
    ctx->bit_rate = ctx->rc_max_rate = std::int64_t(startKbps) * 1000;
    ctx->flags |= AV_CODEC_FLAG_CLOSED_GOP | AV_CODEC_FLAG_LOW_DELAY;
    AVDictionary* options = nullptr; // Mirrors the host's VideoToolbox options.
    av_dict_set(&options, "realtime", "1", 0);
    av_dict_set(&options, "allow_sw", "0", 0);
    av_dict_set(&options, "require_sw", "0", 0);
    av_dict_set(&options, "prio_speed", "1", 0);
    if (std::string_view(codecName) == "hevc_videotoolbox") av_dict_set(&options, "max_ref_frames", "1", 0);
    if (avcodec_open2(ctx, codec, &options)) { std::fprintf(stderr, "open failed\n"); std::exit(2); }
    av_dict_free(&options);
    auto* session = deskport::vt::session(ctx);
    CFTypeRef hardware = nullptr;
    // allow_sw=0 requires a hardware encoder in the pinned FFmpeg adapter.
    out.hardware = true;
    const auto hwStatus = session ? VTSessionCopyProperty(session, kVTCompressionPropertyKey_UsingHardwareAcceleratedVideoEncoder, nullptr, &hardware) : -1;
    if (hwStatus == noErr && hardware) {
        out.hardware = CFEqual(hardware, kCFBooleanTrue);
        CFRelease(hardware);
    }
    std::fprintf(stderr, "hardware_required=1 hardware_property_status=%d\n", int(hwStatus));
    if (live) {
        if (!session) { std::fprintf(stderr, "session accessor rejected encoder\n"); std::exit(3); }
        out.applied = deskport::vt::apply(session, std::int64_t(startKbps) * 1000, 0, ctx->rc_max_rate);

    }
    AVFrame* frame = av_frame_alloc();
    frame->format = ctx->pix_fmt; frame->width = W; frame->height = H;
    av_frame_get_buffer(frame, 64);
    AVPacket* packet = av_packet_alloc();
    const AVCodec* decoderCodec = avcodec_find_decoder(ctx->codec_id);
    // The pinned host archive may be encoder-only. Save Annex B for an
    // independent decoder when no in-process decoder was built into it.
    AVCodecContext* decoder = decoderCodec ? avcodec_alloc_context3(decoderCodec) : nullptr;
    if (decoder && avcodec_open2(decoder, decoderCodec, nullptr)) std::exit(4);
    if (!decoder) out.decoded = -1;
    AVFrame* decoded = av_frame_alloc();
    FILE* stream = nullptr;
    if (const char* prefix = std::getenv("DP_RATE_OUTPUT")) {
        const auto path = std::string(prefix) + (live ? ".live" : ".baseline") + (ctx->codec_id == AV_CODEC_ID_H264 ? ".h264" : ".hevc");
        stream = std::fopen(path.c_str(), "wb");
        if (!stream) std::exit(5);
    }
    auto drain = [&] {
        while (avcodec_receive_packet(ctx, packet) == 0) {
            ++out.packets;
            out.keys += !!(packet->flags & AV_PKT_FLAG_KEY);
            if (packet->pts < 0 || packet->pts >= FRAMES) std::exit(6);
            out.sizes[packet->pts] += packet->size;
            if (stream) std::fwrite(packet->data, 1, packet->size, stream);
            if (decoder && avcodec_send_packet(decoder, packet)) std::exit(7);
            while (decoder && avcodec_receive_frame(decoder, decoded) == 0) {
                if (decoded->width != W || decoded->height != H) std::exit(8);
                ++out.decoded;
                av_frame_unref(decoded);
            }
            av_packet_unref(packet);
        }
    };
    for (int i = 0; i < FRAMES; ++i) {
        if (live && i == 180) {
            const char* qp = std::getenv("DP_MINQP");
            out.applied &= deskport::vt::apply(session, std::int64_t(lowKbps) * 1000, qp ? std::atoi(qp) : 0, std::int64_t(startKbps) * 1000);
        }
        if (live && i == 360)
            out.applied &= deskport::vt::apply(session, std::int64_t(startKbps) * 1000, 0, std::int64_t(lowKbps) * 1000);
        av_frame_make_writable(frame);
        // Full content switches at 60, 120, 240 and 300; small motion otherwise.
        const std::uint32_t seed = 7 + (i >= 60) + (i >= 120) + (i >= 240) + (i >= 300);
        paint(frame, seed, i);
        frame->pts = i;
        if (i == 0) frame->pict_type = AV_PICTURE_TYPE_I; else frame->pict_type = AV_PICTURE_TYPE_NONE;
        if (avcodec_send_frame(ctx, frame)) { std::fprintf(stderr, "send failed\n"); std::exit(2); }
        drain();
    }
    avcodec_send_frame(ctx, nullptr);
    drain();
    if (decoder) avcodec_send_packet(decoder, nullptr);
    while (decoder && avcodec_receive_frame(decoder, decoded) == 0) { ++out.decoded; av_frame_unref(decoded); }
    av_frame_free(&decoded); avcodec_free_context(&decoder);
    if (stream) std::fclose(stream);
    av_packet_free(&packet); av_frame_free(&frame); avcodec_free_context(&ctx);
    return out;
}

void report(const char* label, const Result& r) {
    auto window = [&](int a, int b, int& peak) {
        long long bytes = 0; peak = 0;
        for (int i = a; i < std::min<int>(b, r.sizes.size()); ++i) { bytes += r.sizes[i]; peak = std::max(peak, r.sizes[i]); }
        return bytes * 8.0 * FPS / (b - a) / 1e6;
    };
    int p1, p2;
    const double before = window(1, 180, p1), after = window(240, 360, p2);
    int recoveredPeak = 0;
    const double recovered = window(420, FRAMES, recoveredPeak);
    int cuts[4] = {};
    for (int k = 0; k < 4 && k < 4; ++k) { const int at[] = {60, 120, 240, 300}; for (int i = at[k] - 3; i < at[k] + 3 && i < int(r.sizes.size()); ++i) cuts[k] = std::max(cuts[k], r.sizes[i]); }
    std::printf("%s cut_frames=%d,%d,%d,%d ", label, cuts[0], cuts[1], cuts[2], cuts[3]);
    std::printf("%s packets=%d decoded=%d dropped=%d keys=%d first=%d before_mbps=%.3f peak_before=%d low_mbps=%.3f peak_low=%d recovered_mbps=%.3f applied=%d hardware=%d\n",
                label, r.packets, r.decoded, FRAMES - r.packets, r.keys, r.sizes[0], before, p1, after, p2, recovered, r.applied, r.hardware);
}
}

int main(int argc, char** argv) {
    const char* codec = argc > 1 ? argv[1] : "hevc_videotoolbox";
    scrolling = argc > 2 && std::string_view(argv[2]) == "scroll";
    noisy = argc > 2 && std::string_view(argv[2]) == "noise";
    const int start = argc > 3 ? std::atoi(argv[3]) : 15000, low = 3000;
    const auto base = run(codec, false, start, low);
    const auto live = run(codec, true, start, low);
    report("baseline", base);
    report("live", live);
    int peakLive = 0;
    for (int i = 181; i < int(live.sizes.size()); ++i) peakLive = std::max(peakLive, live.sizes[i]);
    std::printf("peak_after_lowering=%d\n", peakLive);
    return live.applied && live.hardware && live.packets > 0 && (live.decoded == live.packets || (live.decoded == -1 && std::getenv("DP_RATE_OUTPUT"))) &&
        (base.decoded == base.packets || (base.decoded == -1 && std::getenv("DP_RATE_OUTPUT"))) ? 0 : 1;
}
