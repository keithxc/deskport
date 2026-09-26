#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSet>
#include <QVariantMap>
#if defined(Q_OS_WIN)
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#elif defined(Q_OS_MACOS)
#include <libproc.h>
#include <vector>
#elif defined(Q_OS_LINUX)
#include <unistd.h>
#endif

// A best-effort resident-memory sample, never a heap/leak measurement. Query only
// this application and its immediate children; do not include host-launched apps.
namespace ProcessMemory {
inline qint64 resident(qint64 pid) {
    if (pid <= 0) return -1;
#if defined(Q_OS_WIN)
    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, DWORD(pid));
    if (!process) return -1;
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    const bool ok = GetProcessMemoryInfo(process, &counters, sizeof(counters));
    CloseHandle(process);
    return ok ? qint64(counters.WorkingSetSize) : -1;
#elif defined(Q_OS_MACOS)
    proc_taskinfo info{};
    return proc_pidinfo(int(pid), PROC_PIDTASKINFO, 0, &info, sizeof(info)) == sizeof(info)
        ? qint64(info.pti_resident_size) : -1;
#elif defined(Q_OS_LINUX)
    QFile file(QStringLiteral("/proc/%1/statm").arg(pid));
    if (!file.open(QIODevice::ReadOnly)) return -1;
    const auto fields = file.readAll().simplified().split(' ');
    bool ok = false;
    const auto pages = fields.value(1).toLongLong(&ok);
    const auto pageSize = sysconf(_SC_PAGESIZE);
    return ok && pages >= 0 && pageSize > 0 ? pages * pageSize : -1;
#else
    return -1;
#endif
}

inline QSet<qint64> children(qint64 parent, bool& ok) {
    QSet<qint64> result;
    ok = true;
#if defined(Q_OS_WIN)
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) { ok = false; return result; }
    PROCESSENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32First(snapshot, &entry)) ok = false;
    else do {
        if (entry.th32ParentProcessID == DWORD(parent)) result.insert(entry.th32ProcessID);
    } while (Process32Next(snapshot, &entry));
    CloseHandle(snapshot);
#elif defined(Q_OS_MACOS)
    // proc_listchildpids returns bytes, including when querying the buffer size.
    const int bytes = proc_listchildpids(int(parent), nullptr, 0);
    if (bytes < 0) { ok = false; return result; }
    std::vector<pid_t> pids(size_t(bytes / sizeof(pid_t)) + 32);
    const int read = proc_listchildpids(int(parent), pids.data(), int(pids.size() * sizeof(pid_t)));
    if (read < 0) { ok = false; return result; }
    if (read == int(pids.size() * sizeof(pid_t))) ok = false;
    for (int i = 0; i < read / int(sizeof(pid_t)); ++i) if (pids[i] > 0) result.insert(pids[i]);
#elif defined(Q_OS_LINUX)
    const QDir tasks(QStringLiteral("/proc/%1/task").arg(parent));
    const auto entries = tasks.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    if (entries.isEmpty()) ok = false;
    for (const auto& task : entries) {
        QFile file(tasks.filePath(task + QStringLiteral("/children")));
        if (!file.open(QIODevice::ReadOnly)) { ok = false; continue; }
        for (const auto& item : file.readAll().simplified().split(' ')) {
            bool valid = false;
            const auto pid = item.toLongLong(&valid);
            if (valid && pid > 0) result.insert(pid);
        }
    }
#else
    Q_UNUSED(parent);
    ok = false;
#endif
    return result;
}

inline QVariantMap sample(qint64 hostPid) {
    const auto self = QCoreApplication::applicationPid();
    bool complete = true;
    auto pids = children(self, complete);
    if (hostPid > 0 && hostPid != self) pids.insert(hostPid);
    pids.remove(self);
    const qint64 client = resident(self);
    qint64 host = 0, helpers = 0;
    int sampled = client >= 0 ? 1 : 0;
    complete &= client >= 0;
    for (auto pid : pids) {
        const auto bytes = resident(pid);
        if (bytes < 0) { complete = false; if (pid == hostPid) host = -1; continue; }
        ++sampled;
        if (pid == hostPid) host = bytes;
        else helpers += bytes;
    }
    return {{"client", client}, {"host", host}, {"helpers", helpers},
            {"total", qMax(qint64(0), client) + qMax(qint64(0), host) + helpers},
            {"available", client >= 0}, {"complete", complete}, {"processes", sampled}};
}
}
