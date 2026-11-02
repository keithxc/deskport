#include "host/common/smartstream.h"
#include "host/common/encoderpolicy.h"
#include "host/common/inputactivity.h"
#include "host/common/framecadence.h"
#include "host/common/ratecontrol.h"
#include "host/macos/scroll-pacer.h"
#include "app/backend/streambudget.h"
#include "app/backend/trafficaccount.h"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <vector>
int main() {
    deskport::KeyframeRequests keys;
    keys.submitted(10, true);
    keys.submitted(11, false);
    assert(!keys.take(7)); // Delayed packet must not consume the new request.
    assert(keys.take(10));
    assert(!keys.take(10));
    assert(!keys.take(11));
    keys.submitted(20, true);
    keys.submitted(21, true);
    assert(keys.take(21)); // Match even when a backend reorders outputs.
    assert(keys.take(20));

    using namespace DeskPortStream;
    for (bool smart : {false, true}) for (bool qualified : {false, true})
        for (int mode : {0, 1, 2, 4}) for (int attempt : {0, 1}) {
            const auto rate = deskport::rateControl(smart, qualified, mode, attempt);
            const bool expected = smart && qualified && mode == 2 && attempt == 0;
            assert(rate.boundedVbr == expected && rate.retryOriginalOnFailure == expected);
        }
    deskport::EncodedWindow encoded, otherSession;
    assert(!encoded.add(1000, 1000, true));
    assert(!encoded.add(5999, 2000, false));
    assert(encoded.add(6000, 7000, false));
    assert(encoded.bytes == 10000 && encoded.packets == 3 && encoded.keys == 1);
    assert(encoded.maxPacketBytes == 7000 && encoded.mbps(6000) == 0.016);
    assert(otherSession.bytes == 0);
    encoded.reset(6000);
    assert(encoded.bytes == 0 && encoded.keys == 0 && encoded.maxPacketBytes == 0);
    assert(!encoded.add(6000, 100, false) && encoded.mbps(6000) == 0.0);

    // Automatic: resolution-aware budget; the saved manual bitrate is ignored.
    assert(automaticBitrate(2560,1440,60,false)==15000);
    assert(automaticBitrate(1280,720,30,false)==5000);
    assert(automaticBitrate(2560,1440,60,true)==30000);
    assert(automaticBitrate(5104,2720,60,false)==56489); // 5K workspace
    assert(automaticBitrate(7680,4320,60,false)==80000);
    assert(sessionBitrate(true,false,25000,5104,2720,60,false)==56489);
    // Manual keeps the saved value.
    assert(sessionBitrate(false,false,25000,5104,2720,60,false)==25000);
    // Office saver keeps high-resolution desktops at the same video ceiling,
    // while preserving an already lower manual choice.
    assert(dataSaverBitrate(56489)==1000 && dataSaverBitrate(15000)==1000 && dataSaverBitrate(5000)==1000);
    assert(sessionBitrate(true,true,25000,5104,2720,60,false)==1000);
    assert(sessionBitrate(true,true,25000,2560,1440,60,false)==1000);
    assert(sessionBitrate(true,true,25000,1920,1080,60,false)==1000);
    assert(sessionBitrate(false,true,60000,5104,2720,60,false)==1000);
    assert(sessionBitrate(false,true,500,5104,2720,60,false)==500);
    for (bool saver : {false, true}) for (bool audio : {false, true}) for (bool smart : {false, true}) {
        const auto media = sessionMediaSettings(saver, audio, smart);
        assert(media.remoteAudio == (audio && !saver));
        assert(media.smartStreaming == (smart || saver));
        // Computing a session override never mutates the original preferences.
        const auto restored = sessionMediaSettings(false, audio, smart);
        assert(restored.remoteAudio == audio && restored.smartStreaming == smart);
        const auto savingMedia = sessionMediaSettings(true, audio, smart);
        // Audio/cadence are launch-time options in both switch directions.
        assert(dataSaverNeedsReconnect(restored, savingMedia, true) == (audio || !smart));
        assert(dataSaverNeedsReconnect(savingMedia, restored, true) == (audio || !smart));
        assert(dataSaverNeedsReconnect(restored, savingMedia, false));
        assert(dataSaverNeedsReconnect(savingMedia, restored, false));
    }

    // Remote classification: on-link/private paths keep LAN behavior.
    auto v4 = [](unsigned a, unsigned b, unsigned c, unsigned d) {
        const unsigned char bytes[4] = {(unsigned char)a, (unsigned char)b, (unsigned char)c, (unsigned char)d};
        return deskport::remotePeer(bytes, 4);
    };
    assert(!v4(192,168,0,9) && !v4(10,1,2,3) && !v4(172,16,0,1) && !v4(172,31,255,1));
    assert(!v4(127,0,0,1) && !v4(169,254,1,1));
    assert(v4(172,32,0,1) && v4(100,64,0,9) && v4(202,104,119,10));
    auto v6 = [](std::initializer_list<unsigned> prefix, unsigned last) {
        unsigned char bytes[16]{}; int i = 0;
        for (auto b : prefix) bytes[i++] = (unsigned char)b;
        bytes[15] = (unsigned char)last;
        return deskport::remotePeer(bytes, 16);
    };
    assert(!v6({}, 1) && !v6({0xfe,0x80}, 5) && !v6({0xfd,0x12}, 5));
    assert(v6({0xfd,0x7a,0x11,0x5c,0xa1,0xe0}, 0x0e) && v6({0x24,0x08}, 1));
    assert(!v6({0,0,0,0,0,0,0,0,0,0,0xff,0xff,192,168,0}, 1));
    assert(v6({0,0,0,0,0,0,0,0,0,0,0xff,0xff,100,64,0}, 9));
    assert(!deskport::remotePeer(nullptr, 4) && !deskport::remotePeer((const unsigned char*)"abc", 3));

    // Remote rate: queueing delay or unrecoverable loss lowers the target,
    // quiet periods restore it gradually, bounds always hold.
    deskport::BitrateController rate(8000, 0);
    assert(rate.floorKbps() == 1000 && rate.targetKbps(0) == 8000 && !rate.constrained());
    for (int t = 0; t <= 4000; t += 500) rate.rtt(t, 330);
    assert(rate.baselineMs() == 330 && rate.targetKbps(4000) == 8000);
    rate.rtt(4500, 420); // One delayed sample is jitter, not congestion.
    rate.rtt(5000, 330);
    assert(rate.targetKbps(5000) == 8000);
    rate.rtt(5500, 420); rate.rtt(6000, 430);
    assert(rate.targetKbps(6000) == 5600);
    rate.rtt(6500, 440); rate.rtt(7000, 450);
    assert(rate.targetKbps(7000) == 5600); // Let the queue drain first.
    rate.rtt(8000, 450); // Still queued two seconds after the first decrease.
    assert(rate.targetKbps(8000) == 3920 && rate.constrained());
    for (int t = 8500; t < 13000; t += 500) rate.rtt(t, 330);
    assert(rate.targetKbps(12999) == 3920);
    assert(rate.targetKbps(13000) == 4320 && !rate.constrained());
    assert(rate.targetKbps(15999) == 4320); // One step per three seconds.
    for (int t = 16500; t < 80000; t += 3000) rate.targetKbps(t);
    assert(rate.targetKbps(80000) == 8000);
    for (int t = 90000; t < 200000; t += 2000) rate.loss(t);
    assert(rate.targetKbps(200000) == 1000); // Floor holds.
    deskport::BitrateController slow(2000, 0);
    assert(slow.floorKbps() == 1000);
    deskport::BitrateController tiny(600, 0);
    assert(tiny.floorKbps() == 600 && tiny.targetKbps(0) == 600);
    // A stale minimum expires, so a lasting route change becomes the baseline.
    deskport::BitrateController route(8000, 0);
    route.rtt(0, 20);
    for (int t = 40000; t < 41000; t += 500) route.rtt(t, 330);
    assert(route.baselineMs() == 330);
    deskport::BitrateController resumed(8000, 50000);
    resumed.resume(500); assert(resumed.targetKbps(50000) == 1000);
    resumed.resume(3000); assert(resumed.targetKbps(52999) == 3000 && resumed.targetKbps(53000) == 3400);
    resumed.resume(20000); assert(resumed.targetKbps(53000) == 8000);
    // Live 5G trace: one external delay spike that drains while the stream
    // sends ~1.2 Mbps. One cut, holds while RTT falls, later cuts are gentle.
    deskport::BitrateController cellular(20000, 0);
    for (int t = 0; t <= 4000; t += 500) cellular.rtt(t, 41);
    cellular.sent(1200);
    cellular.rtt(10000, 250); cellular.rtt(10500, 279);
    assert(cellular.targetKbps(10500) == 17000); // underused: 15%, not 30%
    cellular.rtt(12500, 205); cellular.rtt(13000, 200);
    assert(cellular.targetKbps(13000) == 17000); // draining: hold
    cellular.rtt(14500, 135); cellular.rtt(15000, 130);
    assert(cellular.targetKbps(15000) == 17000);
    cellular.rtt(16500, 98); cellular.rtt(17000, 96);
    assert(cellular.targetKbps(17000) == 14450); // 6.5 s later and still high
    deskport::BitrateController saturated(20000, 0);
    for (int t = 0; t <= 4000; t += 500) saturated.rtt(t, 41);
    saturated.sent(19000);
    saturated.rtt(10000, 250); saturated.rtt(10500, 279);
    assert(saturated.targetKbps(10500) == 14000); // our own queue: 30%
    saturated.rtt(12500, 300); saturated.rtt(13000, 310);
    assert(saturated.targetKbps(13000) == 9800); // still growing: cut again
    // Live viewer ceiling: scales the encoder target from the requested bitrate,
    // never above the negotiated rate; lifting it restores the full range.
    assert(deskport::ceilingPermille(12000, 56000) == 214);
    assert(deskport::ceilingPermille(56000, 56000) == 0 && deskport::ceilingPermille(90000, 56000) == 0);
    assert(deskport::ceilingPermille(0, 56000) == 0 && deskport::ceilingPermille(5000, 0) == 0);
    assert(deskport::applyPermille(44179, 214) == 9454 && deskport::applyPermille(44179, 0) == 44179);
    deskport::BitrateController capped(40000, 0);
    capped.setCeiling(9000);
    assert(capped.ceilingKbps() == 9000 && capped.targetKbps(0) == 9000 && capped.floorKbps() == 5000);
    capped.setCeiling(4000); // below the usual floor: the floor follows the ceiling
    assert(capped.floorKbps() == 4000 && capped.targetKbps(0) == 4000);
    capped.setCeiling(90000); // never above the negotiated rate
    assert(capped.ceilingKbps() == 40000 && capped.floorKbps() == 5000 && capped.targetKbps(0) == 5000);
    for (int t = 0; t < 120000; t += 3000) capped.targetKbps(t);
    assert(capped.targetKbps(120000) == 40000);
    deskport::KeyframeGate gate;
    assert(gate.allow(1000, 50));                 // first request
    assert(!gate.allow(1100, 50) && !gate.allow(1249, 50)); // 250 ms minimum
    assert(gate.allow(1250, 50));
    assert(!gate.allow(1700, 300) && gate.allow(1850, 300)); // 2 x RTT
    assert(!gate.allow(2800, 900) && gate.allow(2850, 900)); // capped at 1 s
    assert(gate.coalesced == 4);
    deskport::RateMemory memory;
    assert(memory.resumeFor(8000, 0) == 0);
    memory.store(8000, 3000, 1000);
    assert(memory.resumeFor(8000, 301000) == 3000);
    assert(memory.resumeFor(8000, 301001) == 0 && memory.resumeFor(15000, 2000) == 0);
    assert(deskport::remotePacketsPerMs(3000, 1040) == 2);   // 20 Mbps floor
    assert(deskport::remotePacketsPerMs(15000, 1040) == 7);  // 4x target
    assert(deskport::remotePacketsPerMs(1, 1 << 20) == 1);

    // Scroll pacing: bounded steps, total and direction preserved.
    assert(deskport::scrollStepPoints(1.0) == 40 && deskport::scrollStepPoints(2.0) == 20);
    assert(deskport::scrollStepPoints(0.0) == 40 && deskport::scrollStepPoints(16.0) == 4);
    deskport::ScrollPacer pacer;
    pacer.add(-50, 0);
    auto step = pacer.take(20);
    assert(step.vertical == -20 && step.horizontal == 0 && pacer.pending());
    pacer.take(20);
    step = pacer.take(20);
    assert(step.vertical == -10 && !pacer.pending());
    assert(pacer.take(20).empty());
    pacer.add(250, -90);
    int v = 0, h = 0, steps = 0;
    while (pacer.pending()) {
        step = pacer.take(40); ++steps;
        assert(std::abs(step.vertical) <= 40 && std::abs(step.horizontal) <= 40);
        v += step.vertical; h += step.horizontal;
    }
    assert(v == 250 && h == -90 && steps == 7);
    pacer.add(120, 0); pacer.take(40); pacer.add(-80, 0); // reversal cancels pending distance
    assert(!pacer.pending());
    pacer.add(30, 0); pacer.clear(); assert(!pacer.pending());

    // Daily traffic: cumulative counters per session, new sessions start at
    // zero, midnight starts a new day, levels at 70/85/100%.
    DeskPortTraffic::DailyUsage today;
    assert(today.account("2026-10-15", "a", 100) == 100);
    assert(today.account("2026-10-15", "a", 250) == 150);
    assert(today.account("2026-10-15", "b", 50) == 50 && today.used == 300);
    assert(today.account("2026-10-15", "a", 250) == 0);
    assert(today.account("2026-10-15", "a", 30) == 30);   // restarted session
    assert(today.account("2026-10-15", "a", -5) == 0 && today.used == 330);
    today.forget("b");
    assert(today.account("2026-10-15", "b", 70) == 70 && today.used == 400);
    assert(today.account("2026-10-16", "a", 40) == 10 && today.used == 10 && today.day == "2026-10-16");
    today.reset(); assert(today.used == 0);
    using DeskPortTraffic::Level;
    assert(DeskPortTraffic::level(699, 1000) == Level::Normal);
    assert(DeskPortTraffic::level(700, 1000) == Level::Warning);
    assert(DeskPortTraffic::level(849, 1000) == Level::Warning);
    assert(DeskPortTraffic::level(850, 1000) == Level::Critical);
    assert(DeskPortTraffic::level(1000, 1000) == Level::Exceeded);
    assert(DeskPortTraffic::level(5, 0) == Level::Normal);
    assert(DeskPortTraffic::level(8000000000LL, 10000000000LL) == Level::Warning);
    assert(DeskPortTraffic::level(9000000000LL, 10000000000LL) == Level::Critical);
    // Distinct strides/padding; a one-byte change anywhere must be detected.
    std::vector<unsigned char> a(128*80,42), b(160*80,42);
    for(int y=0;y<80;++y) b[y*160+120]=99;
    assert(deskport::samePlane(a.data(),128,b.data(),160,120,80));
    for(int y=0;y<80;++y) for(int x=0;x<120;++x) {
        b[y*160+x]^=1;
        assert(!deskport::samePlane(a.data(),128,b.data(),160,120,80));
        b[y*160+x]^=1;
    }
    assert(!deskport::samePlane(nullptr,128,b.data(),160,120,80));
    assert(!deskport::samePlane(a.data(),100,b.data(),160,120,80));
    unsigned char fec[22]{}; fec[11]=100;fec[13]=20;fec[15]=90;fec[17]=5;fec[20]=1;
    assert(deskport::unrecoverableFec(fec,21));
    for(unsigned n=0;n<21;++n) assert(!deskport::unrecoverableFec(fec,n));
    assert(!deskport::unrecoverableFec(fec,22));
    fec[17]=10;assert(!deskport::unrecoverableFec(fec,21)); // repaired
    fec[17]=21;assert(!deskport::unrecoverableFec(fec,21)); // malformed
    deskport::StreamPolicy p(60);
    for(int n=0;n<100;++n)p.loss(8000+n); // one burst, no change
    assert(p.frameRate(8200)==60);
    p.loss(9500);p.loss(11000);assert(p.frameRate(11000)==30);
    int admitted=0;
    for(int n=0;n<600;++n) admitted+=p.admit(12000000LL+n*1000000LL/60);
    assert(admitted>=299 && admitted<=302);
    // A denied update stays pending until the next slot; no new capture is required.
    assert(!p.admit(21984000));
    assert(p.admit(22000000,true)); // recovery never waits
    assert(p.frameRate(40999)==30);assert(p.frameRate(42000)==60);
    deskport::StreamPolicy idle(60);
    idle.loss(8000);idle.loss(9500);idle.loss(11000);
    assert(idle.frameRate(100000)==30); // idle must not count as a healthy trial
    idle.loss(101000);idle.loss(102500);idle.loss(104000);
    assert(idle.frameRate(104000)==15);
    idle.loss(113000);idle.loss(114500);idle.loss(116000);
    assert(idle.frameRate(116000)==15);
    deskport::StreamPolicy low(10);
    low.loss(8000);low.loss(9500);low.loss(11000);assert(low.frameRate(11000)==10);
    // Input packets are classified without unaligned reads or retaining key data.
    auto packet = [](unsigned type, unsigned size) {
        std::vector<unsigned char> p(size);
        p[3] = size - 4;
        for (int i=0;i<4;++i) p[4+i] = (type >> (8*i)) & 255;
        return p;
    };
    deskport::InputActivity input;
    auto key = packet(3,14);
    auto boost = input.observe(key.data(),key.size(),1000);
    assert(deskport::activityFrameRate(boost,1000,120)==60);
    assert(deskport::activityFrameRate(boost,1000,15)==15); // loss cap wins
    assert(deskport::activityFrameRate(boost,1350,60)==1);
    input.observe(key.data(),key.size(),1100);
    boost=input.observe(key.data(),key.size(),1200);
    assert(boost.untilMs==1900); // continued typing holds activity
    assert(deskport::activityFrameRate(boost,1900,60)==1);
    for(unsigned n=0;n<14;++n) assert(input.observe(key.data(),n,1300).untilMs==0);
    key[3]=11;assert(input.observe(key.data(),key.size(),1300).untilMs==0);
    deskport::InputActivity pointer;
    auto move=packet(7,12);
    for(int i=0;i<1000;++i) {
        move[8]=(i%2)?255:0;move[9]=(i%2)?255:1;
        assert(pointer.observe(move.data(),move.size(),i*10).untilMs==0);
    }
    move[8]=0;move[9]=8;
    boost=pointer.observe(move.data(),move.size(),11000);
    assert(boost.fps==30 && boost.untilMs==11180);
    pointer.observe(move.data(),move.size(),11050);
    boost=pointer.observe(move.data(),move.size(),11100);
    assert(boost.fps==60 && boost.untilMs==11800);
    auto abs=packet(5,18);abs[15]=100;abs[17]=100;abs[9]=50;abs[11]=50;
    deskport::InputActivity absolute;
    assert(absolute.observe(abs.data(),abs.size(),1000).untilMs==0);
    for(int i=0;i<1000;++i) {
        abs[9]=50+(i%2);
        assert(absolute.observe(abs.data(),abs.size(),1010+i*10).untilMs==0);
    }
    abs[9]=60;assert(absolute.observe(abs.data(),abs.size(),12000).fps==30);
    assert(deskport::activityFrameRate({},0,60)==1);
    assert(deskport::activityFrameRate(boost,11100,1)==1);
    deskport::StreamPolicy absoluteClock(60, 100000);
    absoluteClock.loss(100000); absoluteClock.loss(101500); absoluteClock.loss(103000);
    assert(absoluteClock.frameRate(103000)==60); // same startup guard for both clocks
    absoluteClock.loss(105000); absoluteClock.loss(108000);
    assert(absoluteClock.frameRate(108000)==30);
    deskport::FrameCadence cadence(60);
    assert(cadence.admit(1000000,true,false));
    assert(!cadence.admit(1999999,false,false));
    assert(cadence.admit(2000000,false,false)); // one-second idle refresh
    assert(cadence.admit(2001000,true,false)); // changed content bypasses idle timer
    cadence.boost = {2350,60};
    assert(cadence.admit(2020000,false,false)); // input wakes duplicates immediately
    assert(!cadence.admit(2030000,false,false));
    assert(cadence.admit(2040000,false,false));
    assert(cadence.admit(2350000,false,false)); // still refining the last change
    assert(!cadence.admit(2600000,false,false)); // refinement over: idle floor
    assert(cadence.admit(2600001,false,true)); // recovery does not wait for idle
    assert(deskport::refineFrameRate(1499,1000,60)==60 && deskport::refineFrameRate(1500,1000,60)==1);
    assert(deskport::refineFrameRate(1200,1000,15)==15 && deskport::refineFrameRate(1200,-1,60)==1);
    deskport::FrameCadence pending(60);
    pending.policy.loss(8000); pending.policy.loss(9500); pending.policy.loss(11000);
    assert(pending.admit(12000000,true,false));
    assert(!pending.admit(12005000,true,false));
    assert(pending.admit(12034000,false,false)); // final update survives denied slot
    assert(pending.admit(12068000,false,false)); // refinement at the congestion ceiling
    assert(!pending.admit(12090000,false,false));
    assert(!pending.admit(12600000,false,false)); // then the idle floor
    deskport::FrameCadence other(30);
    assert(other.floor(12068000)==1); // no cross-session input state
    // Office saver: a change goes out at once, continuous change at most 8 fps,
    // two duplicates per second for 750 ms after a change or input, then a
    // 5 s refresh; recovery is never delayed.
    assert(deskport::saverFrameRate(1749,1000,{},60)==2 && deskport::saverFrameRate(1750,1000,{},60)==1);
    assert(deskport::saverFrameRate(1500,-1,{1600,60},60)==2 && deskport::saverFrameRate(1500,1000,{},1)==1);
    deskport::SaverGate saverGate;
    assert(saverGate.admit(1000000,false) && !saverGate.admit(1124999,false) && saverGate.admit(1125000,false));
    assert(saverGate.admit(1130000,true) && !saverGate.admit(1254999,false) && saverGate.admit(1255000,false));
    deskport::FrameCadence saving(60);
    saving.saver = true;
    assert(saving.admit(20000000,true,false));  // a single change is not delayed
    assert(!saving.admit(20030000,true,false)); // continuous change waits for the gate
    assert(!saving.admit(20124999,false,false));
    assert(saving.admit(20125000,false,false)); // and still sends the final picture
    assert(!saving.admit(20200000,false,false));
    assert(saving.admit(20625000,false,false)); // one refinement at 2 fps
    assert(!saving.admit(20780000,false,false)); // refinement over: 5 s refresh
    assert(!saving.admit(25624999,false,false));
    assert(saving.admit(25625000,false,false));
    assert(saving.admit(25625001,false,true)); // recovery does not wait
    assert(saving.admit(40000000,true,false)); // first change after idle is immediate
    // Continuous changes use the content cadence, not the duplicate-frame floor.
    assert(saving.admit(40125000,true,false));
    assert(saving.admit(40250000,true,false));
    // Data saver FEC: one parity shard on a clean link, the viewer's minimum
    // for a minute after an unrecoverable block.
    assert(deskport::saverMinParity(true,2,100000,0)==1 && deskport::saverMinParity(true,2,100000,50000)==2);
    assert(deskport::saverMinParity(false,2,100000,0)==2 && deskport::saverMinParity(true,1,100000,0)==1);
    assert(deskport::saverMinParity(true,0,100000,0)==0);
    std::cout << "PASS: exact pixels, malformed/repaired FEC, burst hysteresis, frame cadence, recovery bypass, idle recovery guard, bandwidth ceilings, remote rate control, data saver cadence and FEC, scroll pacing, daily traffic\n";
}
