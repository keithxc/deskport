#include "sleepmonitor.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QtDebug>

namespace {
const QString Service = QStringLiteral("org.freedesktop.login1");
const QString Path = QStringLiteral("/org/freedesktop/login1");
const QString Interface = QStringLiteral("org.freedesktop.login1.Manager");
}

SleepMonitor::SleepMonitor(QObject* parent) : QObject(parent)
{
    auto bus = QDBusConnection::systemBus();
    if (!bus.isConnected() ||
        !bus.connect(Service, Path, Interface, QStringLiteral("PrepareForSleep"), this, SLOT(prepareForSleep(bool)))) {
        qWarning() << "logind sleep notifications are unavailable";
        return;
    }
    acquire();
}

void SleepMonitor::acquire()
{
    if (m_Inhibitor.isValid()) return;
    auto call = QDBusMessage::createMethodCall(Service, Path, Interface, QStringLiteral("Inhibit"));
    call << QStringLiteral("sleep") << QStringLiteral("DeskPort")
         << QStringLiteral("Release remote input before sleep") << QStringLiteral("delay");
    QDBusReply<QDBusUnixFileDescriptor> reply = QDBusConnection::systemBus().call(call, QDBus::Block, 2000);
    if (reply.isValid()) m_Inhibitor = reply.value();
    else qWarning() << "Unable to take a logind sleep delay:" << reply.error().message();
}

void SleepMonitor::release()
{
    // QDBusUnixFileDescriptor owns a duplicate; dropping it closes the lock.
    m_Inhibitor = QDBusUnixFileDescriptor();
}

void SleepMonitor::prepareForSleep(bool start)
{
    if (start) {
        emit sleeping();
    } else {
        acquire();
        emit resumed();
    }
}
