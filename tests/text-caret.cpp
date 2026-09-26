// SPDX-License-Identifier: GPL-3.0-or-later
#include "../host/linux/text-caret.h"
#include "../app/backend/hostcaretmonitor.h"
#include <QCoreApplication>
#include <QTest>
#include <limits>
#include <cstdlib>
#include <QDBusVirtualObject>
#include <QDBusMetaType>
#include <QDBusError>
#define require(condition) do { if(!(condition)) {fprintf(stderr,"caret check failed at line %d: %s\n",__LINE__,#condition); std::abort();} } while(false)
class FakeAccessibility : public QDBusVirtualObject {
public:
    QString address, service;
    bool active=true, showing=true, endOfDocument=false;
    int offset=7, forbidden=0;
    QString introspect(const QString&) const override {return {};}
    bool handleMessage(const QDBusMessage& m,const QDBusConnection& c) override {
        QVariantList values;
        const auto path=m.path();
        if(m.interface()=="org.a11y.Bus" && m.member()=="GetAddress") values={address};
        else if(m.member()=="GetState") {
            uint state=0;
            if(path.endsWith("/window") && active) state|=1u<<1;
            if(path.endsWith("/text")) state|=1u<<12;
            if(showing) state|=1u<<25;
            values={QVariant::fromValue(QList<uint>{state,0})};
        } else if(m.member()=="GetChildren") {
            QList<CaretAccessible> children;
            const auto child=path.endsWith("/root") ? "app" : path.endsWith("/app") ? "window" : path.endsWith("/window") ? "text" : "";
            if(*child) children.append({service,QDBusObjectPath(QString("/org/a11y/atspi/accessible/")+child)});
            values={QVariant::fromValue(children)};
        } else if(m.interface()=="org.freedesktop.DBus.Properties" && m.member()=="Get") {
            if(m.arguments().value(1)=="CaretOffset") values={QVariant::fromValue(QDBusVariant(offset))};
            else if(m.arguments().value(1)=="Parent") {
                CaretAccessible parent{service,QDBusObjectPath("/org/a11y/atspi/accessible/window")};
                values={QVariant::fromValue(QDBusVariant(QVariant::fromValue(parent)))};
            } else ++forbidden;
        } else if(m.member()=="GetRangeExtents" || m.member()=="GetCharacterExtents") {
            const int requested=m.arguments().first().toInt();
            require(requested==offset || (endOfDocument && requested==offset-1));
            values=endOfDocument && requested==offset ? QVariantList{0,0,0,0} : QVariantList{250,requested*100,0,20};
        } else ++forbidden;
        c.send(m.createReply(values)); return true;
    }
};
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv); const auto args=app.arguments();
    if(args.value(1)=="--text-caret") {
        if(args.value(2)=="hang") {QTest::qWait(3000); return 0;}
        if(args.value(2)=="good") {puts("{\"caret\":{\"valid\":true,\"x\":0.25,\"y\":0.75}}"); return 0;}
        if(args.value(2)=="oversized") {for(int i=0;i<8192;++i) putchar('x');return 0;}
        return runTextCaretProbe(args);
    }
    const QRectF screen(-1920,-100,1920,1080);
    auto caret=caretGeometry(QRectF(-960,690,0,20),screen);
    require(caret["valid"].toBool()); require(caret["x"].toDouble()==0.5); require(caret["y"].toDouble()==0.75);
    require(!caretGeometry(QRectF(50,50,0,20),screen)["valid"].toBool());
    require(!caretGeometry(QRectF(-960,690,0,0),screen)["valid"].toBool());
    require(!caretGeometry(QRectF(-960,690,0,20),QRectF())["valid"].toBool());
    require(!caretGeometry(QRectF(std::numeric_limits<double>::quiet_NaN(),10,1,20),screen)["valid"].toBool());
    require(!caretGeometry(QRectF(-960,979,1,20),screen)["valid"].toBool());
    int valid=0,invalid=0;
    HostCaretMonitor monitor(nullptr,[&](const QJsonObject& value){value["valid"].toBool()?++valid:++invalid;});
    const auto binary=QCoreApplication::applicationFilePath();
    monitor.start(binary,[]{return QStringList{"good"};});
    QTest::qWait(600); require(valid>0);
    monitor.stop(); const int stopped=valid; QTest::qWait(500); require(valid==stopped);
    monitor.start(binary,[]{return QStringList{"hang"};});
    const int previous=invalid; QTest::qWait(850); require(invalid>previous); require(valid==stopped);
    monitor.stop(); monitor.start(binary,[]{return QStringList{"good"};});
    QTest::qWait(650); require(valid>stopped); // Old timed-out process cannot poison a new session.
    monitor.stop(); const int beforeOversize=valid;
    monitor.start(binary,[]{return QStringList{"oversized"};}); QTest::qWait(650);
    require(valid==beforeOversize); monitor.stop();
    if(qEnvironmentVariableIsSet("DESKPORT_TEST_DBUS_ADDRESS")) {
        qDBusRegisterMetaType<CaretAccessible>(); qDBusRegisterMetaType<QList<CaretAccessible>>();
        qDBusRegisterMetaType<QList<uint>>();
        const auto address=qEnvironmentVariable("DESKPORT_TEST_DBUS_ADDRESS");
        auto bus=QDBusConnection::connectToBus(address,"caret-test-service"); if(!bus.isConnected()) fprintf(stderr,"bus: %s\n",qPrintable(bus.lastError().message())); require(bus.isConnected());
        FakeAccessibility fake; fake.address=address; fake.service=bus.baseService();
        require(bus.registerService("org.a11y.Bus")); require(bus.registerService("org.a11y.atspi.Registry"));
        require(bus.registerVirtualObject("/org/a11y",&fake,QDBusConnection::SubPath));
        auto query=[&](bool cached) {
            QProcess child; auto env=QProcessEnvironment::systemEnvironment(); env.insert("DBUS_SESSION_BUS_ADDRESS",address);child.setProcessEnvironment(env);
            QStringList values{"--text-caret","0","0","1000","1000"};
            if(cached) values<<fake.service<<"/org/a11y/atspi/accessible/text";
            child.start(binary,values);
            for(int i=0;i<1500 && child.state()!=QProcess::NotRunning;++i) { QCoreApplication::processEvents(); QThread::msleep(1); }
            require(child.state()==QProcess::NotRunning); require(child.exitCode()==0);
            return QJsonDocument::fromJson(child.readAllStandardOutput()).object()["caret"].toObject();
        };
        auto first=query(false); require(first["valid"].toBool()); require(first["y"].toDouble()==0.72);
        fake.offset=8; auto moved=query(true); require(moved["valid"].toBool()); require(moved["y"].toDouble()==0.82);
        fake.endOfDocument=true; auto end=query(true); require(end["valid"].toBool()); require(end["y"].toDouble()==0.72);
        fake.endOfDocument=false;
        fake.active=false; require(!query(true)["valid"].toBool());
        fake.active=true; fake.showing=false; require(!query(false)["valid"].toBool());
        require(fake.forbidden==0);
        puts("PASS isolated AT-SPI discovery, cached moving caret, inactive and hidden focus, geometry-only queries");
    }
    puts("PASS caret geometry, isolated probe timeout, session stop/restart, bounded output");
}
