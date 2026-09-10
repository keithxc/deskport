#pragma once

#include <QHostAddress>
#include <QNetworkInterface>

namespace DeskPortNetwork {
// Compare numeric interface addresses only. Names and shared public/NAT addresses
// are not evidence that two hosts are the same machine.
inline bool isLocalHostAddress(const QString& value,
                               const QList<QHostAddress>& interfaces = QNetworkInterface::allAddresses())
{
    const QHostAddress address(value);
    if (address.isNull() || address == QHostAddress::AnyIPv4 || address == QHostAddress::AnyIPv6)
        return false;
    if (address.isLoopback()) return true;
    for (const auto& local : interfaces) {
        if (address.isEqual(local, QHostAddress::ConvertV4MappedToIPv4)) return true;
    }
    return false;
}
}
