#include "sleepmonitor.h"

#ifdef Q_OS_LINUX
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDBusVariant>
#endif
#include <QtDebug>

#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
void* deskportWatchSleep(SleepMonitor*);
void* deskportWatchLock(ScreenLockMonitor*);
void deskportUnwatchLifecycle(void*);
#endif

std::atomic<bool> SleepMonitor::s_Asleep{false};
#ifdef Q_OS_LINUX

namespace {
const QString Service = QStringLiteral("org.freedesktop.login1");
const QString Path = QStringLiteral("/org/freedesktop/login1");
const QString Interface = QStringLiteral("org.freedesktop.login1.Manager");
}

#endif
SleepMonitor::SleepMonitor(QObject* parent, bool native) : QObject(parent), m_Subscribe(native)
{
    if (!native) return;
#ifdef Q_OS_LINUX
    auto bus = QDBusConnection::systemBus();
    if (!bus.isConnected() ||
        !bus.connect(Service, Path, Interface, QStringLiteral("PrepareForSleep"), this, SLOT(prepareForSleep(bool)))) {
        qWarning() << "logind sleep notifications are unavailable";
        return;
    }
    acquire();
#elif defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    m_Native = deskportWatchSleep(this);
#endif
}
SleepMonitor::~SleepMonitor() {
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    deskportUnwatchLifecycle(m_Native);
#endif
}

void SleepMonitor::acquire()
{
    if (!m_Subscribe) return;
#ifdef Q_OS_LINUX
    if (m_Inhibitor.isValid()) return;
    auto call = QDBusMessage::createMethodCall(Service, Path, Interface, QStringLiteral("Inhibit"));
    call << QStringLiteral("sleep") << QStringLiteral("DeskPort")
         << QStringLiteral("Release remote input before sleep") << QStringLiteral("delay");
    QDBusReply<QDBusUnixFileDescriptor> reply = QDBusConnection::systemBus().call(call, QDBus::Block, 2000);
    if (reply.isValid()) m_Inhibitor = reply.value();
    else qWarning() << "Unable to take a logind sleep delay:" << reply.error().message();
#endif
}

void SleepMonitor::release()
{
    // QDBusUnixFileDescriptor owns a duplicate; dropping it closes the lock.
#ifdef Q_OS_LINUX
    m_Inhibitor = QDBusUnixFileDescriptor();
#endif
}

void SleepMonitor::prepareForSleep(bool start)
{
    if (s_Asleep.exchange(start) == start) return;
    if (start) {
        emit sleeping();
    } else {
        acquire();
        emit resumed();
    }
}

std::atomic<bool> ScreenLockMonitor::s_Locked{false};

#ifdef Q_OS_LINUX
namespace {
const QString PropertiesInterface = QStringLiteral("org.freedesktop.DBus.Properties");
const QString SessionInterface = QStringLiteral("org.freedesktop.login1.Session");

QDBusReply<QDBusVariant> loginProperty(const QString& path, const QString& interface, const QString& name)
{
    auto call = QDBusMessage::createMethodCall(Service, path, PropertiesInterface, QStringLiteral("Get"));
    call << interface << name;
    return QDBusConnection::systemBus().call(call, QDBus::Block, 1000);
}
}

#endif
ScreenLockMonitor::ScreenLockMonitor(QObject* parent, bool native) : QObject(parent)
{
    if (!native) return;
#ifdef Q_OS_LINUX
    auto bus = QDBusConnection::systemBus();
    if (!bus.isConnected()) return;
    const auto display = loginProperty(QStringLiteral("/org/freedesktop/login1/user/self"),
                                  QStringLiteral("org.freedesktop.login1.User"), QStringLiteral("Display"));
    if (!display.isValid()) {
        qWarning() << "Screen lock state is unavailable:" << display.error().message();
        return;
    }
    QString id;
    QDBusObjectPath path;
    const auto argument = qvariant_cast<QDBusArgument>(display.value().variant());
    argument.beginStructure();
    argument >> id >> path;
    argument.endStructure();
    if (id.isEmpty() || path.path().isEmpty() || path.path() == QLatin1String("/")) {
        qWarning() << "Screen lock state is unavailable: no graphical session";
        return;
    }
    m_Session = path.path();
    if (!bus.connect(Service, m_Session, PropertiesInterface, QStringLiteral("PropertiesChanged"), this,
                     SLOT(propertiesChanged(QString,QVariantMap,QStringList)))) {
        qWarning() << "Screen lock notifications are unavailable";
        return;
    }
    refresh();
#elif defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    m_Native = deskportWatchLock(this);
#endif
}
ScreenLockMonitor::~ScreenLockMonitor() {
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    deskportUnwatchLifecycle(m_Native);
#endif
}
void ScreenLockMonitor::setLocked(bool locked) {
    if (s_Locked.exchange(locked) != locked) emit changed(locked);
}

void ScreenLockMonitor::refresh()
{
#ifdef Q_OS_LINUX
    const auto reply = loginProperty(m_Session, SessionInterface, QStringLiteral("LockedHint"));
    if (reply.isValid()) setLocked(reply.value().variant().toBool());
#endif
}

void ScreenLockMonitor::propertiesChanged(const QString& interface, const QVariantMap& changed, const QStringList& invalidated)
{
#ifdef Q_OS_LINUX
    if (interface != SessionInterface) return;
    const auto key = QStringLiteral("LockedHint");
    if (changed.contains(key)) setLocked(changed.value(key).toBool());
    else if (invalidated.contains(key)) refresh();
    else return;
    qInfo() << "Screen" << (s_Locked ? "locked" : "unlocked");
#else
    Q_UNUSED(interface); Q_UNUSED(changed); Q_UNUSED(invalidated);
#endif
}
