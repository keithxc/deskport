#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QVariantMap>
#include <QDBusUnixFileDescriptor>
#include <QDBusVariant>
#include <QDBusVirtualObject>
#include <QTimer>
#include <fcntl.h>
#include <unistd.h>

struct LoginDisplay { QString id; QDBusObjectPath path; };
Q_DECLARE_METATYPE(LoginDisplay)
QDBusArgument& operator<<(QDBusArgument& argument, const LoginDisplay& display) {
    argument.beginStructure(); argument << display.id << display.path; argument.endStructure();
    return argument;
}
const QDBusArgument& operator>>(const QDBusArgument& argument, LoginDisplay& display) {
    argument.beginStructure(); argument >> display.id >> display.path; argument.endStructure();
    return argument;
}

// A private-bus logind fixture. No request is sent to the machine's real logind.
class LogindFixture : public QDBusVirtualObject {
public:
    QString introspect(const QString&) const override { return {}; }
    bool handleMessage(const QDBusMessage& message, const QDBusConnection& bus) override {
        const auto method = message.member();
        const auto arguments = message.arguments();
        if (method == "Inhibit") {
            ++inhibitors;
            int fd = ::open("/dev/null", O_RDONLY);
            QDBusUnixFileDescriptor descriptor(fd); ::close(fd);
            bus.send(message.createReply(QVariantList{QVariant::fromValue(descriptor)}));
        } else if (method == "Get" && arguments.value(1).toString() == "Display") {
            const LoginDisplay display{QString("fixture"), QDBusObjectPath("/org/freedesktop/login1/session/fixture")};
            bus.send(message.createReply(QVariantList{QVariant::fromValue(QDBusVariant(QVariant::fromValue(display)))}));
        } else if (method == "Get" && arguments.value(1).toString() == "LockedHint") {
            bus.send(message.createReply(QVariantList{QVariant::fromValue(QDBusVariant(locked))}));
        } else if (method == "SetSleep") {
            auto event = QDBusMessage::createSignal("/org/freedesktop/login1", "org.freedesktop.login1.Manager", "PrepareForSleep");
            event << arguments.value(0).toBool(); bus.send(event); bus.send(message.createReply());
        } else if (method == "SetLock") {
            locked = arguments.value(0).toBool();
            auto event = QDBusMessage::createSignal("/org/freedesktop/login1/session/fixture", "org.freedesktop.DBus.Properties", "PropertiesChanged");
            event << QString("org.freedesktop.login1.Session") << QVariantMap{{"LockedHint",locked}} << QStringList{};
            bus.send(event); bus.send(message.createReply());
        } else if (method == "InhibitCount") bus.send(message.createReply(QVariantList{inhibitors}));
        else return false;
        return true;
    }
private:
    bool locked = false;
    int inhibitors = 0;
};
int runLogindFixture(QCoreApplication& app) {
    if (qEnvironmentVariable("DESKPORT_LIFECYCLE_PRIVATE_BUS") != "1") return 2;
    qDBusRegisterMetaType<LoginDisplay>();
    auto bus = QDBusConnection::systemBus();
    LogindFixture fixture;
    if (!bus.registerService("org.freedesktop.login1") ||
        !bus.registerVirtualObject("/org/freedesktop/login1", &fixture, QDBusConnection::SubPath)) return 3;
    QTimer::singleShot(30000, &app, &QCoreApplication::quit);
    return app.exec();
}
