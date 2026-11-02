// Synthetic resource-driven codec/policy regression. No display, capture or input.
// FrameCadence is the production synchronous policy; the macOS asynchronous
// mailbox/conversion loop must be validated separately with the same metrics.
#include "host/common/framecadence.h"
#include "host/macos/vtlatency.h"
#include "host/macos/vtbitrate.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
extern "C" {
#include <libavutil/hwcontext.h>
#include <libavutil/opt.h>
}

namespace {
struct Scene { std::string id; std::vector<unsigned char> pixels; };
struct Event { long long us; int scene; bool activity, recovery; };
struct Input { long long number, us; int scene, event; };
struct Encoder {
    AVCodecContext* ctx = nullptr;
    AVBufferRef* device = nullptr;
    AVBufferRef* frames = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* packet = nullptr;
    ~Encoder() {
        av_packet_free(&packet); av_frame_free(&frame); avcodec_free_context(&ctx);
        av_buffer_unref(&frames); av_buffer_unref(&device);
    }
};
void require(bool ok, const std::string& error) { if (!ok) throw std::runtime_error(error); }
std::vector<Scene> readScenes(const std::string& path, std::size_t bytes) {
    std::ifstream list(path);
    require(bool(list), "cannot open scene list");
    std::vector<Scene> scenes;
    std::string id, file;
    while (list >> id >> file) {
        std::ifstream input(file, std::ios::binary);
        require(bool(input), "cannot open NV12 scene: " + file);
        std::vector<unsigned char> pixels((std::istreambuf_iterator<char>(input)), {});
        require(pixels.size() == bytes, "invalid NV12 scene size: " + file);
        scenes.push_back({id, std::move(pixels)});
    }
    require(!scenes.empty(), "no scenes");
    return scenes;
}
std::vector<Event> readEvents(const std::string& path, int sceneCount) {
    std::ifstream input(path);
    std::vector<Event> events;
    Event event;
    while (input >> event.us >> event.scene >> event.activity >> event.recovery) {
        require(event.us >= 0 && event.scene >= 0 && event.scene < sceneCount,
                "invalid timeline event");
        require(events.empty() || event.us > events.back().us, "events must increase");
        events.push_back(event);
    }
    require(!events.empty() && events.front().us == 0, "timeline must start at zero");
    return events;
}
int run(int argc, char** argv) {
    require(argc == 13, "usage: runner WIDTH HEIGHT FPS KBPS BURST DURATION_MS sequential|elapsed PACE SCENES EVENTS OUTPUT_PREFIX SAVER");
    const int width = std::stoi(argv[1]), height = std::stoi(argv[2]), fps = std::stoi(argv[3]);
    const int kbps = std::stoi(argv[4]), burst = std::stoi(argv[5]), durationMs = std::stoi(argv[6]);
    const bool elapsed = std::string(argv[7]) == "elapsed", pace = std::stoi(argv[8]) != 0;
    const std::string prefix = argv[11];
    const bool saver = std::stoi(argv[12]) != 0;
    require(width > 0 && height > 0 && width % 4 == 0 && height % 4 == 0 && fps > 0,
            "invalid dimensions or frame rate");
    const auto scenes = readScenes(argv[9], std::size_t(width) * height * 3 / 2);
    const auto events = readEvents(argv[10], int(scenes.size()));
    Encoder encoder;
    const auto codec = avcodec_find_encoder_by_name("hevc_videotoolbox");
    require(codec, "missing pinned HEVC VideoToolbox encoder");
    encoder.ctx = avcodec_alloc_context3(codec);
    require(encoder.ctx, "cannot allocate encoder");
    auto ctx = encoder.ctx;
    ctx->width = width; ctx->height = height; ctx->pix_fmt = AV_PIX_FMT_VIDEOTOOLBOX;
    ctx->time_base = {1, fps}; ctx->framerate = {fps, 1};
    ctx->gop_size = ctx->keyint_min = 32767; ctx->max_b_frames = 0;
    ctx->bit_rate = ctx->rc_max_rate = std::int64_t(kbps) * 1000;
    ctx->flags |= AV_CODEC_FLAG_CLOSED_GOP | AV_CODEC_FLAG_LOW_DELAY;
    ctx->color_range = AVCOL_RANGE_MPEG; ctx->colorspace = AVCOL_SPC_BT709;
    ctx->color_primaries = AVCOL_PRI_BT709; ctx->color_trc = AVCOL_TRC_BT709;
    require(av_hwdevice_ctx_create(&encoder.device, AV_HWDEVICE_TYPE_VIDEOTOOLBOX,
                                   nullptr, nullptr, 0) >= 0, "cannot create VT device");
    encoder.frames = av_hwframe_ctx_alloc(encoder.device);
    require(encoder.frames, "cannot allocate hardware frame pool");
    auto pool = reinterpret_cast<AVHWFramesContext*>(encoder.frames->data);
    pool->format = AV_PIX_FMT_VIDEOTOOLBOX; pool->sw_format = AV_PIX_FMT_NV12;
    pool->width = width; pool->height = height;
    require(av_hwframe_ctx_init(encoder.frames) >= 0, "cannot initialize hardware frame pool");
    ctx->hw_frames_ctx = av_buffer_ref(encoder.frames);
    AVDictionary* options = nullptr;
    av_dict_set(&options, "realtime", "1", 0);
    av_dict_set(&options, "prio_speed", "1", 0);
    av_dict_set(&options, "max_ref_frames", "1", 0);
    const auto opened = avcodec_open2(ctx, codec, &options);
    av_dict_free(&options);
    require(opened >= 0, "cannot open encoder");
    require(deskport::vt::applyRate(deskport::vt::session(ctx), std::int64_t(kbps) * 1000, burst),
            "native rate adapter rejected settings");
    encoder.frame = av_frame_alloc(); encoder.packet = av_packet_alloc();
    require(encoder.frame && encoder.packet, "cannot allocate frame/packet");
    std::ofstream stream(prefix + ".hevc", std::ios::binary);
    std::ofstream admissions(prefix + ".admissions.csv"), packets(prefix + ".packets.csv");
    require(bool(stream) && bool(admissions) && bool(packets), "cannot open output files");
    admissions << "input,sim_us,event,scene,changed,recovery,cadence_floor,native_pts,bytes,packets,encode_us,completion_status\n";
    packets << "wire_index,input,sim_us,event,scene,native_pts,packet_pts,bytes,key,output_us,capture_us\n";
    deskport::FrameCadence cadence(fps);
    cadence.saver = saver;
    std::map<long long, Input> inputs;
    long long inputNumber = 0, wireIndex = 0, lastNativePts = -1, totalBytes = 0;
    int eventIndex = -1, sceneIndex = -1;
    const auto start = std::chrono::steady_clock::now();
    for (long long tick = 0; ; ++tick) {
        const auto nowUs = tick * 1000000 / fps;
        if (nowUs > std::int64_t(durationMs) * 1000) break;
        if (pace) std::this_thread::sleep_until(start + std::chrono::microseconds(nowUs));
        bool changed = false, recovery = inputNumber == 0;
        while (eventIndex + 1 < int(events.size()) && events[eventIndex + 1].us <= nowUs) {
            const auto& event = events[++eventIndex];
            changed = changed || sceneIndex < 0 || scenes[sceneIndex].pixels != scenes[event.scene].pixels;
            sceneIndex = event.scene;
            if (event.activity) cadence.boost = {nowUs / 1000 + 350, 60};
            recovery = recovery || event.recovery;
        }
        if (!cadence.admit(nowUs, changed, recovery)) continue;
        auto frame = encoder.frame;
        av_frame_unref(frame);
        require(av_hwframe_get_buffer(encoder.frames, frame, 0) >= 0, "cannot allocate hardware image");
        frame->color_range = ctx->color_range; frame->colorspace = ctx->colorspace;
        frame->color_primaries = ctx->color_primaries; frame->color_trc = ctx->color_trc;
        auto image = reinterpret_cast<CVPixelBufferRef>(frame->data[3]);
        require(CVPixelBufferLockBaseAddress(image, 0) == kCVReturnSuccess, "cannot lock pixel buffer");
        for (int plane = 0; plane < 2; ++plane) {
            auto dest = static_cast<unsigned char*>(CVPixelBufferGetBaseAddressOfPlane(image, plane));
            const auto stride = CVPixelBufferGetBytesPerRowOfPlane(image, plane);
            const auto source = scenes[sceneIndex].pixels.data() + (plane ? std::size_t(width) * height : 0);
            for (int y = 0; y < (plane ? height / 2 : height); ++y)
                std::memcpy(dest + y * stride, source + y * width, width);
        }
        CVPixelBufferUnlockBaseAddress(image, 0);
        frame->pts = elapsed ? std::max(lastNativePts + 1, (nowUs * fps + 500000) / 1000000) : inputNumber;
        lastNativePts = frame->pts;
        frame->flags = recovery ? AV_FRAME_FLAG_KEY : 0;
        frame->pict_type = recovery ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_NONE;
        inputs[frame->pts] = {inputNumber, nowUs, sceneIndex, eventIndex};
        int currentBytes = -1, packetCount = 0;
        const auto encodeStart = std::chrono::steady_clock::now();
        deskport::vt::ScopedFrameCompletion completion(true);
        require(avcodec_send_frame(ctx, frame) >= 0, "native submission failed");
        require(completion.status() == noErr, "native completion failed after successful submission");
        int ret;
        while ((ret = avcodec_receive_packet(ctx, encoder.packet)) >= 0) {
            require(completion.status() == noErr, "native completion failed while receiving");
            auto packet = encoder.packet;
            const auto found = inputs.find(packet->pts);
            require(found != inputs.end(), "output PTS has no source provenance");
            const auto& input = found->second;
            stream.write(reinterpret_cast<const char*>(packet->data), packet->size);
            packets << wireIndex++ << ',' << input.number << ',' << input.us << ',' << input.event << ','
                    << scenes[input.scene].id << ',' << found->first << ',' << packet->pts << ','
                    << packet->size << ',' << bool(packet->flags & AV_PKT_FLAG_KEY) << ','
                    << std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - start).count() << ',' << events[input.event].us << '\n';
            if (packet->pts == frame->pts) currentBytes = packet->size;
            totalBytes += packet->size; ++packetCount;
            av_packet_unref(packet);
        }
        require(ret == AVERROR(EAGAIN), "packet receive failed");
        cadence.encoded(currentBytes);
        const auto encodeUs = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - encodeStart).count();
        admissions << inputNumber << ',' << nowUs << ',' << eventIndex << ',' << scenes[sceneIndex].id << ','
                   << changed << ',' << recovery << ',' << cadence.floor(nowUs) << ',' << frame->pts << ','
                   << currentBytes << ',' << packetCount << ',' << encodeUs << ',' << completion.status() << '\n';
        ++inputNumber;
    }
    std::printf("inputs=%lld outputs=%lld bytes=%lld pts_mode=%s saver=%d\n", inputNumber, wireIndex,
                totalBytes, elapsed ? "elapsed" : "sequential", saver);
    return 0;
}
}
int main(int argc, char** argv) {
    try { return run(argc, argv); }
    catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 2; }
}
