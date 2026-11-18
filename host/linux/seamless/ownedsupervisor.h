// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace SeamlessOwned {
// 0 means an unreaped live direct child. Any other result must not be
// signalled: in particular ECHILD can mean a stale/reused PID from a snapshot.
template<class System> bool signalDirectChild(System& system, long pid, bool hard)
{
    if (system.childState(pid) != 0)
        return true;
    return system.sendChildSignal(pid, hard);
}

// The supervisor itself pins the process group until all adopted children have
// been reaped. The caller addresses a pipe, never a saved PID/PGID. System is
// injectable so shutdown, orphan adoption and PID reuse need no unsafe fixture.
template<class System> int supervise(System& system, char** argv)
{
    if (!system.start(argv))
        return 125;
    bool stopping = false;
    long long deadline = 0;
    long long hardDeadline = 0;
    for (;;) {
        const int children = system.reap(); // -1: failure, 0: ECHILD, 1: alive
        if (children == 0)
            return system.exitCode();
        if (children < 0)
            return 125;
        const long long now = system.now();
        // EOF is also a stop request: parent death closes the private pipe.
        if (!stopping && system.stopRequested()) {
            stopping = true;
            deadline = now + 3000;
            hardDeadline = deadline + 1000;
            system.termGroup();
        }
        if (stopping) {
            // Direct children are still owned even if they called setsid().
            // Killing a parent adopts its descendants via CHILD_SUBREAPER.
            if (!system.signalChildren(now >= deadline))
                return 125;
            if (now >= hardDeadline)
                return 124; // Explicit cleanup failure, e.g. uninterruptible IO.
        }
        system.pause();
    }
}

int runSupervisor(char** argv);
} // namespace SeamlessOwned
