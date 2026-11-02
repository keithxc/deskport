// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Separate desktop-input process for the macOS host. Sunshine keeps the
// connection, decryption, activity tracking and encoding; this same executable,
// started as `Sunshine <config> --deskport-input <fd>`, runs Sunshine's own
// input module for keyboard, text, mouse and scroll, including key repeat and
// modifier state. It shares the host's code signature and privacy permissions.
#include "src/config.h"
#include "src/deskport/common/inputprocess.h"
#include "src/globals.h"
#include "src/input.h"
#include "src/logging.h"

#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <fcntl.h>
#include <mach-o/dyld.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

namespace deskport::input_process {
static_assert(std::is_trivially_copyable_v<input::touch_port_t>, "touch ports cross the process boundary as bytes");

// Sunshine side. Packets the helper cannot take are injected in-process, so a
// helper failure degrades to the previous behavior instead of losing input.
class Host {
public:
    static Host &get() {
        static Host host;
        return host;
    }
    // DESKPORT_INPUT_PROCESS=0 keeps every packet in Sunshine.
    void start() {
        const char *value = std::getenv("DESKPORT_INPUT_PROCESS");
        enabled = !(value && std::string_view(value) == "0");
        if (!enabled) {
            BOOST_LOG(info) << "DeskPort input process: disabled";
            return;
        }
        std::lock_guard lock {mutex};
        spawnLocked();
    }
    bool forward(const std::vector<std::uint8_t> &packet) {
        if (!enabled || !desktopPacket(packet.data(), packet.size())) return false;
        std::lock_guard lock {mutex};
        return sendLocked(Message::packet, packet.data(), packet.size());
    }
    void touchPort(const input::touch_port_t &port) {
        if (!enabled) return;
        std::lock_guard lock {mutex};
        lastPort = port;
        sendLocked(Message::touchPort, &port, sizeof(port));
    }
    // Session end: release held keys and buttons, as input::reset does in-process.
    void reset() {
        if (!enabled) return;
        std::lock_guard lock {mutex};
        sendLocked(Message::reset, nullptr, 0);
    }

private:
    bool enabled = false;
    std::mutex mutex;
    int fd = -1;
    pid_t pid = -1;
    std::optional<input::touch_port_t> lastPort;
    std::chrono::steady_clock::time_point lastSpawn {};

