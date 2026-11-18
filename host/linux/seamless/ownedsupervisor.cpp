// SPDX-License-Identifier: GPL-3.0-or-later
#include "ownedsupervisor.h"

#ifdef __linux__
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fcntl.h>
#include <poll.h>
#include <set>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace SeamlessOwned {
namespace {
class LinuxSystem {
public:
    bool start(char** argv) {
        if (!argv || !argv[0] || ::setsid() < 0 ||
            ::prctl(PR_SET_CHILD_SUBREAPER, 1) != 0)
            return false;
        struct sigaction ignored {};
        ignored.sa_handler = SIG_IGN;
        ::sigemptyset(&ignored.sa_mask);
        if (::sigaction(SIGTERM, &ignored, nullptr) != 0 ||
            ::sigaction(SIGINT, &ignored, nullptr) != 0)
            return false;
        struct sigaction reaper {};
        reaper.sa_handler = SIG_DFL;
        ::sigemptyset(&reaper.sa_mask);
        if (::sigaction(SIGCHLD, &reaper, nullptr) != 0)
            return false;
        // Require direct-child enumeration before creating any application.
        char path[96];
        std::snprintf(path, sizeof(path), "/proc/self/task/%ld/children", static_cast<long>(::getpid()));
        FILE* children = std::fopen(path, "r");
        if (!children) return false;
        std::fclose(children);
        const int flags = ::fcntl(STDIN_FILENO, F_GETFL);
        if (flags < 0 || ::fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK) < 0)
            return false;
        leader = ::fork();
        if (leader < 0)
            return false;
        if (leader == 0) {
            // Do not let applications read/retain the private control endpoint.
            const int null = ::open("/dev/null", O_RDONLY);
            if (null < 0 || ::dup2(null, STDIN_FILENO) < 0)
                ::_exit(126);
            if (null != STDIN_FILENO) ::close(null);
            struct sigaction action {};
            action.sa_handler = SIG_DFL;
            ::sigemptyset(&action.sa_mask);
            ::sigaction(SIGTERM, &action, nullptr);
            ::sigaction(SIGINT, &action, nullptr);
            ::execvp(argv[0], argv);
            ::_exit(127);
        }
        return true;
    }
    int reap() {
        for (;;) {
            int status = 0;
            const pid_t pid = ::waitpid(-1, &status, WNOHANG);
            if (pid > 0) { record(pid, status); continue; }
            if (pid == 0) return 1;
            if (errno == EINTR) continue;
            return errno == ECHILD ? 0 : -1;
        }
    }
    long long now() const {
        struct timespec t {};
        ::clock_gettime(CLOCK_MONOTONIC, &t);
        return static_cast<long long>(t.tv_sec) * 1000 + t.tv_nsec / 1000000;
    }
    bool stopRequested() const {
        char commands[32];
        const ssize_t count = ::read(STDIN_FILENO, commands, sizeof(commands));
        return count >= 0 || (errno != EAGAIN && errno != EINTR);
    }
    void termGroup() const {
        // 0 means OUR current group, which this living supervisor pins. Never
        // accept a numeric group from the parent or from previous launches.
        (void)::kill(0, SIGTERM);
    }
    bool signalChildren(bool hard) {
        char path[96];
        std::snprintf(path, sizeof(path), "/proc/self/task/%ld/children", static_cast<long>(::getpid()));
        FILE* file = std::fopen(path, "r");
        if (!file) return false;
        long value;
        bool ok = true;
        while (std::fscanf(file, "%ld", &value) == 1) {
            if (!signalDirectChild(*this, value, hard))
                ok = false;
        }
        if (std::ferror(file)) ok = false;
        std::fclose(file);
        return ok;
    }
    int childState(long value) {
        int status = 0;
        const pid_t pid = static_cast<pid_t>(value);
        // No concurrent reaper exists. A child that exits after this check
        // remains a zombie and reserves its PID until our next waitpid.
        const pid_t waited = ::waitpid(pid, &status, WNOHANG);
        if (waited == pid) record(pid, status);
        return waited == 0 ? 0 : 1;
    }
    bool sendChildSignal(long value, bool hard) {
        const pid_t pid = static_cast<pid_t>(value);
        if (!hard && !termSent.insert(pid).second)
            return true;
        return ::kill(pid, hard ? SIGKILL : SIGTERM) == 0 || errno == ESRCH;
    }
    void pause() const { ::poll(nullptr, 0, 10); }
    int exitCode() const { return result; }
private:
    void record(pid_t pid, int status) {
        termSent.erase(pid);
        if (pid == leader) {
            result = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
            leader = -1; // Reaping releases this PID; a later child can reuse it.
        }
    }
    std::set<pid_t> termSent;
    pid_t leader = -1;
    int result = 125;
};
}
int runSupervisor(char** argv) {
    LinuxSystem system;
    const int code = supervise(system, argv);
    if (code == 124 || code == 125)
        std::fprintf(stderr, "seamless owned supervisor: cleanup/setup failed (or application returned reserved code %d)\n", code);
    return code;
}
}
#else
namespace SeamlessOwned { int runSupervisor(char**) { return 125; } }
#endif
