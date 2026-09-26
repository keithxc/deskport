// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <functional>

// A provider may hang inside another application's accessibility implementation.
// Keep it outside both the UI process and the display/topology owner.
class HostCaretMonitor : public QObject {
    QProcess probe;
    QTimer tick, deadline;
    QByteArray output;
    bool enabled=false, accepted=false;
    QString executable;
    QStringList cache;
    std::function<QStringList()> arguments;
    std::function<void(const QJsonObject&)> report;
public:
    explicit HostCaretMonitor(QObject* parent, std::function<void(const QJsonObject&)> callback)
        : QObject(parent), report(std::move(callback)) {
        tick.setInterval(200); deadline.setSingleShot(true); deadline.setInterval(400);
        connect(&tick,&QTimer::timeout,this,[this] {
            if (!enabled || probe.state()!=QProcess::NotRunning) return;
            const auto args=arguments();
            if (args.isEmpty()) { cache.clear(); report({{"valid",false}}); return; }
            output.clear(); accepted=true;
            probe.start(executable,QStringList{"--text-caret"}+args+cache);
            deadline.start();
        });
        connect(&deadline,&QTimer::timeout,this,[this] {
            accepted=false; cache.clear(); probe.kill();
            if(enabled) report({{"valid",false}});
        });
        connect(&probe,&QProcess::readyReadStandardOutput,this,[this] {
            output+=probe.readAllStandardOutput();
            if(output.size()>4096) { accepted=false; output.clear(); probe.kill(); }
        });
        // Do not record accessibility-provider output in diagnostics.
        connect(&probe,&QProcess::readyReadStandardError,this,[this]{ probe.readAllStandardError(); });
        connect(&probe,&QProcess::errorOccurred,this,[this](QProcess::ProcessError) {
            accepted=false; deadline.stop(); cache.clear();
            if(enabled) report({{"valid",false}});
        });
        connect(&probe,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus status) {
            deadline.stop(); output+=probe.readAllStandardOutput();
            if(!enabled || !accepted) return;
            const auto result=output.size()<=4096 && code==0 && status==QProcess::NormalExit
                ? QJsonDocument::fromJson(output).object() : QJsonObject{};
            cache.clear();
            const auto bus=result["bus"].toString(), path=result["path"].toString();
            if(bus.startsWith(':') && bus.size()<128 && path.startsWith("/org/a11y/") && path.size()<1024) cache={bus,path};
            const auto caret=result["caret"].toObject();
            report(caret.isEmpty() ? QJsonObject{{"valid",false}} : caret);
        });
    }
    void stop() { enabled=false; accepted=false; tick.stop(); deadline.stop(); cache.clear(); probe.kill(); report({{"valid",false}}); }
    void start(const QString& helper,std::function<QStringList()> args) { executable=helper; arguments=std::move(args); enabled=true; tick.start(); }
    ~HostCaretMonitor() override { enabled=false; probe.kill(); probe.waitForFinished(500); }
};