    // A stuck helper must never stall the control stream.
    bool writeAll(const std::vector<std::uint8_t> &bytes) {
        std::size_t done = 0;
        while (fd >= 0 && done < bytes.size()) {
            pollfd ready {fd, POLLOUT, 0};
            if (::poll(&ready, 1, 100) <= 0) return false;
            const ssize_t written = ::write(fd, bytes.data() + done, bytes.size() - done);
            if (written < 0) {
                if (errno == EINTR || errno == EAGAIN) continue;
                return false;
            }
            done += std::size_t(written);
        }
        return done == bytes.size();
    }
    bool sendLocked(Message type, const void *data, std::size_t size) {
        const auto bytes = frame(type, data, size);
        if (bytes.empty()) return false;
        if (fd >= 0 && writeAll(bytes)) return true;
        stopLocked();
        return spawnLocked() && writeAll(bytes);
    }
    void stopLocked() {
        if (fd >= 0) ::close(fd);
        fd = -1;
        if (pid > 0) {
            ::kill(pid, SIGKILL);
            ::waitpid(pid, nullptr, 0);
            BOOST_LOG(warning) << "DeskPort input process: stopped pid=" << pid;
        }
        pid = -1;
    }
    // At most one start per second; meanwhile packets stay in-process.
    bool spawnLocked() {
        const auto now = std::chrono::steady_clock::now();
        if (lastSpawn != std::chrono::steady_clock::time_point {} && now - lastSpawn < std::chrono::seconds(1)) return false;
        lastSpawn = now;
        char path[PATH_MAX];
        std::uint32_t length = sizeof(path);
        if (_NSGetExecutablePath(path, &length) != 0) return false;
        int pair[2];
        if (::socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0) return false;
        ::fcntl(pair[0], F_SETFD, FD_CLOEXEC);
        int noSignal = 1;
        ::setsockopt(pair[0], SOL_SOCKET, SO_NOSIGPIPE, &noSignal, sizeof(noSignal));
        posix_spawn_file_actions_t actions;
        posix_spawnattr_t attributes;
        posix_spawn_file_actions_init(&actions);
        posix_spawnattr_init(&attributes);
        // Only stdio and the socket (as fd 3) reach the helper.
        posix_spawnattr_setflags(&attributes, POSIX_SPAWN_CLOEXEC_DEFAULT);
        for (int stdio = 0; stdio < 3; ++stdio) posix_spawn_file_actions_addinherit_np(&actions, stdio);
        posix_spawn_file_actions_adddup2(&actions, pair[1], 3);
        std::string config = config::sunshine.config_file;
        std::string command = "--deskport-input";
        std::string channel = "3";
        std::vector<char *> argv {path};
        if (!config.empty()) argv.push_back(config.data());
        argv.push_back(command.data());
        argv.push_back(channel.data());
        argv.push_back(nullptr);
        pid_t child = -1;
        const int status = posix_spawn(&child, path, &actions, &attributes, argv.data(), environ);
        posix_spawn_file_actions_destroy(&actions);
        posix_spawnattr_destroy(&attributes);
        ::close(pair[1]);
        if (status != 0) {
            ::close(pair[0]);
            BOOST_LOG(warning) << "DeskPort input process: start failed, error=" << status;
            return false;
        }
        fd = pair[0];
        pid = child;
        BOOST_LOG(info) << "DeskPort input process: started pid=" << pid;
        if (lastPort) writeAll(frame(Message::touchPort, &*lastPort, sizeof(*lastPort)));
        return true;
    }
};

// Sunshine call sites.
inline void passthrough(std::shared_ptr<input::input_t> &input, std::vector<std::uint8_t> &&packet) {
    if (!Host::get().forward(packet)) input::passthrough(input, std::move(packet));
}
template<typename Event>
void raisePort(Event &event, input::touch_port_t &&port) {
    Host::get().touchPort(port);
    event->raise(std::move(port));
}

// Helper side: `--deskport-input <fd>`. Ends when Sunshine closes the socket,
// including a crash, after releasing everything still held.
inline int run(int argc, char **argv) {
    const int fd = argc > 0 ? std::atoi(argv[0]) : -1;
    if (fd < 0) return 2;
    ::signal(SIGPIPE, SIG_IGN);
    auto inputDeinit = input::init();
    task_pool.start(1);
    auto mail = std::make_shared<safe::mail_raw_t>();
    auto input = input::alloc(mail, "deskport-input");
    std::optional<input::touch_port_t> port;
    Reader reader;
    std::uint8_t buffer[4096];
    BOOST_LOG(info) << "DeskPort input process: ready";
    for (;;) {
        const ssize_t received = ::read(fd, buffer, sizeof(buffer));
        if (received < 0 && errno == EINTR) continue;
        if (received <= 0) break;
        reader.feed(buffer, std::size_t(received));
        while (auto message = reader.next()) {
            auto &[type, payload] = *message;
            switch (type) {
            case Message::packet:
                input::passthrough(input, std::move(payload));
                break;
            case Message::touchPort:
                if (payload.size() == sizeof(input::touch_port_t)) {
                    input::touch_port_t value;
                    std::memcpy(&value, payload.data(), sizeof(value));
                    port = value;
                    mail->event<input::touch_port_t>(mail::touch_port)->raise(std::move(value));
                }
                break;
            case Message::reset:
                input::reset(input);
                input = input::alloc(mail, "deskport-input");
                if (port) mail->event<input::touch_port_t>(mail::touch_port)->raise(input::touch_port_t(*port));
                break;
            }
        }
        if (reader.broken()) {
            BOOST_LOG(error) << "DeskPort input process: malformed channel";
            break;
        }
    }
    input::reset(input);
    task_pool.stop();
    task_pool.join();
    BOOST_LOG(info) << "DeskPort input process: exited";
    return 0;
}
}
