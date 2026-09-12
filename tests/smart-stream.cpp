#include "host/common/smartstream.h"
#include "app/backend/streambudget.h"
#include <cassert>
#include <iostream>
#include <vector>
int main() {
    using namespace DeskPortStream;
    assert(initialBitrate(25000,2560,1440,60,false)==15000);
    assert(initialBitrate(7000,3840,2160,60,false)==7000);
    assert(initialBitrate(40000,1280,720,30,false)==5000);
    assert(initialBitrate(40000,2560,1440,60,true)==30000);
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
    std::cout << "PASS: exact pixels, malformed/repaired FEC, burst hysteresis, frame cadence, recovery bypass, idle recovery guard, bandwidth ceilings\n";
}
