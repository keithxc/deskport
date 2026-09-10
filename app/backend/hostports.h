#pragma once

#include <QHostAddress>
#include <QList>
#include <QNetworkProxy>
#include <QTcpServer>
#include <QUdpSocket>
#include <memory>
#include <vector>

namespace DeskPortNetwork {
constexpr int DefaultBasePort = 48989;
constexpr int PortStep = 100;
constexpr int PortChoices = 20;
// Sunshine v2026.906.222525 port offsets. Reserve the unused microphone slot too.
inline QList<int> tcpOffsets() { return {-5, 0, 1, 21}; }
inline QList<int> udpOffsets() { return {9, 10, 11, 13}; }
inline bool isPrivateBase(int port) {
    return port >= DefaultBasePort && port < DefaultBasePort + PortStep * PortChoices &&
        (port - DefaultBasePort) % PortStep == 0;
}
}

// Hold the complete IPv4 family while preparing our host. Never reuse another
// listener's sockets. Sunshine uses IPv4 explicitly in the generated config.
class HostPortReservation {
public:
    int reserve(int preferred, const QHostAddress &address = QHostAddress::AnyIPv4) {
        release();
        QList<int> candidates;
        if (DeskPortNetwork::isPrivateBase(preferred)) candidates.append(preferred);
        for (int i = 0; i < DeskPortNetwork::PortChoices; ++i) {
            const int port = DeskPortNetwork::DefaultBasePort + i * DeskPortNetwork::PortStep;
            if (!candidates.contains(port)) candidates.append(port);
        }
        for (int base : candidates) {
            if (tryReserve(base, address)) return base;
            release();
        }
        return 0;
    }
    void release() { m_Tcp.clear(); m_Udp.clear(); }
private:
    bool tryReserve(int base, const QHostAddress &address) {
        for (int offset : DeskPortNetwork::tcpOffsets()) {
            std::unique_ptr<QTcpServer> socket(new QTcpServer);
            socket->setProxy(QNetworkProxy::NoProxy);
            if (!socket->listen(address, quint16(base + offset))) return false;
            m_Tcp.push_back(std::move(socket));
        }
        for (int offset : DeskPortNetwork::udpOffsets()) {
            std::unique_ptr<QUdpSocket> socket(new QUdpSocket);
            socket->setProxy(QNetworkProxy::NoProxy);
            if (!socket->bind(address, quint16(base + offset), QAbstractSocket::DontShareAddress)) return false;
            m_Udp.push_back(std::move(socket));
        }
        return true;
    }
    std::vector<std::unique_ptr<QTcpServer>> m_Tcp;
    std::vector<std::unique_ptr<QUdpSocket>> m_Udp;
};
