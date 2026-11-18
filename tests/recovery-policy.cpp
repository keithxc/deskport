#include "../host/macos/recovery-policy.h"
#include <cassert>
#include <iostream>
#include <set>
using namespace DeskPortRecovery;
struct Fake {
    bool enabled = true, paused = false, denied = false, gui = true;
    bool loaded = true, running = false, external = false, login = true;
    bool failure = false, noPid = false, queryFailure = false;
    int starts = 0, bootstraps = 0;
    std::string health;
    Result command(const std::vector<std::string>& args) {
        if (args[0] == "/usr/bin/pgrep") return {queryFailure ? 2 : external ? 0 : 1, {}};
        assert(args[0] == "/bin/launchctl");
        if (args[1] == "print-disabled") return {gui ? 0 : 113, denied ? "\"io.github.keithxc.DeskPort\" => disabled" : ""};
        if (args[1] == "print") return {loaded ? 0 : 113, running ? "\tpid = 123\n" : "state = not running\n"};
        if (args[1] == "bootstrap") { ++bootstraps; loaded = true; return {failure ? 1 : 0, {}}; }
        assert(args[1] == "kickstart" && args.size() == 3); // Never -k (would kill a live job).
        ++starts; running = !failure && !noPid; return {failure ? 1 : 0, {}};
    }
    int check() {
        return recover("/test-user", 501, [this](const std::vector<std::string>& a) { return command(a); },
            [this](const std::string& p) {
                if (p.find("/enabled") != std::string::npos) return enabled;
                if (p.find("/paused") != std::string::npos) return paused;
                return login;
            }, [this](const std::string&, const char* s) { health = s; }, [] {});
    }
};
int main() {
    assert(hasPid("\tpid = 123\n"));
    for (const auto& s : {"last pid = 123\n", "pid = 0\n", "pid = -1\n", "pid = 12junk\n", "state = running\n"}) assert(!hasPid(s));
    assert(!disabled("\"other.app\" => disabled"));
    { Fake f; f.enabled = false; assert(f.check() == 0 && f.starts == 0); }
    { Fake f; f.paused = true; assert(f.check() == 0 && f.starts == 0); }
    { Fake f; f.denied = true; assert(f.check() == 0 && f.health == "disabled" && !f.starts); }
    { Fake f; f.gui = false; assert(f.check() == 0 && f.health == "waiting-session" && !f.starts); }
    { Fake f; f.running = true; assert(f.check() == 0 && f.health == "running" && !f.starts); }
    { Fake f; f.external = true; assert(f.check() == 0 && f.health == "running" && !f.starts); }
    { Fake f; f.login = false; assert(f.check() == 0 && f.health == "missing-login-item" && !f.starts); }
    { Fake f; assert(f.check() == 0 && f.starts == 1 && f.health == "running"); assert(f.check() == 0 && f.starts == 1); }
    { Fake f; f.loaded = false; assert(f.check() == 0 && f.bootstraps == 1 && f.starts == 1); }
    { Fake f; f.failure = true; assert(f.check() == 1 && f.health == "error"); }
    { Fake f; f.loaded = false; f.failure = true; assert(f.check() == 1 && f.bootstraps == 1 && !f.starts); }
    { Fake f; f.noPid = true; assert(f.check() == 1 && f.health == "error"); }
    { Fake f; f.queryFailure = true; assert(f.check() == 1 && !f.starts); }
    {
        // One persistent account across opt-in, app crash, pause, reopen,
        // upgrade/rollback process overlap, opt-out and login-item removal.
        Fake f; f.enabled = false;
        assert(f.check() == 0 && !f.starts);
        f.enabled = true;
        assert(f.check() == 0 && f.starts == 1);
        f.running = false; // Crash: exactly one recovery start.
        assert(f.check() == 0 && f.starts == 2);
        f.paused = true; f.running = false;
        assert(f.check() == 0 && f.starts == 2);
        f.paused = false; // User reopened the app; an existing app wins.
        f.external = true;
        assert(f.check() == 0 && f.starts == 2);
        f.loaded = false; // Replacement/rollback binary is already running.
        assert(f.check() == 0 && f.starts == 2 && !f.bootstraps);
        f.external = false;
        assert(f.check() == 0 && f.starts == 3 && f.bootstraps == 1);
        f.enabled = false; f.running = false;
        assert(f.check() == 0 && f.starts == 3);
        f.enabled = true; f.login = false;
        assert(f.check() == 0 && f.health == "missing-login-item" && f.starts == 3);
    }
    std::cout << "PASS: 13 recovery scenarios, persistent lifecycle sequence, PID parsing, disabled-job isolation\n";
}
