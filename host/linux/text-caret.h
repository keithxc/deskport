// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../common/caretgeometry.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusArgument>
#include <QDBusVariant>
#include <QDBusObjectPath>
#include <QElapsedTimer>
#include <QSet>
#include <QVector>
#include <QJsonDocument>
#include <cstdio>

struct CaretAccessible { QString bus; QDBusObjectPath path; };
Q_DECLARE_METATYPE(CaretAccessible)
inline QDBusArgument& operator<<(QDBusArgument& a,const CaretAccessible& v) {
    a.beginStructure(); a<<v.bus<<v.path; a.endStructure(); return a;
}
inline const QDBusArgument& operator>>(const QDBusArgument& a,CaretAccessible& v) {
    a.beginStructure(); a>>v.bus>>v.path; a.endStructure(); return a;
}
// A short-lived, read-only AT-SPI probe. Never fetch names, text, values or
// selections, and never enable accessibility or change global desktop settings.
inline int runTextCaretProbe(const QStringList& args) {
    QJsonObject result{{"caret",QJsonObject{{"valid",false}}}};
    const auto finish=[&]{const auto bytes=QJsonDocument(result).toJson(QJsonDocument::Compact); fwrite(bytes.constData(),1,size_t(bytes.size()),stdout); return 0;};
    if(args.size()!=6 && args.size()!=8) return finish();
    QRectF screen(args[2].toDouble(),args[3].toDouble(),args[4].toDouble(),args[5].toDouble());
    if(!screen.isValid()) return finish();
    QElapsedTimer budget; budget.start();
    auto addressMessage=QDBusMessage::createMethodCall("org.a11y.Bus","/org/a11y/bus","org.a11y.Bus","GetAddress");
    const auto address=QDBusConnection::sessionBus().call(addressMessage,QDBus::Block,40);
    if(address.type()==QDBusMessage::ErrorMessage || address.arguments().size()!=1) return finish();
    auto bus=QDBusConnection::connectToBus(address.arguments()[0].toString(),"caret-probe");
    auto call=[&](const CaretAccessible& obj,const QString& iface,const QString& method,const QVariantList& values=QVariantList{}) {
        if(budget.elapsed()>280) return QDBusMessage();
        auto m=QDBusMessage::createMethodCall(obj.bus,obj.path.path(),iface,method); m.setArguments(values);
        return bus.call(m,QDBus::Block,30);
    };
    const QString accessible="org.a11y.atspi.Accessible", text="org.a11y.atspi.Text";
    const auto states=[&](const CaretAccessible& obj) {
        auto r=call(obj,accessible,"GetState");
        return r.arguments().size()==1 ? qdbus_cast<QList<uint>>(r.arguments()[0]) : QList<uint>{};
    };
    const auto has=[](const QList<uint>& s,int bit){return s.size()>bit/32 && (s[bit/32]&(1u<<(bit%32)));};
    const auto activeAncestor=[&](CaretAccessible obj) {
        for(int i=0;i<32 && budget.elapsed()<280;++i) {
            const auto s=states(obj); if(has(s,1)) return true;
            const auto parent=call(obj,"org.freedesktop.DBus.Properties","Get",{accessible,"Parent"});
            if(parent.arguments().size()!=1) return false;
            obj=qdbus_cast<CaretAccessible>(qvariant_cast<QDBusVariant>(parent.arguments()[0]).variant());
            if(obj.bus.isEmpty() || obj.path.path()=="/org/a11y/atspi/null") return false;
        }
        return false;
    };
    CaretAccessible focus;
    if(args.size()==8) {
        CaretAccessible candidate{args[6],QDBusObjectPath(args[7])};
        const auto s=states(candidate);
        if(has(s,12) && has(s,25) && !has(s,6) && activeAncestor(candidate)) focus=candidate;
    }
    if(focus.bus.isEmpty()) {
        QVector<QPair<CaretAccessible,int>> pending{{{"org.a11y.atspi.Registry",QDBusObjectPath("/org/a11y/atspi/accessible/root")},0}};
        QSet<QString> seen;
        for(int i=0;i<pending.size() && i<512 && budget.elapsed()<280;++i) {
            const auto obj=pending[i].first; const int depth=pending[i].second;
            const auto key=obj.bus+obj.path.path(); if(seen.contains(key)) continue; seen.insert(key);
            const auto s=states(obj);
            if(has(s,6) || (depth==2 && !has(s,1))) continue;
            if(has(s,12) && has(s,25)) {focus=obj; break;}
            // Desktop and application roots may omit SHOWING; other invisible
            // subtrees cannot contain a usable caret.
            if(depth>1 && !has(s,25)) continue;
            if(depth>=32) continue;
            const auto r=call(obj,accessible,"GetChildren");
            if(r.arguments().size()!=1) continue;
            const auto children=qdbus_cast<QList<CaretAccessible>>(r.arguments()[0]);
            for(const auto& child:children) {if(pending.size()>=512) break; pending.append({child,depth+1});}
        }
    }
    if(focus.bus.isEmpty()) return finish();
    result["bus"]=focus.bus; result["path"]=focus.path.path();
    // CaretOffset is an offset only; it discloses no document content.
    const auto r=call(focus,"org.freedesktop.DBus.Properties","Get",{text,"CaretOffset"});
    if(r.arguments().size()!=1) return finish();
    bool ok=false; const int offset=qvariant_cast<QDBusVariant>(r.arguments()[0]).variant().toInt(&ok);
    if(!ok || offset<0) return finish();
    auto extents=call(focus,text,"GetRangeExtents",{offset,offset,uint(0)});
    if(extents.arguments().size()!=4 || extents.arguments()[3].toInt()<=0)
        extents=call(focus,text,"GetCharacterExtents",{offset,uint(0)});
    // At document end some providers cannot describe the empty insertion
    // range. The preceding glyph is a nearby line anchor, not an inferred
    // trailing edge (which would be wrong for wrapping/bidirectional text).
    if((extents.arguments().size()!=4 || extents.arguments()[3].toInt()<=0) && offset>0)
        extents=call(focus,text,"GetCharacterExtents",{offset-1,uint(0)});
    if(extents.arguments().size()!=4) return finish();
    const auto a=extents.arguments();
    // Use the insertion line/nearby glyph area. Do not guess a previous glyph's
    // trailing edge at wrapped line ends or in bidirectional text.
    const QRectF caret(a[0].toInt(),a[1].toInt(),a[2].toInt(),a[3].toInt());
    const auto finalState=states(focus);
    if(has(finalState,12) && has(finalState,25) && !has(finalState,6)) result["caret"]=caretGeometry(caret,screen);
    return finish();
}
