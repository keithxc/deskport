// SPDX-License-Identifier: GPL-3.0-or-later
// API doubles only: never send a signal to a numeric PID from a fixture.
#include "ownedsupervisor.h"
#include <cassert>
#include <cstdio>
#include <vector>
struct Fake {
    long long clock = 0;
    int exit = 7;
    int aliveUntil = 100;
    int stopAt = -1;
    int terms = 0, hard = 0, soft = 0, pauses = 0;
    bool started = true, signalOK = true;
    std::vector<int> directStates;
    std::vector<long> signalled;
    bool start(char**) { return started; }
    int reap() { return clock < aliveUntil ? 1 : 0; }
    int exitCode() { return exit; }
    long long now() { return clock; }
    bool stopRequested() { return stopAt >= 0 && clock >= stopAt; }
    void termGroup() { ++terms; }
    bool signalChildren(bool kill) { kill ? ++hard : ++soft; return signalOK; }
    void pause() { clock += 10; ++pauses; assert(pauses < 1000); }
    int childState(long) {
        assert(!directStates.empty());
        int state = directStates.front(); directStates.erase(directStates.begin()); return state;
    }
    bool sendChildSignal(long pid, bool) { signalled.push_back(pid); return true; }
};
int main() {
    using namespace SeamlessOwned;
    Fake earlyLeader; earlyLeader.aliveUntil = 6000; // leader exited; descendant owns lifetime
    assert(supervise(earlyLeader, nullptr) == 7);
    assert(earlyLeader.clock == 6000 && earlyLeader.terms == 0);
    // Descendants disappear later, then the kernel reuses the old PID. No
    // retained PGID handle exists and supervise returns before any stop call.
    const int calls = earlyLeader.terms + earlyLeader.hard + earlyLeader.soft;
    assert(calls == 0);
    Fake term; term.stopAt = 0; term.aliveUntil = 20;
    assert(supervise(term, nullptr) == 7 && term.terms == 1 && term.hard == 0);
    Fake stubborn; stubborn.stopAt = 0; stubborn.aliveUntil = 3030;
    assert(supervise(stubborn, nullptr) == 7 && stubborn.terms == 1 && stubborn.hard == 3);
    Fake adopted; adopted.stopAt = 0; adopted.aliveUntil = 3200;
    assert(supervise(adopted, nullptr) == 7 && adopted.hard == 20);
    Fake parentDeath; parentDeath.stopAt = 20; parentDeath.aliveUntil = 3050;
    assert(supervise(parentDeath, nullptr) == 7 && parentDeath.terms == 1 && parentDeath.hard == 3);
    Fake timeout; timeout.stopAt = 0; timeout.aliveUntil = 10000;
    assert(supervise(timeout, nullptr) == 124 && timeout.clock == 4000);
    Fake startFailure; startFailure.started = false;
    assert(supervise(startFailure, nullptr) == 125 && startFailure.pauses == 0);
    Fake failure; failure.stopAt = 0; failure.signalOK = false;
    assert(supervise(failure, nullptr) == 125);
    Fake ownership;
    // Live direct child, exited/reaped child, ECHILD (including reused PID),
    // transient wait failure. Only the live owned child receives a signal.
    ownership.directStates = {0, 1, -1, -2};
    for (int i = 0; i < 4; ++i) assert(signalDirectChild(ownership, 100 + i, true));
    assert(ownership.signalled == std::vector<long>{100});
    std::puts("PASS: 9 supervisor lifetime/shutdown scenarios; 4 direct-child identity states (API doubles)");
}
