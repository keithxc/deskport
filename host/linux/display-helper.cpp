// SPDX-License-Identifier: GPL-3.0-or-later
// A connection-owned KWin primary output with temporary physical-screen mirroring.
#include "gnome-display.h"
#include "text-caret.h"
#include "kwin-permission.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QSize>
#include <QUuid>
#include <QThread>
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <poll.h>
#include <unistd.h>
#include <wayland-client.h>
#include "zkde-screencast-unstable-v1.h"
#include "kde-output-device-v2.h"
#include "kde-output-management-v2.h"

static void reply(const QJsonObject& object) {
    const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
    fwrite(bytes.constData(), 1, size_t(bytes.size()), stdout); fflush(stdout);
}

class Display {
    struct Mode { QSize size; int refresh = 0; bool removed = false; };
    struct Output {
        kde_output_device_v2* proxy = nullptr;
        QString name, uuid, replication;
        uint32_t priority = 0;
        bool enabled = false;
        int x = 0, y = 0, transform = 0;
        std::map<kde_output_device_mode_v2*, Mode> modes;
        kde_output_device_mode_v2* current = nullptr;
        double scale = 1;
        bool removed = false;
    };
    wl_display* display = nullptr;
    wl_registry* registry = nullptr;
    zkde_screencast_unstable_v1* screencast = nullptr;
    zkde_screencast_stream_unstable_v1* stream = nullptr;
    kde_output_management_v2* management = nullptr;
    std::map<uint32_t, std::unique_ptr<Output>> outputs;
    Output* owned = nullptr;
    bool ready = false, broken = false;
    int applied = 0;
    QString error;
    std::unique_ptr<QProcess> recovery;
    QJsonArray baseline;
    bool sessionActive = false;
    bool recoveryPending = false;
    QString layoutWarning;
    int sessionPolicy = -1;
public:
    const QString name = "DeskPort-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    ~Display() {
        // Remove our output before recovery: topology changes may otherwise
        // overwrite the recovered policy and re-enable previously disabled screens.
        if (stream) zkde_screencast_stream_unstable_v1_close(stream);
        if (display) { wl_display_flush(display); wl_display_disconnect(display); display = nullptr; }
        if (recovery) {
            recovery->closeWriteChannel();
            if (!recovery->waitForFinished(3000)) fprintf(stderr, "KWin layout recovery did not finish promptly\n");
        }
    }
    static bool valid(int width, int height, int scale) {
        return width >= 640 && width <= 7680 && height >= 360 && height <= 4320 &&
            width % 4 == 0 && height % 4 == 0 && (scale == 1 || scale == 2);
    }
    bool wait(const std::function<bool()>& condition, int timeout = 4000) {
        QElapsedTimer timer; timer.start();
        while (!broken && !condition()) {
            if (wl_display_dispatch_pending(display) < 0) break;
            if (condition()) return true;
            if (wl_display_flush(display) < 0 && errno != EAGAIN) break;
            const int remaining = timeout - int(timer.elapsed());
            if (remaining <= 0) break;
            pollfd fd{wl_display_get_fd(display), POLLIN, 0};
            const int result = poll(&fd, 1, remaining);
            if (result < 0 && errno == EINTR) continue;
            if (result <= 0 || !(fd.revents & POLLIN) || wl_display_dispatch(display) < 0) break;
        }
        if (!broken && condition()) return true;
        broken = true;
        if (error.isEmpty()) error = "KWin virtual display request timed out or disconnected";
        return false;
    }
    bool sync() {
        bool done = false;
        auto callback = wl_display_sync(display);
        static const wl_callback_listener listener = { [](void* p, wl_callback*, uint32_t) { *static_cast<bool*>(p) = true; } };
        wl_callback_add_listener(callback, &listener, &done);
        const bool ok = wait([&] { return done; });
        wl_callback_destroy(callback);
        return ok;
    }
    void resetConnection() {
        for (auto& pair : outputs) {
            for (auto& mode : pair.second->modes) wl_proxy_destroy(reinterpret_cast<wl_proxy*>(mode.first));
            wl_proxy_destroy(reinterpret_cast<wl_proxy*>(pair.second->proxy));
        }
        outputs.clear();
        if (screencast) { zkde_screencast_unstable_v1_destroy(screencast); screencast = nullptr; }
        if (management) { kde_output_management_v2_destroy(management); management = nullptr; }
        if (registry) { wl_registry_destroy(registry); registry = nullptr; }
        if (display) { wl_display_disconnect(display); display = nullptr; }
        error.clear();
    }
    bool connectSession() {
        if (connectOnce()) return true;
        // Native/Nix permissions need no writes or cache refresh. Repair only
        // the missing screencast grant, never an unsupported compositor.
        if (broken || screencast || !management) return false;
        if (!KWinPermission::install(error)) return false;
        QElapsedTimer deadline;
        deadline.start();
        do {
            // KWin caches requested interfaces per ClientConnection. A new
            // connection is essential after registering the executable.
            resetConnection();
            if (connectOnce()) return true;
            if (broken || !display) return false;
            QThread::msleep(100);
        } while (deadline.elapsed() < 3500);
        error = "KWin has not granted deskport-display screencast access (" + KWinPermission::executable() +
            "). Restart sharing after running kbuildsycoca6 --noincremental; see LINUX_PACKAGES.md.";
        return false;
    }
    bool connectOnce() {
        display = wl_display_connect(nullptr);
        if (!display) { error = "Cannot connect to the Wayland session"; return false; }
        registry = wl_display_get_registry(display);
        static const wl_registry_listener listener = {
            [](void* p, wl_registry* registry, uint32_t id, const char* interface, uint32_t version) {
                auto self = static_cast<Display*>(p);
                if (!strcmp(interface, "zkde_screencast_unstable_v1") && version >= 2) {
                    self->screencast = static_cast<zkde_screencast_unstable_v1*>(wl_registry_bind(registry, id, &zkde_screencast_unstable_v1_interface, std::min(version, 4u)));
                } else if (!strcmp(interface, "kde_output_management_v2") && version >= 18) {
                    self->management = static_cast<kde_output_management_v2*>(wl_registry_bind(registry, id, &kde_output_management_v2_interface, 18));
                } else if (!strcmp(interface, "kde_output_device_v2") && version >= 18) {
                    auto out = std::make_unique<Output>();
                    out->proxy = static_cast<kde_output_device_v2*>(wl_registry_bind(registry, id, &kde_output_device_v2_interface, 18));
                    wl_proxy_add_dispatcher(reinterpret_cast<wl_proxy*>(out->proxy), outputEvent, nullptr, out.get());
                    self->outputs.emplace(id, std::move(out));
                }
            },
            [](void* p, wl_registry*, uint32_t id) {
                auto self = static_cast<Display*>(p);
                auto found = self->outputs.find(id);
                if (found != self->outputs.end()) {
                    found->second->removed = true;
                    if (found->second.get() == self->owned) {
                        self->broken = true; self->error = "The DeskPort virtual output was removed";
                    }
                }
            }
        };
        wl_registry_add_listener(registry, &listener, this);
        if (!sync() || !sync()) return false;
        if (!screencast || !management) {
            error = !management ? "deskport-display requires KWin 6.6+ output-management protocols" :
                "KWin screencast permission is unavailable for deskport-display";
            return false;
        }
        return true;
    }
    QJsonArray layout() const {
        QJsonArray state;
        for (const auto& entry : outputs) {
            const auto& out = *entry.second;
            if (out.removed || &out == owned) continue;
            QJsonObject saved{{"uuid", out.uuid}, {"replication", out.replication},
                {"priority", double(out.priority)}, {"enabled", out.enabled},
                {"x", out.x}, {"y", out.y}, {"transform", out.transform}, {"scale", out.scale}};
            const auto mode = out.modes.find(out.current);
            if (mode != out.modes.end()) {
                saved["width"] = mode->second.size.width(); saved["height"] = mode->second.size.height();
                saved["refresh"] = mode->second.refresh;
            }
            state.append(saved);
        }
        return state;
    }
    void baselineForDiagnostics(const QJsonArray& state) { baseline=state; }
    void recordLayoutEvent(const QString& reason) const {
        const QString directory = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/DeskPort";
        QDir().mkpath(directory);
        const QString path = directory + "/display-layout-events.jsonl";
        QFile file(path);
        if (file.size() > 1024 * 1024) { QFile::remove(path + ".previous"); QFile::rename(path, path + ".previous"); }
        QJsonObject event{{"time", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
            {"reason", reason}, {"policy", sessionPolicy}, {"original", baseline}, {"observed", layout()},
            {"workspace", outputName()}};
        if (file.open(QIODevice::WriteOnly | QIODevice::Append)) {
            file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
            file.write(QJsonDocument(event).toJson(QJsonDocument::Compact) + '\n');
        }
        fprintf(stderr,"DeskPort local layout: %s; original and observed layouts recorded\n",reason.toUtf8().constData());
    }
    void deferLocalLayout() {
        layoutWarning = error.isEmpty() ? QStringLiteral("Local layout recovery remains pending") : error;
        recoveryPending = true;
        recordLayoutEvent(layoutWarning);
    }
    bool saveRecovery(const QJsonArray& state) {
        recovery->write(QJsonDocument(state).toJson(QJsonDocument::Compact) + '\n');
        if (!recovery->waitForReadyRead(3000) || recovery->readLine() != "ready\n") {
            error = "Display layout recovery is not ready"; return false;
        }
        return true;
    }
    bool restore(const QJsonArray& state) {
        if (!sync()) return false;
        auto config = kde_output_management_v2_create_configuration(management);
        uint32_t lastPriority = 0;
        int right = 0;
        for (const auto& saved : state) for (const auto& entry : outputs) {
            const auto& out = *entry.second;
            const auto value = saved.toObject();
            if (out.removed || out.uuid != value["uuid"].toString()) continue;
            kde_output_configuration_v2_enable(config, out.proxy, value["enabled"].toBool());
            kde_output_configuration_v2_set_replication_source(config, out.proxy, value["replication"].toString().toUtf8().constData());
            const uint32_t priority = uint32_t(value["priority"].toDouble());
            kde_output_configuration_v2_set_priority(config, out.proxy, priority);
            kde_output_configuration_v2_position(config, out.proxy, value["x"].toInt(), value["y"].toInt());
            kde_output_configuration_v2_transform(config, out.proxy, value["transform"].toInt());
            kde_output_configuration_v2_scale(config, out.proxy, wl_fixed_from_double(value["scale"].toDouble(1)));
            for (const auto& mode : out.modes) {
                if (!mode.second.removed && mode.second.size == QSize(value["width"].toInt(), value["height"].toInt()) && mode.second.refresh == value["refresh"].toInt()) {
                    kde_output_configuration_v2_mode(config, out.proxy, mode.first); break;
                }
            }
            if (value["enabled"].toBool()) {
                lastPriority = std::max(lastPriority, priority);
                const int pixels = value["transform"].toInt() % 2 ? value["height"].toInt() : value["width"].toInt();
                right = std::max(right, value["x"].toInt() + int(std::ceil(pixels / value["scale"].toDouble(1))));
            }
        }
        if (owned && !owned->removed) {
            // Keep the remote workspace alive but no longer primary or mirrored.
            kde_output_configuration_v2_set_priority(config, owned->proxy, lastPriority + 1);
            kde_output_configuration_v2_position(config, owned->proxy, right, 0);
        }
        if (!apply(config)) return false;
        const auto current = layout();
        for (const auto& saved : state) for (const auto& actual : current) {
            if (saved.toObject()["uuid"] == actual.toObject()["uuid"] && saved != actual) {
                error = "KWin did not restore the original physical output state"; return false;
            }
        }
        return true;
    }
    bool removeOutput() {
        auto previous = owned;
        owned = nullptr;
        auto closing = stream; stream = nullptr;
        if (closing) zkde_screencast_stream_unstable_v1_close(closing);
        if (previous && !previous->removed && !wait([&] { return previous->removed; })) return false;
        return sync();
    }
    bool restoreIdle() {
        if (!owned && !stream && !sessionActive && !recoveryPending) return true;
        // Lease ownership ends even if physical-layout recovery fails.
        sessionActive = false; sessionPolicy = -1;
        if (!removeOutput() || !restore(baseline)) { deferLocalLayout(); return false; }
        recordLayoutEvent("Local layout recovery verified; detached outputs were skipped");
        recoveryPending = false;
        return saveRecovery({});
    }
    bool beginSession(int width, int height, int policy) {
        if (sessionActive) {
            if (sessionPolicy != policy) { error = "Display policy changed during a session"; return false; }
            return true;
        }
        if (!sync()) return false;
        // Retain the original recovery target across failed restores. Never
        // replace it with the damaged local layout at the next connection.
        if (!recoveryPending) baseline = layout();
        else recordLayoutEvent("New client takes over while local recovery remains pending");
        if (!saveRecovery(baseline)) return false;
        if (!owned && !createOutput(width, height)) return false;
        sessionActive = true; sessionPolicy = policy;
        return true;
    }
    bool waitForRemoval(const QString& outputName) {
        if (!sync()) return false;
        return wait([&] {
            for (const auto& entry : outputs) if (!entry.second->removed &&
                (entry.second->name == outputName || entry.second->name == "Virtual-" + outputName)) return false;
            return true;
        }, 2000);
    }
    bool start(int width, int height) {
        if (!connectSession()) return false;
        baseline = layout();
        recovery = std::make_unique<QProcess>();
        recovery->setProcessChannelMode(QProcess::ForwardedErrorChannel);
        recovery->start(QCoreApplication::applicationFilePath(), {"--restore-kwin", name});
        if (!recovery->waitForStarted(3000)) { error = "Cannot start display layout recovery"; return false; }
        if (qEnvironmentVariableIntValue("DESKPORT_DISPLAY_ON_DEMAND") == 1) return saveRecovery({});
        if (!saveRecovery(baseline)) return false;
        return createOutput(width, height);
    }
    bool createOutput(int width, int height) {
        ready = false;
        stream = zkde_screencast_unstable_v1_stream_virtual_output(screencast, name.toUtf8().constData(), width, height, wl_fixed_from_int(1), ZKDE_SCREENCAST_UNSTABLE_V1_POINTER_EMBEDDED);
        static const zkde_screencast_stream_unstable_v1_listener streamListener = {
            [](void* p, zkde_screencast_stream_unstable_v1* stream) { auto s = static_cast<Display*>(p); if (s->stream != stream) return; s->broken = true; s->error = "KWin closed the virtual output"; },
            [](void* p, zkde_screencast_stream_unstable_v1*, uint32_t) { static_cast<Display*>(p)->ready = true; },
            [](void* p, zkde_screencast_stream_unstable_v1*, const char* error) { auto s = static_cast<Display*>(p); s->broken = true; s->error = QString::fromUtf8(error); },
            nullptr // Bound at version 4: serial is a version 6 event.
        };
        zkde_screencast_stream_unstable_v1_add_listener(stream, &streamListener, this);
        if (!wait([&] { return ready; }) || !sync() || !sync()) return false;
        for (auto& entry : outputs) if (!entry.second->removed && (entry.second->name == name || entry.second->name == "Virtual-" + name)) owned = entry.second.get();
        if (!owned) { error = "KWin did not announce the owned virtual output"; return false; }
        // The DRM backend may apply saved/default output configuration after
        // creation (for example fractional scale on a rotated internal panel).
        // Creation parameters are a request, not an acknowledgment of the mode.
        if (!matches(width, height, 1)) {
            fprintf(stderr, "KWin initial mode differs; reconciling to %dx%d at scale 1\n", width, height);
            if (!resize(width, height, 1)) return false;
        }
        if (!restore(baseline)) {
            deferLocalLayout();
            // The physical layout and the owned capture mode are independent.
            if (broken) return false;
        }
        return matches(width,height,1) || resize(width,height,1);
    }
    bool mirror(int policy) {
        if (policy == 2) return true;
        if (!owned || owned->uuid.isEmpty()) { error = "Missing virtual output UUID"; return false; }
        auto config = kde_output_management_v2_create_configuration(management);
        kde_output_configuration_v2_set_priority(config, owned->proxy, 1);
        // Absolute mouse injection addresses the whole logical desktop. With
        // physical outputs mirrored, the sole logical output must start at zero.
        kde_output_configuration_v2_position(config, owned->proxy, 0, 0);
        kde_output_configuration_v2_set_replication_source(config, owned->proxy, "");
        uint32_t priority = 2;
        for (const auto& saved : baseline) for (const auto& entry : outputs) {
            const auto& out = *entry.second;
            const auto value = saved.toObject();
            if (&out == owned || out.removed || out.uuid != value["uuid"].toString()) continue;
            // KWin may have re-enabled this screen during virtual-output creation.
            // The pre-session snapshot, not its current state, owns this decision.
            if (!value["enabled"].toBool() || policy == 1) {
                kde_output_configuration_v2_enable(config, out.proxy, 0);
                kde_output_configuration_v2_set_replication_source(config, out.proxy, value["replication"].toString().toUtf8().constData());
                continue;
            }
            kde_output_configuration_v2_enable(config, out.proxy, 1);
            kde_output_configuration_v2_set_replication_source(config, out.proxy, owned->uuid.toUtf8().constData());
            kde_output_configuration_v2_set_priority(config, out.proxy, priority++);
        }
        if (!apply(config)) return false;
        for (const auto& saved : baseline) for (const auto& entry : outputs) {
            const auto& out = *entry.second;
            const auto value = saved.toObject();
            if (out.removed || out.uuid != value["uuid"].toString()) continue;
            const bool enabled = value["enabled"].toBool() && policy != 1;
            if (out.enabled != enabled || (enabled && out.replication != owned->uuid)) {
                error = "KWin did not apply the requested physical output policy"; return false;
            }
        }
        if (owned->priority != 1 || owned->x != 0 || owned->y != 0) { error = "KWin did not make the virtual output the primary workspace at the desktop origin"; return false; }
        return true;
    }
    bool matches(int width, int height, int scale) const {
        if (!owned || owned->removed || !owned->current || !owned->enabled ||
            !owned->replication.isEmpty() || owned->transform != 0) return false;
        const auto mode = owned->modes.find(owned->current);
        return mode != owned->modes.end() && !mode->second.removed &&
            mode->second.size == QSize(width, height) && owned->scale == scale;
    }
    bool apply(kde_output_configuration_v2* config) {
        applied = 0;
        static const kde_output_configuration_v2_listener listener = {
            [](void* p, kde_output_configuration_v2*) { static_cast<Display*>(p)->applied = 1; },
            [](void* p, kde_output_configuration_v2*) { static_cast<Display*>(p)->applied = -1; },
            [](void* p, kde_output_configuration_v2*, const char* reason) { static_cast<Display*>(p)->error = QString::fromUtf8(reason); }
        };
        kde_output_configuration_v2_add_listener(config, &listener, this);
        kde_output_configuration_v2_apply(config);
        bool ok = wait([&] { return applied != 0; });
        kde_output_configuration_v2_destroy(config);
        if (!ok) return false;
        if (applied != 1) { if (error.isEmpty()) error = "KWin rejected the virtual output mode"; return false; }
        return sync();
    }
    bool resize(int width, int height, int scale) {
        error.clear();
        if (broken || !owned || owned->removed) { error = "Virtual output is unavailable"; return false; }
        if (matches(width, height, scale)) return true;
        // The custom-mode list and selected mode are separate atomic transactions.
        // Keep the old mode until the new one has been advertised and selected.
        auto modes = kde_output_management_v2_create_mode_list(management);
        auto add = [&](QSize size) {
            kde_mode_list_v2_set_resolution(modes, uint32_t(size.width()), uint32_t(size.height()));
            kde_mode_list_v2_set_refresh_rate(modes, 60000);
            kde_mode_list_v2_set_reduced_blanking(modes, 1);
            kde_mode_list_v2_add_mode(modes);
        };
        const auto old = owned->modes.find(owned->current);
        if (old != owned->modes.end() && old->second.size != QSize(width, height)) add(old->second.size);
        add(QSize(width, height));
        auto config = kde_output_management_v2_create_configuration(management);
        kde_output_configuration_v2_set_custom_modes(config, owned->proxy, modes);
        const bool added = apply(config);
        kde_mode_list_v2_destroy(modes);
        if (!added || !sync()) return false;
        kde_output_device_mode_v2* selected = nullptr;
        for (const auto& mode : owned->modes) if (!mode.second.removed && mode.second.size == QSize(width, height) && mode.second.refresh == 60000) selected = mode.first;
        if (!selected) { error = "KWin did not advertise the requested virtual mode"; return false; }
        config = kde_output_management_v2_create_configuration(management);
        kde_output_configuration_v2_enable(config, owned->proxy, 1);
        kde_output_configuration_v2_set_replication_source(config, owned->proxy, "");
        kde_output_configuration_v2_transform(config, owned->proxy, 0);
        kde_output_configuration_v2_mode(config, owned->proxy, selected);
        kde_output_configuration_v2_scale(config, owned->proxy, wl_fixed_from_int(scale));
        if (!apply(config)) return false;
        if (!matches(width, height, scale)) { error = "KWin mode acknowledgment did not match requested pixels and scale"; return false; }
        return true;
    }
    bool configureWorkspace(int width, int height, int scale, int policy) {
        layoutWarning.clear();
        if (!beginSession(width,height,policy) || !resize(width,height,scale)) return false;
        if (!mirror(policy)) { deferLocalLayout(); if (broken) return false; }
        // Local policy submission can change the owned mode too. Acknowledging
        // success always requires a final independent capture-target check.
        return resize(width,height,scale);
    }
    QString localLayoutWarning() const { return layoutWarning; }
    QString outputName() const { return owned ? owned->name : QString(); }
    QString lastError() const { return error; }
    int fd() const { return wl_display_get_fd(display); }
    bool dispatch() { return !broken && wl_display_dispatch(display) >= 0 && !broken; }
    bool healthy() const { return !broken; }
private:
    static int modeEvent(const void*, void* target, uint32_t, const wl_message* message, wl_argument* args) {
        auto out = static_cast<Output*>(wl_proxy_get_user_data(static_cast<wl_proxy*>(target)));
        auto& mode = out->modes[static_cast<kde_output_device_mode_v2*>(target)];
        if (!strcmp(message->name, "size")) mode.size = QSize(args[0].i, args[1].i);
        else if (!strcmp(message->name, "refresh")) mode.refresh = args[0].i;
        else if (!strcmp(message->name, "removed")) mode.removed = true;
        return 0;
    }
    static int outputEvent(const void*, void* target, uint32_t, const wl_message* message, wl_argument* args) {
        auto out = static_cast<Output*>(wl_proxy_get_user_data(static_cast<wl_proxy*>(target)));
        if (!strcmp(message->name, "name")) out->name = QString::fromUtf8(args[0].s);
        else if (!strcmp(message->name, "uuid")) out->uuid = QString::fromUtf8(args[0].s);
        else if (!strcmp(message->name, "replication_source")) out->replication = QString::fromUtf8(args[0].s);
        else if (!strcmp(message->name, "priority")) out->priority = args[0].u;
        else if (!strcmp(message->name, "enabled")) out->enabled = args[0].i;
        else if (!strcmp(message->name, "geometry")) { out->x = args[0].i; out->y = args[1].i; out->transform = args[7].i; }
        else if (!strcmp(message->name, "scale")) out->scale = wl_fixed_to_double(args[0].f);
        else if (!strcmp(message->name, "current_mode")) out->current = reinterpret_cast<kde_output_device_mode_v2*>(args[0].o);
        else if (!strcmp(message->name, "mode")) {
            auto mode = reinterpret_cast<kde_output_device_mode_v2*>(args[0].o);
            out->modes.emplace(mode, Mode{});
            wl_proxy_add_dispatcher(reinterpret_cast<wl_proxy*>(mode), modeEvent, nullptr, out);
        }
        return 0;
    }
};

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.value(1)=="--text-caret") return runTextCaretProbe(args);
    if (args.size() == 3 && args[1] == "--restore-kwin") {
        Display restore;
        if (!restore.connectSession()) return 1;
        QJsonArray state;
        QByteArray buffer;
        char byte;
        while (true) {
            const auto count = read(STDIN_FILENO, &byte, 1);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) break;
            if (byte != '\n') { buffer.append(byte); if (buffer.size() > 65536) return 2; continue; }
            QJsonParseError parse;
            const auto document = QJsonDocument::fromJson(buffer, &parse); buffer.clear();
            if (parse.error != QJsonParseError::NoError || !document.isArray()) return 2;
            state = document.array();
            fputs("ready\n", stdout); fflush(stdout);
        }
        if (state.isEmpty()) return 0;
        const bool ok = restore.waitForRemoval(args[2]) && restore.restore(state);
        if (!ok) { restore.baselineForDiagnostics(state); restore.recordLayoutEvent(restore.lastError()); }
        return ok ? 0 : 1;
    }
    if (args.size() != 3 || !Display::valid(args[1].toInt(), args[2].toInt(), 1)) {
        reply({{"error", "Expected width and height (640x360 through 7680x4320, aligned to four)"}}); return 2;
    }
    if (qgetenv("XDG_CURRENT_DESKTOP").split(':').contains("GNOME")) return runGnomeDisplay(args[1].toInt(), args[2].toInt());
    Display display;
    if (!display.start(args[1].toInt(), args[2].toInt())) { reply({{"error", display.lastError()}}); return 1; }
    if (qEnvironmentVariableIntValue("DESKPORT_DISPLAY_ON_DEMAND") == 1)
        reply({{"ready", true}, {"active", false}, {"outputName", display.name}});
    else reply({{"displayId", 1}, {"outputName", display.outputName()}, {"width", args[1].toInt()}, {"height", args[2].toInt()}, {"scale", 1}});
    QByteArray buffer;
    while (display.healthy()) {
        pollfd fds[] = {{STDIN_FILENO, POLLIN, 0}, {display.fd(), POLLIN, 0}};
        const int result = poll(fds, 2, -1);
        if (result < 0 && errno == EINTR) continue;
        if (result < 0) return 1;
        if (fds[1].revents && (!(fds[1].revents & POLLIN) || !display.dispatch())) return 1;
        if (fds[0].revents) {
            char input[4096]; const auto count = read(STDIN_FILENO, input, sizeof(input));
            if (count <= 0) return 0;
            buffer.append(input, int(count));
            if (buffer.size() > 8192) return 2;
            while (buffer.contains('\n')) {
                const int end = buffer.indexOf('\n');
                const auto request = QJsonDocument::fromJson(buffer.left(end)).object(); buffer.remove(0, end + 1);
                const int seq = request["seq"].toInt(), width = request["width"].toInt(), height = request["height"].toInt(), scale = request["scale"].toInt();
                const int policy = request.contains("displayPolicy") ? request["displayPolicy"].toInt(-1) : 0;
                QJsonObject response{{"seq", seq}};
                if (policy < 0 || policy > 2 || !seq || !Display::valid(width, height, scale)) response["error"] = "Invalid virtual output request";
                else if (!(request["session"].toBool(true)
                         ? display.configureWorkspace(width, height, scale, policy)
                         : display.restoreIdle())) response["error"] = display.lastError();
                else { response["outputName"] = display.outputName(); response["displayId"] = request["session"].toBool(true) ? 1 : 0; response["width"] = width; response["height"] = height; response["scale"] = scale; response["active"] = request["session"].toBool(true);
                    if (!display.localLayoutWarning().isEmpty()) response["layoutWarning"] = display.localLayoutWarning(); }
                reply(response);
                if (!display.healthy()) return 1; // Never accept a late acknowledgment after a timeout.
            }
        }
    }
    return 1;
}
