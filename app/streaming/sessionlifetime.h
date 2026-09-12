#pragma once

#include <QObject>
#include <QQmlEngine>

// The page can disappear while exec() or transport cleanup still uses the
// session. All methods run on the object's Qt thread; cleanup arrives queued.
class SessionLifetime {
public:
    explicit SessionLifetime(QObject* owner) : m_Owner(owner) {}
    void beginExec() {
        QQmlEngine::setObjectOwnership(m_Owner, QQmlEngine::CppOwnership);
        m_Executing = true;
    }
    void endExec() {
        m_Executing = false;
        disposeIfReady();
    }
    void cleanupFinished() {
        m_Cleaned = true;
        disposeIfReady();
    }
private:
    void disposeIfReady() {
        if (m_Cleaned && !m_Executing) m_Owner->deleteLater();
    }
    QObject* m_Owner;
    bool m_Executing = false;
    bool m_Cleaned = false;
};
