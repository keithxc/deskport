// SPDX-License-Identifier: GPL-3.0-or-later
// Compile the actual Linux syscall adapter with API doubles on macOS too.
// No real process is launched/signalled, no /proc or user session is read.
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cassert>
#include <map>
#include <set>
#include <vector>
#include <string>

namespace Test {
struct Proc { long long death; bool reaped = false; int status = 0; };
std::map<pid_t, Proc> children;
std::set<pid_t> foreign{999}; // stale/reused PID negative control
std::vector<std::pair<pid_t,int>> signals;
long long tick = 0, stopAt = -1;
bool reuseLeader = false;
bool eof = false, failSetup = false;
void snapshot() {
    FILE* f = ::fopen("children.txt", "w"); assert(f);
    for (auto& [pid, process] : children) if (!process.reaped) std::fprintf(f, "%d ",pid);
    std::fprintf(f, "999 "); // deliberately inject a stale/non-child PID
    std::fclose(f);
}
void reset() { children.clear(); signals.clear(); tick = 0; stopAt = -1; eof = false; failSetup = false; reuseLeader = false; }
}
#ifdef sigemptyset
#undef sigemptyset
int test_sigemptyset(sigset_t* set) { *set = 0; return 0; }
#define sigemptyset test_sigemptyset
#endif
pid_t test_setsid() { return Test::failSetup ? -1 : 123; }
int test_prctl(int option, ...) { assert(option == 36); return 0; }
pid_t test_fork() { return 123; }
int test_fcntl(int, int, ...) { return 0; }
ssize_t test_read(int fd, void* buffer, size_t) {
    assert(fd == STDIN_FILENO);
    if (Test::stopAt >= 0 && Test::tick >= Test::stopAt) {
        if (Test::eof) return 0;
        *static_cast<char*>(buffer) = 'T'; return 1;
    }
    errno = EAGAIN; return -1;
}
int test_clock_gettime(clockid_t, struct timespec* t) {
    t->tv_sec = Test::tick / 1000; t->tv_nsec = Test::tick % 1000 * 1000000; return 0;
}
int test_poll(struct pollfd*, nfds_t, int ms) {
    Test::tick += ms;
    if (Test::reuseLeader && Test::tick == 20)
        Test::children[123] = {30,false,9 << 8};
    return 0;
}
pid_t test_waitpid(pid_t pid, int* status, int flags) {
    assert(flags == WNOHANG);
    if (pid == -1) {
        bool alive = false;
        for (auto& [id, p] : Test::children) {
            if (p.reaped) continue;
            alive = true;
            if (Test::tick >= p.death) { p.reaped = true; *status = p.status; return id; }
        }
        if (alive) return 0;
        errno = ECHILD; return -1;
    }
    const auto it = Test::children.find(pid);
    if (it == Test::children.end() || it->second.reaped) { errno = ECHILD; return -1; }
    if (Test::tick >= it->second.death) { it->second.reaped = true; *status = it->second.status; return pid; }
    return 0;
}
int test_kill(pid_t pid, int signal) {
    // No arbitrary PGID, no foreign/reused PID, no signal to the supervisor.
    assert(pid == 0 || (Test::children.count(pid) && !Test::children.at(pid).reaped));
    assert(!Test::foreign.count(pid));
    Test::signals.emplace_back(pid, signal);
    if (pid > 0 && signal == SIGKILL) Test::children[pid].death = Test::tick + 10;
    return 0;
}
FILE* test_fopen(const char* path, const char* mode) {
    assert(std::string(path).find("/proc/self/task/") == 0 && std::string(mode) == "r");
    Test::snapshot(); return ::fopen("children.txt", "r");
}
namespace std { using ::test_fopen; }
#ifndef __linux__
#define __linux__ 1
#endif
#define setsid test_setsid
#define prctl test_prctl
#define fork test_fork
#define fcntl test_fcntl
#define read test_read
#define clock_gettime test_clock_gettime
#define poll test_poll
#define waitpid test_waitpid
#define kill test_kill
#define fopen test_fopen
#include "ownedsupervisor.cpp"
#undef fopen

int main() {
    char arg[] = "fixture"; char* argv[] = {arg, nullptr};
    using SeamlessOwned::runSupervisor;
    Test::reset(); Test::children = {{123,{0,false,7 << 8}},{124,{6000}}};
    assert(runSupervisor(argv) == 7 && Test::tick == 6000 && Test::signals.empty());
    // A later reused leader number is not retained anywhere by the runtime.
    Test::foreign.insert(123); Test::reset();
    Test::children = {{123,{0,false,7 << 8}},{124,{100}}};
    Test::stopAt = 10;
    assert(runSupervisor(argv) == 7);
    assert(Test::signals.front() == std::make_pair(pid_t(0),SIGTERM));
    for (auto s : Test::signals) assert(s.first == 0 || s.first == 124);
    Test::foreign.erase(123);
    // TERM-ignoring descendant, including one that escaped the original group:
    // direct-child ownership, not group membership, authorizes hard cleanup.
    Test::reset(); Test::children = {{123,{0}},{124,{100000}},{125,{100000}}}; Test::stopAt = 10;
    assert(runSupervisor(argv) == 0 && Test::tick == 3020);
    int kills = 0; for (auto s : Test::signals) if (s.second == SIGKILL) ++kills;
    assert(kills == 2);
    // Parent death (EOF, no command) performs the same bounded cleanup.
    Test::reset(); Test::children = {{123,{100000}}}; Test::stopAt = 0; Test::eof = true;
    assert(runSupervisor(argv) == 0 && Test::tick == 3010);
    Test::reset(); Test::reuseLeader = true;
    Test::children = {{123,{0,false,7 << 8}},{124,{60}}};
    assert(runSupervisor(argv) == 7 && Test::tick == 60 && Test::signals.empty());
    Test::reset(); Test::failSetup = true;
    assert(runSupervisor(argv) == 125 && Test::signals.empty());
    std::puts("PASS: production Linux adapter, 6 API-double scenarios; foreign/reused PID never signalled");
}
