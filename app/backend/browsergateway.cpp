#include <QCoreApplication>
#include "browsergateway.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHostInfo>
#include <QJsonDocument>
#include <QMessageAuthenticationCode>
#include <QNetworkInterface>
#include <QNetworkProxy>
#include <QPointer>
#include <QProcess>
#include <QTimer>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QStandardPaths>
#include <QTcpServer>
#include <QUrl>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/x509v3.h>
#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

namespace {
constexpr int MaxHeaders = 16384;
constexpr int MaxBody = 262144;
const QByteArray CookieName("__Host-deskport-session");
const QByteArray PairingCookieName("__Host-deskport-pairing");
constexpr qint64 PairingLifetimeSeconds = BrowserGateway::RememberSeconds;
// Pairings saved before the time-based code (version 1, one year) are dropped.
constexpr int PairingFileVersion = 2;
constexpr int CodeStepSeconds = 30;
constexpr qint64 CodeFailureWindowMs = 15 * 60 * 1000;
constexpr qint64 CodeLockMs = 15 * 60 * 1000;
constexpr int MaxPairings = 32;
constexpr int MaxPairingFile = 32768;
QByteArray cookie(const QByteArray& name, const QByteArray& token, qint64 age = -1) {
    auto value = name + '=' + token + "; Secure; HttpOnly; SameSite=Strict; Path=/";
    if (age >= 0) value += "; Max-Age=" + QByteArray::number(age);
    return value;
}
bool validDeviceName(const QString& name) {
    if (name.isEmpty() || name.size() > 64 || name != name.trimmed()) return false;
    return std::none_of(name.begin(), name.end(), [](QChar c) { return c.category() == QChar::Other_Control; });
}
qint64 now() {
    // Wall-clock corrections must not extend credentials or replenish retries.
    static QElapsedTimer clock;
    if (!clock.isValid()) clock.start();
    return clock.elapsed() + 1;
}
QByteArray randomToken() {
    quint32 words[8];
    QRandomGenerator::system()->generate(words, words + 8);
    return QByteArray(reinterpret_cast<const char*>(words), sizeof(words))
        .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}
const char Base32[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
QString base32(const QByteArray& data) {
    QString out; quint32 buffer = 0; int bits = 0;
    for (const char byte : data) {
        buffer = (buffer << 8) | quint8(byte); bits += 8;
        while (bits >= 5) { out += QLatin1Char(Base32[(buffer >> (bits - 5)) & 31]); bits -= 5; }
    }
    if (bits > 0) out += QLatin1Char(Base32[(buffer << (5 - bits)) & 31]);
    return out;
}
QByteArray fromBase32(const QString& text) {
    QByteArray out; quint32 buffer = 0; int bits = 0;
    for (const QChar c : text) {
        const char* found = std::strchr(Base32, c.toLatin1());
        if (!c.toLatin1() || !found) return {};
        buffer = (buffer << 5) | quint32(found - Base32); bits += 5;
        if (bits >= 8) { out += char((buffer >> (bits - 8)) & 0xff); bits -= 8; }
    }
    return out;
}
QByteArray newSecret() {
    QByteArray secret(20, Qt::Uninitialized);
    QRandomGenerator::system()->generate(reinterpret_cast<quint32*>(secret.data()),
                                         reinterpret_cast<quint32*>(secret.data()) + 5);
    return secret;
}
QString tokenKey(const QByteArray& token) {
    return QString::fromLatin1(QCryptographicHash::hash(token, QCryptographicHash::Sha256).toHex());
}
QByteArray cookieToken(const QByteArray& cookies, const QByteArray& name = CookieName) {
    QByteArray token;
    bool present = false;
    for (const auto& value : cookies.split(';')) {
        const auto cookie = value.trimmed();
        if (cookie.startsWith(name + '=')) {
            if (present) return {};
            present = true;
            token = cookie.mid(name.size() + 1);
        }
    }
    return QRegularExpression("^[A-Za-z0-9_-]{43}$").match(QString::fromLatin1(token)).hasMatch() ? token : QByteArray();
}
QString cookieKey(const QByteArray& cookies, const QByteArray& name = CookieName) {
    const auto token = cookieToken(cookies, name);
    return token.isEmpty() ? QString() : tokenKey(token);
}
bool equal(const QByteArray& a, const QByteArray& b) {
    return a.size() == b.size() && !a.isEmpty() && CRYPTO_memcmp(a.constData(), b.constData(), size_t(a.size())) == 0;
}
bool localAddress(const QHostAddress& address) {
    if (address.isLoopback()) return true;
    if (address.protocol() != QAbstractSocket::IPv4Protocol) return false;
    const quint32 value = address.toIPv4Address();
    return (value & 0xff000000u) == 0x0a000000u || (value & 0xfff00000u) == 0xac100000u ||
        (value & 0xffff0000u) == 0xc0a80000u || (value & 0xffff0000u) == 0xa9fe0000u;
}
bool sharedAddressSpace(const QHostAddress& address) {
    // RFC 6598 100.64.0.0/10, used by Tailscale/Headscale tailnets and by carrier NAT.
    return address.protocol() == QAbstractSocket::IPv4Protocol && (address.toIPv4Address() & 0xffc00000u) == 0x64400000u;
}
QList<QHostAddress> lanAddresses() {
    QList<QHostAddress> addresses{QHostAddress::LocalHost};
    auto interfaces = QNetworkInterface::allInterfaces();
    // Prefer the ordinary Wi-Fi/Ethernet entry for the QR, ahead of VPN and
    // container bridges that a tablet on the LAN usually cannot reach.
    const auto physical = [](const QNetworkInterface& interface) {
        return interface.type() == QNetworkInterface::Wifi || interface.type() == QNetworkInterface::Ethernet;
    };
    std::stable_sort(interfaces.begin(), interfaces.end(), [&](const auto& a, const auto& b) {
        return physical(a) > physical(b);
    });
    for (const auto& interface : interfaces) {
        if (!(interface.flags() & QNetworkInterface::IsUp) || !(interface.flags() & QNetworkInterface::IsRunning)) continue;
        const bool pointToPoint = interface.flags().testFlag(QNetworkInterface::IsPointToPoint);
        for (const auto& entry : interface.addressEntries()) {
            const auto address = entry.ip();
            const bool overlay = BrowserGateway::overlayAddress(address, interface.name(), pointToPoint, physical(interface));
            if (address.protocol() == QAbstractSocket::IPv4Protocol && (localAddress(address) || overlay) &&
                !addresses.contains(address))
                addresses.append(address);
        }
    }
    return addresses;
}
QString tailscaleProgram(const QString& configured) {
    if (!configured.isEmpty()) return QFileInfo(configured).isExecutable() ? configured : QString();
    const auto found = QStandardPaths::findExecutable("tailscale");
    if (!found.isEmpty()) return found;
    // GUI and launchd sessions often lack the shell PATH.
    for (const char* path : {"/run/current-system/sw/bin/tailscale", "/usr/local/bin/tailscale", "/opt/homebrew/bin/tailscale",
                             "/usr/bin/tailscale", "/Applications/Tailscale.app/Contents/MacOS/Tailscale"})
        if (QFileInfo(QString::fromLatin1(path)).isExecutable()) return QString::fromLatin1(path);
    return {};
}
// Runs one bounded local Tailscale query; any failure yields an empty object.
void tailscaleJson(QObject* context, const QString& program, const QStringList& arguments,
                   std::function<void(QJsonObject)> done) {
    auto process = new QProcess(context);
    auto finished = std::make_shared<bool>(false);
    auto complete = [process, done, finished](const QJsonObject& result) {
        if (*finished) return;
        *finished = true; process->deleteLater(); done(result);
    };
    QObject::connect(process, &QProcess::finished, context, [process, complete](int code, QProcess::ExitStatus status) {
        const auto output = process->readAllStandardOutput();
        complete(status == QProcess::NormalExit && code == 0 && output.size() < 4 * 1024 * 1024
            ? QJsonDocument::fromJson(output).object() : QJsonObject());
    });
    QObject::connect(process, &QProcess::errorOccurred, context, [complete](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) complete({});
    });
    QTimer::singleShot(4000, process, [process, complete] { process->kill(); complete({}); });
    process->start(program, arguments);
}
QString listenerUrl(const QHostAddress& address, int port) {
    QUrl url; url.setScheme("https"); url.setHost(address.toString()); url.setPort(port); url.setPath("/");
    return url.toString();
}
QByteArray readBounded(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) && file.size() <= 1024 * 1024 ? file.readAll() : QByteArray();
}
bool privateWrite(const QString& path, const QByteArray& bytes) {
    if (QFileInfo(path).isSymLink()) return false;
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.setPermissions(QFile::ReadOwner | QFile::WriteOwner) &&
        file.write(bytes) == bytes.size() && file.commit();
}
bool createCertificate(const QString& certPath, const QString& keyPath, const QStringList& hosts) {
    EVP_PKEY_CTX* context = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    EVP_PKEY* key = nullptr;
    X509* certificate = X509_new();
    bool ok = context && certificate && EVP_PKEY_keygen_init(context) > 0 &&
        EVP_PKEY_CTX_set_rsa_keygen_bits(context, 2048) > 0 && EVP_PKEY_keygen(context, &key) > 0;
    if (context) EVP_PKEY_CTX_free(context);
    if (ok) {
        unsigned char serial[16] = {};
        ok = RAND_bytes(serial, sizeof(serial)) == 1;
        serial[0] &= 0x7f;
        BIGNUM* number = BN_bin2bn(serial, sizeof(serial), nullptr);
        ASN1_INTEGER* integer = number ? BN_to_ASN1_INTEGER(number, nullptr) : nullptr;
        ok = ok && integer && X509_set_serialNumber(certificate, integer) == 1;
        BN_free(number); ASN1_INTEGER_free(integer);
        ok = ok && X509_set_version(certificate, 2) == 1 && X509_set_pubkey(certificate, key) == 1 &&
            X509_gmtime_adj(X509_getm_notBefore(certificate), -300) &&
            X509_gmtime_adj(X509_getm_notAfter(certificate), 397L * 24 * 60 * 60);
        X509_NAME* subject = X509_get_subject_name(certificate);
        const unsigned char commonName[] = "DeskPort Browser";
        ok = ok && X509_NAME_add_entry_by_txt(subject, "CN", MBSTRING_ASC, commonName, -1, -1, 0) == 1 &&
            X509_set_issuer_name(certificate, subject) == 1;
        QStringList names;
        for (const auto& host : hosts) {
            const QHostAddress address(host);
            names.append((address.isNull() ? QStringLiteral("DNS:") : QStringLiteral("IP:")) + host);
        }
        const auto san = names.join(',').toLatin1();
        X509V3_CTX extensionContext;
        X509V3_set_ctx(&extensionContext, certificate, certificate, nullptr, nullptr, 0);
        const QList<QPair<int, QByteArray>> extensions{
            {NID_subject_alt_name, san}, {NID_basic_constraints, "critical,CA:FALSE"},
            {NID_key_usage, "critical,digitalSignature,keyEncipherment"}, {NID_ext_key_usage, "serverAuth"}};
        for (const auto& value : extensions) {
            X509_EXTENSION* extension = X509V3_EXT_conf_nid(nullptr, &extensionContext, value.first,
                const_cast<char*>(value.second.constData()));
            ok = ok && extension && X509_add_ext(certificate, extension, -1) == 1;
            X509_EXTENSION_free(extension);
        }
        ok = ok && X509_sign(certificate, key, EVP_sha256()) > 0;
    }
    BIO* certBuffer = BIO_new(BIO_s_mem());
    BIO* keyBuffer = BIO_new(BIO_s_mem());
    ok = ok && certBuffer && keyBuffer && PEM_write_bio_X509(certBuffer, certificate) == 1 &&
        PEM_write_bio_PrivateKey(keyBuffer, key, nullptr, nullptr, 0, nullptr, nullptr) == 1;
    if (ok) {
        char *certData = nullptr, *keyData = nullptr;
        const long certSize = BIO_get_mem_data(certBuffer, &certData);
        const long keySize = BIO_get_mem_data(keyBuffer, &keyData);
        ok = privateWrite(keyPath, QByteArray(keyData, int(keySize))) && privateWrite(certPath, QByteArray(certData, int(certSize)));
    }
    BIO_free(certBuffer); BIO_free(keyBuffer); X509_free(certificate); EVP_PKEY_free(key);
    return ok;
}
bool matchingKey(const QByteArray& certBytes, const QByteArray& keyBytes) {
    BIO* certBuffer = BIO_new_mem_buf(certBytes.constData(), certBytes.size());
    BIO* keyBuffer = BIO_new_mem_buf(keyBytes.constData(), keyBytes.size());
    X509* certificate = certBuffer ? PEM_read_bio_X509(certBuffer, nullptr, nullptr, nullptr) : nullptr;
    EVP_PKEY* key = keyBuffer ? PEM_read_bio_PrivateKey(keyBuffer, nullptr, nullptr, nullptr) : nullptr;
    const bool ok = certificate && key && X509_check_private_key(certificate, key) == 1;
    X509_free(certificate); EVP_PKEY_free(key); BIO_free(certBuffer); BIO_free(keyBuffer);
    return ok;
}
class Listener : public QTcpServer {
public:
    explicit Listener(QObject* parent) : QTcpServer(parent) {}
    std::function<void(qintptr)> incoming;
protected:
    void incomingConnection(qintptr descriptor) override { incoming(descriptor); }
};
}

struct BrowserGateway::Connection : QObject {
    explicit Connection(QObject* parent) : QObject(parent) {}
    QSslSocket* socket = nullptr;
    QByteArray buffer, method, path, body;
    QHash<QByteArray, QByteArray> headers;
    int contentLength = -1;
    bool dispatched = false, replied = false;
};

bool BrowserGateway::tailnetListening() const {
    return std::any_of(m_Listeners.cbegin(), m_Listeners.cend(),
        [](const QTcpServer* listener) { return sharedAddressSpace(listener->serverAddress()); });
}
bool BrowserGateway::tailnetEligible(Connection* connection) const {
    if (!m_Options.tailnetIdentity) return false;
    const auto peer = connection->socket->peerAddress();
    return (sharedAddressSpace(peer) && sharedAddressSpace(connection->socket->localAddress())) ||
        (m_Options.testLoopbackTailnet && peer.isLoopback());
}
void BrowserGateway::tailnetOwner(const QHostAddress& peer, std::function<void(bool)> done) {
    if (m_Hooks.tailnetOwner) { m_Hooks.tailnetOwner(peer, std::move(done)); return; }
    const auto program = tailscaleProgram(m_Options.tailscaleProgram);
    if (program.isEmpty()) { done(false); return; }
    // tailscaled authenticated the WireGuard peer, so its address identifies a
    // node. Allow only an untagged node owned by the same user as this untagged host.
    const QString address = peer.toString();
    tailscaleJson(this, program, {"status", "--json", "--peers=false"}, [this, program, address, done](QJsonObject status) {
        const auto self = status.value("Self").toObject();
        const auto user = self.value("UserID").toVariant().toLongLong();
        if (user <= 0 || !self.value("Tags").toArray().isEmpty()) { done(false); return; }
        tailscaleJson(this, program, {"whois", "--json", address}, [user, address, done](QJsonObject whois) {
            const auto node = whois.value("Node").toObject();
            bool listed = false;
            for (const auto& value : node.value("Addresses").toArray())
                if (value.toString().section('/', 0, 0) == address) listed = true;
            done(listed && node.value("Tags").toArray().isEmpty() && node.value("User").toVariant().toLongLong() == user);
        });
    });
}
bool BrowserGateway::overlayAddress(const QHostAddress& address, const QString& interfaceName,
                                    bool pointToPoint, bool physical) {
    if (physical || !sharedAddressSpace(address)) return false;
    return pointToPoint || interfaceName.startsWith("tailscale") || interfaceName.startsWith("utun");
}
BrowserGateway::BrowserGateway(const Hooks& hooks, QObject* parent) : BrowserGateway(hooks, Options(), parent) {}
BrowserGateway::BrowserGateway(const Hooks& hooks, const Options& options, QObject* parent)
    : QObject(parent), m_Hooks(hooks), m_Options(options) {
    m_Directory = options.stateDirectory.isEmpty() ?
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/browser" : options.stateDirectory;
    m_GlobalRate.credits = 12;
    m_Cleanup.setInterval(1000);
    connect(&m_Cleanup, &QTimer::timeout, this, &BrowserGateway::cleanup);
    m_CodeTick.setSingleShot(true);
    connect(&m_CodeTick, &QTimer::timeout, this, [this] {
        if (m_LockedUntil && m_LockedUntil <= now()) m_LockedUntil = 0;
        if (active()) scheduleCodeTick();
        emit changed();
    });
}
BrowserGateway::~BrowserGateway() { stop(); }

bool BrowserGateway::saveSecret(const QByteArray& secret) {
    const QString path = m_Directory + "/browser.ini";
    if (QFileInfo(path).isSymLink()) return false;
    if (!QFileInfo::exists(path) && !privateWrite(path, {})) return false;
    if (!QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner)) return false;
    QSettings settings(path, QSettings::IniFormat);
    settings.remove("access/code"); // The fixed code is retired by the time-based code.
    settings.setValue("access/secret", base32(secret));
    settings.sync();
    return settings.status() == QSettings::NoError && QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
}
bool BrowserGateway::savePairings(const QHash<QString, Pairing>& pairings) {
    const QFileInfo directory(m_Directory), file(m_Directory + "/pairings.json");
    bool permitted = directory.isDir() && !directory.isSymLink() && !file.isSymLink() &&
        (!file.exists() || file.isFile()) && pairings.size() <= MaxPairings;
#ifdef Q_OS_UNIX
    permitted = permitted && directory.ownerId() == ::getuid() &&
        (!file.exists() || file.ownerId() == ::getuid());
#endif
    QJsonArray entries;
    auto ids = pairings.keys(); std::sort(ids.begin(), ids.end());
    for (const auto& id : ids) {
        const auto& entry = pairings[id];
        entries.append(QJsonObject{{"id", entry.id}, {"name", entry.name}, {"hash", entry.hash},
            {"createdAt", entry.createdAt}, {"lastSeenAt", entry.lastSeenAt}, {"expiresAt", entry.expiresAt}});
    }
    const auto bytes = QJsonDocument(QJsonObject{{"version", PairingFileVersion}, {"devices", entries}}).toJson(QJsonDocument::Compact);
    if (!permitted || bytes.size() > MaxPairingFile || !privateWrite(file.filePath(), bytes)) {
        m_Error = tr("Cannot save browser pairings. The requested change was not applied.");
        emit changed(); return false;
    }
    m_Error.clear();
    return true;
}
bool BrowserGateway::loadPairings() {
    m_Pairings.clear();
    const QFileInfo info(m_Directory + "/pairings.json");
    if (!info.exists() && !info.isSymLink()) return savePairings({});
    const auto invalid = [this] {
        m_Error = tr("Browser pairing settings are invalid or inaccessible; restore them before enabling browser access.");
        return false;
    };
    if (info.isSymLink() || !info.isFile() || info.size() > MaxPairingFile) return invalid();
#ifdef Q_OS_UNIX
    if (info.ownerId() != ::getuid()) return invalid();
#endif
    if (!QFile::setPermissions(info.filePath(), QFile::ReadOwner | QFile::WriteOwner)) return invalid();
    QFile file(info.filePath());
    if (!file.open(QIODevice::ReadOnly)) return invalid();
    const auto bytes = file.read(MaxPairingFile + 1);
    if (bytes.size() > MaxPairingFile || !file.atEnd()) return invalid();
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    const auto object = document.object();
    // Upgrading from fixed-code pairings forgets every remembered browser once.
    if (error.error == QJsonParseError::NoError && document.isObject() && object.value("version") == QJsonValue(1))
        return savePairings({});
    if (error.error != QJsonParseError::NoError || !document.isObject() || object.size() != 2 ||
        object.value("version") != QJsonValue(PairingFileVersion) || !object.value("devices").isArray() ||
        object.value("devices").toArray().size() > MaxPairings) return invalid();
    QHash<QString, Pairing> loaded;
    QSet<QString> hashes;
    const auto timestamp = [](const QJsonValue& value) -> qint64 {
        const double number = value.toDouble(-1);
        return value.isDouble() && number > 0 && number <= 9007199254740991.0 && std::floor(number) == number
            ? qint64(number) : -1;
    };
    for (const auto& value : object.value("devices").toArray()) {
        const auto row = value.toObject();
        Pairing entry{row.value("id").toString(), row.value("name").toString(), row.value("hash").toString(),
            timestamp(row.value("createdAt")), timestamp(row.value("lastSeenAt")), timestamp(row.value("expiresAt"))};
        if (!value.isObject() || row.size() != 6 ||
            !QRegularExpression("^[a-f0-9]{32}$").match(entry.id).hasMatch() ||
            !QRegularExpression("^[a-f0-9]{64}$").match(entry.hash).hasMatch() ||
            !validDeviceName(entry.name) || entry.createdAt <= 0 || entry.lastSeenAt < entry.createdAt ||
            entry.lastSeenAt > entry.expiresAt || entry.expiresAt - entry.createdAt != PairingLifetimeSeconds * 1000 ||
            loaded.contains(entry.id) || hashes.contains(entry.hash)) return invalid();
        loaded.insert(entry.id, entry); hashes.insert(entry.hash);
    }
    m_Pairings = loaded;
    return true;
}
QJsonArray BrowserGateway::pairedBrowsers() const {
    QJsonArray entries;
    auto ids = m_Pairings.keys(); std::sort(ids.begin(), ids.end());
    const auto current = QDateTime::currentMSecsSinceEpoch();
    for (const auto& id : ids) {
        const auto& entry = m_Pairings[id];
        if (entry.expiresAt <= current) continue;
        entries.append(QJsonObject{{"id", entry.id}, {"name", entry.name},
            {"createdAt", entry.createdAt}, {"lastSeenAt", entry.lastSeenAt}});
    }
    return entries;
}
void BrowserGateway::releasePairing(const QString& id) {
    const auto keys = m_Sessions.keys();
    for (const auto& key : keys) if (m_Sessions.value(key).pairingId == id) {
        release(key); m_Sessions.remove(key);
    }
}
bool BrowserGateway::revokeBrowser(const QString& id) {
    if (!m_Pairings.contains(id)) {
        m_Error = tr("This browser pairing no longer exists."); emit changed(); return false;
    }
    auto updated = m_Pairings; updated.remove(id);
    // Durable revocation precedes disconnect. A failed write must not claim
    // success, then silently restore this credential on the next restart.
    if (!savePairings(updated)) return false;
    m_Pairings = updated;
    releasePairing(id);
    emit changed(); return true;
}
QString BrowserGateway::pairingFor(Connection* connection) const {
    const auto hash = cookieKey(connection->headers.value("cookie"), PairingCookieName);
    if (hash.isEmpty()) return {};
    const auto current = QDateTime::currentMSecsSinceEpoch();
    for (const auto& entry : m_Pairings)
        if (entry.expiresAt > current && equal(entry.hash.toLatin1(), hash.toLatin1())) return entry.id;
    return {};
}
bool BrowserGateway::sessionAvailable(Connection* connection) {
    const auto previous = cookieKey(connection->headers.value("cookie"));
    if (m_Sessions.size() - int(m_Sessions.contains(previous)) < 32) return true;
    error(connection, 503, "busy", "Too many browser sessions are open."); return false;
}
void BrowserGateway::createSession(Connection* connection, const QString& pairingId, const QList<QByteArray>& cookies, bool reuse) {
    const auto previous = cookieKey(connection->headers.value("cookie"));
    const auto existing = m_Sessions.constFind(previous);
    const bool retain = reuse && existing != m_Sessions.constEnd() && existing->pairingId == pairingId &&
        now() - existing->lastSeen <= m_Options.sessionIdleSeconds * 1000LL && now() - existing->created <= 8 * 60 * 60 * 1000LL;
    QByteArray token;
    Session session;
    if (retain) {
        // Tabs share cookies. Merely opening another paired tab must not stop
        // the current controller or invalidate its in-memory CSRF token.
        session = *existing; token = cookieToken(connection->headers.value("cookie"));
        session.lastSeen = now();
    } else {
        if (!previous.isEmpty()) { release(previous); m_Sessions.remove(previous); }
        token = randomToken();
        session.id = "browser-" + tokenKey(randomToken()).left(32);
        session.csrf = QString::fromLatin1(randomToken()); session.pairingId = pairingId;
        session.created = session.lastSeen = now();
    }
    m_Sessions.insert(tokenKey(token), session);
    QJsonValue pairing(QJsonValue::Null);
    if (!pairingId.isEmpty()) {
        const auto entry = m_Pairings.value(pairingId);
        pairing = QJsonObject{{"id", entry.id}, {"name", entry.name}};
    }
    auto responseCookies = cookies;
    responseCookies.append(cookie(CookieName, token));
    respond(connection, 200, QJsonDocument(QJsonObject{{"ok", true}, {"csrfToken", session.csrf},
        {"paired", !pairingId.isEmpty()}, {"pairing", pairing}}).toJson(QJsonDocument::Compact),
        "application/json", responseCookies);
}
bool BrowserGateway::loadCredentials(const QList<QHostAddress>& addresses) {
    Q_UNUSED(addresses);
    const QFileInfo directory(m_Directory);
    if (directory.isSymLink() || !QDir().mkpath(m_Directory) ||
        !QFile::setPermissions(m_Directory, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)) {
        m_Error = tr("Cannot create a private browser settings directory."); return false;
    }
#ifdef Q_OS_UNIX
    if (QFileInfo(m_Directory).ownerId() != ::getuid()) { m_Error = tr("Browser settings must belong to the current user."); return false; }
#endif
    const QString settingsPath = m_Directory + "/browser.ini";
    if (QFileInfo(settingsPath).isSymLink()) { m_Error = tr("Browser settings cannot be a symbolic link."); return false; }
    QSettings settings(settingsPath, QSettings::IniFormat);
    const auto stored = settings.value("access/secret").toString();
    if (settings.status() != QSettings::NoError || (!stored.isEmpty() &&
        (!QRegularExpression("^[A-Z2-7]{32}$").match(stored).hasMatch() || fromBase32(stored).size() != 20))) {
        m_Error = tr("Browser access settings are invalid; restore or explicitly reset them."); return false;
    }
    m_Secret = stored.isEmpty() ? newSecret() : fromBase32(stored);
    if (!saveSecret(m_Secret)) { m_Error = tr("Cannot save the browser access code."); return false; }
    return loadCertificate();
}
bool BrowserGateway::loadCertificate() {
    const bool custom = !m_Options.certificatePath.isEmpty() || !m_Options.privateKeyPath.isEmpty();
    if (custom && (m_Options.certificatePath.isEmpty() || m_Options.privateKeyPath.isEmpty())) {
        m_Error = tr("A custom HTTPS certificate requires both certificate and private key paths."); return false;
    }
    const QString certPath = custom ? m_Options.certificatePath : m_Directory + "/https-cert.pem";
    const QString keyPath = custom ? m_Options.privateKeyPath : m_Directory + "/https-key.pem";
    if (!custom && (QFileInfo(certPath).isSymLink() || QFileInfo(keyPath).isSymLink())) {
        m_Error = tr("Generated HTTPS credentials cannot be symbolic links."); return false;
    }
    if (!custom && !QFileInfo::exists(certPath) && !QFileInfo::exists(keyPath) &&
        !createCertificate(certPath, keyPath, m_Hosts)) {
        m_Error = tr("Cannot generate the browser HTTPS certificate."); return false;
    }
    if (!custom && QFileInfo::exists(certPath) && QFileInfo::exists(keyPath)) {
        // A generated identity must name every address it serves, including a
        // Tailscale address that appeared later. Keep earlier names so a
        // returning DHCP or tailnet address does not force another trust step.
        const auto existing = QSslCertificate::fromData(readBounded(certPath));
        if (!existing.isEmpty() && matchingKey(readBounded(certPath), readBounded(keyPath))) {
            QStringList covered;
            const auto names = existing.first().subjectAlternativeNames();
            for (auto it = names.cbegin(); it != names.cend(); ++it) covered.append(it.value().toLower());
            QStringList missing;
            for (const auto& host : m_Hosts) if (!covered.contains(host)) missing.append(host);
            if (!missing.isEmpty()) {
                QStringList hosts = m_Hosts;
                for (const auto& name : covered) if (!hosts.contains(name) && hosts.size() < MaxCertificateNames) hosts.append(name);
                if (!createCertificate(certPath, keyPath, hosts)) {
                    m_Error = tr("Cannot extend the browser HTTPS certificate to a new address."); return false;
                }
            }
        }
    }
    const auto certificate = readBounded(certPath), key = readBounded(keyPath);
    m_Certificates = QSslCertificate::fromData(certificate);
    m_PrivateKey = QSslKey(key, QSsl::Rsa);
    if (m_PrivateKey.isNull()) m_PrivateKey = QSslKey(key, QSsl::Ec);
    const auto current = QDateTime::currentDateTimeUtc();
    if (m_Certificates.isEmpty() || m_PrivateKey.isNull() || !matchingKey(certificate, key) ||
        m_Certificates.first().effectiveDate() > current || m_Certificates.first().expiryDate() <= current) {
        m_Error = tr("Browser HTTPS credentials are missing, expired, or do not match."); return false;
    }
    if (!custom && (!QFile::setPermissions(certPath, QFile::ReadOwner | QFile::WriteOwner) ||
                    !QFile::setPermissions(keyPath, QFile::ReadOwner | QFile::WriteOwner))) {
        m_Error = tr("Cannot protect browser HTTPS credentials."); return false;
    }
    return true;
}

bool BrowserGateway::start() {
    if (active()) return true;
    m_Error.clear();
    const auto addresses = m_Options.listenAddresses.isEmpty() ? lanAddresses() : m_Options.listenAddresses;
    m_Hosts = {"localhost"};
    const QString hostname = QHostInfo::localHostName().toLower();
    if (QRegularExpression("^[a-z0-9][a-z0-9.-]{0,252}$").match(hostname).hasMatch()) {
        m_Hosts.append(hostname);
        if (!hostname.contains('.')) m_Hosts.append(hostname + ".local");
    }
    for (const auto& address : addresses) {
        if ((!localAddress(address) && !sharedAddressSpace(address)) || address.isNull()) {
            m_Error = tr("The browser listener requires an explicit LAN, Tailscale or loopback address."); emit changed(); return false;
        }
        m_Hosts.append(address.toString().toLower());
    }
    for (const auto& host : m_Options.allowedHosts) {
        if (!QRegularExpression("^[A-Za-z0-9][A-Za-z0-9.-]{0,252}$").match(host).hasMatch()) {
            m_Error = tr("Invalid browser HTTPS hostname."); emit changed(); return false;
        }
        m_Hosts.append(host.toLower());
    }
    m_Hosts.removeDuplicates();
    if (!loadCredentials(addresses) || !loadPairings()) { emit changed(); return false; }
    m_Port = m_Options.port;
    for (const auto& address : addresses) {
        if (!addListener(address)) { stop(); emit changed(); return false; }
    }
    if (!m_Options.certificatePath.isEmpty() && !m_Options.privateKeyPath.isEmpty()) {
        QStringList preferred;
        for (const auto& host : m_Options.allowedHosts) {
            if (!QHostAddress(host).isNull()) continue;
            QUrl url; url.setScheme("https"); url.setHost(host.toLower()); url.setPort(m_Port); url.setPath("/");
            if (!preferred.contains(url.toString())) preferred.append(url.toString());
        }
        m_Urls = preferred + m_Urls;
    }
    m_Cleanup.start(); scheduleCodeTick(); emit changed(); return true;
}
bool BrowserGateway::addListener(const QHostAddress& address) {
    auto listener = new Listener(this);
    listener->setProxy(QNetworkProxy::NoProxy);
    listener->incoming = [this](qintptr descriptor) { accept(descriptor); };
    if (!listener->listen(address, quint16(m_Port))) {
        m_Error = tr("Browser HTTPS could not listen on %1:%2: %3. Existing services were left unchanged.")
            .arg(address.toString()).arg(m_Port).arg(listener->errorString());
        delete listener; return false;
    }
    m_Port = listener->serverPort();
    m_Listeners.append(listener);
    const auto url = listenerUrl(address, m_Port);
    // Preserve interface preference; loopback stays last in the UI.
    if (address.isLoopback()) m_Urls.append(url);
    else {
        auto beforeLoopback = std::find_if(m_Urls.begin(), m_Urls.end(), [](const QString& entry) {
            return QHostAddress(QUrl(entry).host()).isLoopback();
        });
        m_Urls.insert(beforeLoopback, url);
    }
    return true;
}
void BrowserGateway::refreshAddresses() {
    // Automatic listeners follow interface changes, such as Tailscale starting
    // after DeskPort at login. Existing connections and sessions are untouched.
    if (!active() || !m_Options.listenAddresses.isEmpty()) return;
    const auto current = lanAddresses();
    bool updated = false;
    for (int i = m_Listeners.size() - 1; i >= 0; --i) {
        const auto address = m_Listeners[i]->serverAddress();
        if (current.contains(address)) continue;
        m_Listeners[i]->close(); delete m_Listeners[i]; m_Listeners.removeAt(i);
        m_Urls.removeAll(listenerUrl(address, m_Port));
        m_Hosts.removeAll(address.toString().toLower());
        updated = true;
    }
    QList<QHostAddress> added;
    for (const auto& address : current) {
        const bool bound = std::any_of(m_Listeners.cbegin(), m_Listeners.cend(),
            [&](const QTcpServer* listener) { return listener->serverAddress() == address; });
        if (!bound) added.append(address);
    }
    if (!added.isEmpty()) {
        const auto previousHosts = m_Hosts;
        for (const auto& address : added) m_Hosts.append(address.toString().toLower());
        m_Hosts.removeDuplicates();
        if (!loadCertificate()) m_Hosts = previousHosts;
        else for (const auto& address : added) {
            if (addListener(address)) updated = true;
            else m_Hosts.removeAll(address.toString().toLower());
        }
    }
    if (updated) emit changed();
}
void BrowserGateway::stop() {
    m_Cleanup.stop(); m_CodeTick.stop();
    const auto sessions = m_Sessions.keys();
    for (const auto& key : sessions) release(key);
    m_Sessions.clear();
    for (auto listener : m_Listeners) { listener->close(); delete listener; }
    m_Listeners.clear(); m_Urls.clear();
    const auto connections = m_Connections;
    for (auto connection : connections) { connection->socket->abort(); delete connection; }
    m_Connections.clear(); m_Port = 0;
    emit changed();
}
bool BrowserGateway::resetAccessCode() {
    // A new secret also disconnects authenticator apps that held the old one.
    const auto secret = newSecret();
    if (!saveSecret(secret)) { m_Error = tr("Cannot save a new browser access code."); emit changed(); return false; }
    m_Secret = secret; m_LastStep = -1;
    const auto keys = m_Sessions.keys();
    for (const auto& key : keys) release(key);
    m_Sessions.clear(); emit changed(); return true;
}
void BrowserGateway::hostStateChanged() {
    if (!m_Hooks.state || m_Hooks.state().value("sharing").toBool()) return;
    const auto keys = m_Sessions.keys();
    for (const auto& key : keys) release(key);
}
void BrowserGateway::accept(qintptr descriptor) {
    auto socket = new QSslSocket;
    if (!socket->setSocketDescriptor(descriptor)) { delete socket; return; }
    int sameAddress = 0;
    for (auto connection : m_Connections)
        if (connection->socket->peerAddress() == socket->peerAddress()) ++sameAddress;
    // A tailnet peer is accepted only on a tailnet listener; the same RFC 6598
    // range arriving on a LAN listener could be carrier NAT and stays refused.
    const bool tailnetPeer = sharedAddressSpace(socket->peerAddress()) && sharedAddressSpace(socket->localAddress());
    if (m_Connections.size() >= 32 || sameAddress >= 8 || (!localAddress(socket->peerAddress()) && !tailnetPeer)) {
        socket->abort(); delete socket; return;
    }
    auto connection = new Connection(this);
    connection->socket = socket; socket->setParent(connection);
    m_Connections.insert(connection);
    connect(connection, &QObject::destroyed, this, [this, connection] { m_Connections.remove(connection); });
    auto configuration = QSslConfiguration::defaultConfiguration();
    configuration.setLocalCertificateChain(m_Certificates);
    configuration.setPrivateKey(m_PrivateKey);
    configuration.setProtocol(QSsl::TlsV1_2OrLater);
    configuration.setPeerVerifyMode(QSslSocket::VerifyNone);
    socket->setSslConfiguration(configuration);
    socket->setReadBufferSize(MaxBody + MaxHeaders + 1);
    connect(socket, &QSslSocket::readyRead, connection, [this, connection] { receive(connection); });
    connect(socket, &QSslSocket::disconnected, connection, &QObject::deleteLater);
    connect(socket, qOverload<QAbstractSocket::SocketError>(&QSslSocket::errorOccurred), connection,
        [connection](QAbstractSocket::SocketError) { connection->deleteLater(); });
    QTimer::singleShot(10000, connection, [connection] {
        if (!connection->dispatched) { connection->socket->abort(); connection->deleteLater(); }
    });
    QTimer::singleShot(45000, connection, [this, connection] {
        if (!connection->replied) error(connection, 504, "timeout", tr("The browser operation timed out."));
    });
    socket->startServerEncryption();
}

void BrowserGateway::receive(Connection* connection) {
    if (connection->replied || connection->dispatched || !connection->socket->isEncrypted()) return;
    connection->buffer += connection->socket->readAll();
    if (connection->buffer.size() > MaxHeaders + MaxBody) {
        error(connection, 413, "too-large", "Request exceeded the browser limit."); return;
    }
    if (connection->contentLength < 0) {
        const int end = connection->buffer.indexOf("\r\n\r\n");
        if (end < 0) {
            if (connection->buffer.size() > MaxHeaders) error(connection, 431, "too-large", "HTTP headers are too large.");
            return;
        }
        if (end > MaxHeaders) { error(connection, 431, "too-large", "HTTP headers are too large."); return; }
        const auto lines = connection->buffer.left(end).split('\n');
        const auto request = lines.first().trimmed().split(' ');
        if (request.size() != 3 || request[2] != "HTTP/1.1" || (request[0] != "GET" && request[0] != "POST")) {
            error(connection, 400, "invalid-request", "Unsupported HTTP request."); return;
        }
        connection->method = request[0]; connection->path = request[1];
        for (int i = 1; i < lines.size(); ++i) {
            const auto line = lines[i].trimmed();
            const int colon = line.indexOf(':');
            if (colon <= 0 || lines[i].startsWith(' ') || lines[i].startsWith('\t')) {
                error(connection, 400, "invalid-request", "Invalid HTTP header."); return;
            }
            const auto name = line.left(colon).toLower(), value = line.mid(colon + 1).trimmed();
            bool validValue = true;
            for (char byte : value) if ((static_cast<unsigned char>(byte) < 32 && byte != '\t') || byte == 127) validValue = false;
            if (connection->headers.contains(name) || !validValue ||
                !QRegularExpression("^[!#$%&'*+.^_`|~0-9a-z-]+$").match(QString::fromLatin1(name)).hasMatch()) {
                error(connection, 400, "invalid-request", "Duplicate or invalid HTTP header."); return;
            }
            connection->headers.insert(name, value);
        }
        if (!permittedHost(connection->headers.value("host")) || connection->headers.contains("transfer-encoding")) {
            error(connection, 400, "invalid-request", "Invalid HTTP authority or transfer encoding."); return;
        }
        bool numeric = false;
        const auto length = connection->headers.value("content-length", "0");
        const auto parsed = length.toLongLong(&numeric);
        if (!numeric || parsed < 0 || parsed > MaxBody || !QRegularExpression("^[0-9]+$").match(QString::fromLatin1(length)).hasMatch()) {
            error(connection, 413, "too-large", "Invalid request length."); return;
        }
        connection->contentLength = int(parsed);
        connection->buffer.remove(0, end + 4);
    }
    if (connection->buffer.size() < connection->contentLength) return;
    if (connection->buffer.size() != connection->contentLength) {
        error(connection, 400, "invalid-request", "HTTP pipelining is not supported."); return;
    }
    connection->body = connection->buffer;
    connection->buffer.clear(); connection->dispatched = true;
    dispatch(connection);
}
bool BrowserGateway::permittedHost(const QByteArray& host) const {
    const QUrl url(QString::fromLatin1("https://" + host));
    return url.isValid() && url.userInfo().isEmpty() && url.path().isEmpty() && url.query().isEmpty() &&
        url.fragment().isEmpty() && url.port(443) == m_Port && m_Hosts.contains(url.host().toLower());
}
qint64 BrowserGateway::wallSeconds() const {
    return m_Options.clock ? m_Options.clock() : QDateTime::currentSecsSinceEpoch();
}
QString BrowserGateway::totp(const QByteArray& secret, qint64 step) {
    QByteArray counter(8, 0);
    for (int i = 7; i >= 0; --i) { counter[i] = char(step & 0xff); step >>= 8; }
    const auto digest = QMessageAuthenticationCode::hash(counter, secret, QCryptographicHash::Sha1);
    const int offset = digest.at(19) & 0x0f;
    const quint32 value = (quint32(quint8(digest.at(offset)) & 0x7f) << 24) | (quint32(quint8(digest.at(offset + 1))) << 16) |
        (quint32(quint8(digest.at(offset + 2))) << 8) | quint32(quint8(digest.at(offset + 3)));
    return QStringLiteral("%1").arg(value % 1000000, 6, 10, QLatin1Char('0'));
}
QString BrowserGateway::accessCode() const {
    return m_Secret.isEmpty() ? QString() : totp(m_Secret, wallSeconds() / CodeStepSeconds);
}
qint64 BrowserGateway::accessCodeExpiresAt() const {
    return (wallSeconds() / CodeStepSeconds + 1) * CodeStepSeconds * 1000LL;
}
QString BrowserGateway::authenticatorUri(const QString& account) const {
    if (m_Secret.isEmpty()) return {};
    const auto label = QString::fromLatin1(QUrl::toPercentEncoding("DeskPort:" + (account.isEmpty() ? QStringLiteral("computer") : account), ":"));
    return "otpauth://totp/" + label + "?secret=" + base32(m_Secret) +
        "&issuer=DeskPort&algorithm=SHA1&digits=6&period=" + QString::number(CodeStepSeconds);
}
qint64 BrowserGateway::codeLockedUntil() const {
    const auto left = m_LockedUntil - now();
    return left > 0 ? QDateTime::currentMSecsSinceEpoch() + left : 0;
}
void BrowserGateway::scheduleCodeTick() {
    // Refresh local displays when the code changes; never part of any response.
    const qint64 current = m_Options.clock ? wallSeconds() * 1000LL : QDateTime::currentMSecsSinceEpoch();
    const qint64 wait = accessCodeExpiresAt() - current + 50;
    m_CodeTick.start(int(std::clamp<qint64>(wait, 200, CodeStepSeconds * 1000LL + 50)));
}
// One step either side covers clock drift between this computer and an
// authenticator app. Each step is accepted once, and repeated wrong codes pause
// code sign-in so six digits cannot be guessed at the per-address rate.
bool BrowserGateway::acceptCode(const QByteArray& input) {
    QByteArray code; for (const char c : input) if (c != ' ' && c != '-') code += c;
    const qint64 step = wallSeconds() / CodeStepSeconds;
    if (QRegularExpression("^[0-9]{6}$").match(QString::fromLatin1(code)).hasMatch() && !m_Secret.isEmpty()) {
        for (qint64 candidate = step - 1; candidate <= step + 1; ++candidate) {
            if (candidate > m_LastStep && equal(code, totp(m_Secret, candidate).toLatin1())) {
                m_LastStep = candidate; m_CodeFailures.clear(); return true;
            }
        }
    }
    const qint64 current = now();
    m_CodeFailures.append(current);
    while (!m_CodeFailures.isEmpty() && current - m_CodeFailures.first() > CodeFailureWindowMs) m_CodeFailures.removeFirst();
    if (m_CodeFailures.size() >= qMax(1, m_Options.codeFailureLimit)) {
        m_LockedUntil = current + CodeLockMs; m_CodeFailures.clear(); emit changed();
    }
    return false;
}
bool BrowserGateway::loginAllowed(const QString& address) {
    const qint64 current = now();
    const auto take = [current](Rate& rate, double maximum, double refill) {
        if (rate.updated) rate.credits = qMin(maximum, rate.credits + qMax(qint64(0), current - rate.updated) * refill / 1000.0);
        rate.updated = current;
        if (rate.credits < 1.0) return false;
        rate.credits -= 1.0; return true;
    };
    if (!m_Rates.contains(address) && m_Rates.size() >= 128) return false;
    const bool local = take(m_Rates[address], 5, 1.0 / 12.0);
    const bool global = take(m_GlobalRate, 12, 1.0 / 5.0);
    return local && global;
}
QString BrowserGateway::authenticated(Connection* connection) const {
    const auto key = cookieKey(connection->headers.value("cookie"));
    const auto found = m_Sessions.constFind(key);
    if (found == m_Sessions.constEnd() || now() - found->lastSeen > m_Options.sessionIdleSeconds * 1000LL ||
        now() - found->created > 8 * 60 * 60 * 1000LL ||
        !equal(found->csrf.toLatin1(), connection->headers.value("x-deskport-session"))) return {};
    if (!found->pairingId.isEmpty() && (!m_Pairings.contains(found->pairingId) ||
        m_Pairings.value(found->pairingId).expiresAt <= QDateTime::currentMSecsSinceEpoch())) return {};
    return key;
}
void BrowserGateway::dispatch(Connection* connection) {
    const auto origin = connection->headers.value("origin");
    if ((!origin.isEmpty() && origin != "https://" + connection->headers.value("host")) ||
        (connection->method == "POST" && origin.isEmpty()) ||
        connection->headers.value("sec-fetch-site") == "cross-site") {
        error(connection, 403, "origin", "Use the browser page on this HTTPS origin."); return;
    }
    if (connection->method == "GET" && !connection->path.startsWith("/api/")) {
        QString resource;
        QByteArray contentType;
        if (connection->path == "/" || connection->path == "/index.html") { resource = ":/browser/index.html"; contentType = "text/html; charset=utf-8"; }
        else if (connection->path == "/app.js") { resource = ":/browser/app.js"; contentType = "application/javascript; charset=utf-8"; }
        else if (connection->path == "/i18n.js") { resource = ":/browser/i18n.js"; contentType = "application/javascript; charset=utf-8"; }
        else if (connection->path == "/i18n-data.js") { resource = ":/browser/i18n-data.js"; contentType = "application/javascript; charset=utf-8"; }
        else if (connection->path == "/style.css") { resource = ":/browser/style.css"; contentType = "text/css; charset=utf-8"; }
        else if (connection->path == "/icon.svg") { resource = ":/browser/icon.svg"; contentType = "image/svg+xml"; }
        else { error(connection, 404, "not-found", "Resource not found."); return; }
        QFile file(resource);
        if (!file.open(QIODevice::ReadOnly)) { error(connection, 503, "unavailable", "The browser client is not included in this build."); return; }
        auto content = file.readAll();
        if (resource == ":/browser/index.html") content.replace("@DESKPORT_VERSION@", QCoreApplication::applicationVersion().toHtmlEscaped().toUtf8());
        respond(connection, 200, content, contentType); return;
    }
    QJsonObject body;
    if (connection->method == "POST") {
        if (connection->headers.value("content-type").split(';').first().trimmed() != "application/json") {
            error(connection, 415, "invalid-request", "Expected application/json."); return;
        }
        QJsonParseError parse;
        const auto json = QJsonDocument::fromJson(connection->body, &parse);
        if (parse.error != QJsonParseError::NoError || !json.isObject()) {
            error(connection, 400, "invalid-request", "Expected a JSON object."); return;
        }
        body = json.object();
    } else if (!connection->body.isEmpty()) { error(connection, 400, "invalid-request", "Unexpected request body."); return; }
    if (connection->path == "/api/login/tailnet" && connection->method == "POST") {
        if (body.contains("probe") && !body.value("probe").isBool()) {
            error(connection, 400, "invalid-request", "Expected a probe flag."); return;
        }
        // Every page load probes once; a LAN page must not spend code sign-in attempts.
        if (!tailnetEligible(connection)) {
            error(connection, 403, "tailnet-unavailable", "Enter the computer's access code on this network."); return;
        }
        if (!loginAllowed(connection->socket->peerAddress().toString())) {
            error(connection, 429, "rate-limited", "Too many attempts. Wait before trying again."); return;
        }
        const bool probe = body.value("probe").toBool();
        const QPointer<Connection> guarded(connection);
        const QPointer<BrowserGateway> self(this);
        tailnetOwner(connection->socket->peerAddress(), [self, guarded, probe](bool owner) {
            if (!self || !guarded || guarded->replied) return;
            if (!owner) {
                self->error(guarded, 401, "tailnet-denied", "This Tailscale device does not belong to this computer's user."); return;
            }
            // A probe only lets the page hide the code field; it never creates a session.
            if (probe) { self->respond(guarded, 200, "{\"ok\":true,\"tailnet\":true}"); return; }
            if (!self->sessionAvailable(guarded)) return;
            self->createSession(guarded, {});
        });
        return;
    }
    if (connection->path == "/api/login" && connection->method == "POST") {
        if (!loginAllowed(connection->socket->peerAddress().toString())) {
            error(connection, 429, "rate-limited", "Too many attempts. Wait before trying again."); return;
        }
        if (m_LockedUntil > now()) {
            error(connection, 429, "code-locked", "Too many wrong codes. Code sign-in is paused for 15 minutes."); return;
        }
        if (!acceptCode(body.value("code").toString().trimmed().toLatin1())) {
            error(connection, 401, "unauthorized", "The access code is incorrect or has expired."); return;
        }
        if ((body.contains("remember") && !body.value("remember").isBool()) ||
            (body.contains("deviceName") && (!body.value("deviceName").isString() ||
                !validDeviceName(body.value("deviceName").toString().trimmed())))) {
            error(connection, 400, "invalid-request", "Expected a remember flag and a device name of 1 to 64 characters."); return;
        }
        if (!sessionAvailable(connection)) return;
        if (!body.value("remember").toBool()) { createSession(connection, {}); return; }
        auto updated = m_Pairings;
        const auto current = QDateTime::currentMSecsSinceEpoch();
        for (auto it = updated.begin(); it != updated.end();) {
            if (it->expiresAt <= current) it = updated.erase(it); else ++it;
        }
        QString id = pairingFor(connection);
        QByteArray token = cookieToken(connection->headers.value("cookie"), PairingCookieName);
        if (id.isEmpty()) {
            if (updated.size() >= MaxPairings) { error(connection, 409, "pairing-limit", "Remove an existing browser pairing before adding another."); return; }
            do { id = tokenKey(randomToken()).left(32); } while (updated.contains(id));
            token = randomToken();
            updated.insert(id, Pairing{id, "Browser", tokenKey(token), current, current, current + PairingLifetimeSeconds * 1000});
        }
        auto& entry = updated[id];
        if (body.contains("deviceName")) entry.name = body.value("deviceName").toString().trimmed();
        // Entering a code (re)starts the seven days; using the browser does not extend them.
        entry.createdAt = entry.lastSeenAt = qMax(current, entry.lastSeenAt);
        entry.expiresAt = entry.createdAt + PairingLifetimeSeconds * 1000;
        if (!savePairings(updated)) { error(connection, 503, "storage-unavailable", "The browser pairing could not be saved."); return; }
        m_Pairings = updated;
        createSession(connection, id, {cookie(PairingCookieName, token, (entry.expiresAt - current) / 1000)});
        emit changed(); return;
    }
    if (connection->path == "/api/resume" && connection->method == "POST") {
        const auto id = pairingFor(connection);
        if (id.isEmpty()) {
            // Cookies are shared by tabs. A delayed failed resume must not
            // delete a credential that another tab has just enrolled. Invalid
            // credentials cannot authorize; login replaces them and forget
            // explicitly removes them after an authenticated request.
            error(connection, 401, "unpaired", "Pair this browser with the computer's access code.");
            return;
        }
        if (!sessionAvailable(connection)) return;
        auto updated = m_Pairings;
        auto& entry = updated[id];
        const auto current = QDateTime::currentMSecsSinceEpoch();
        entry.lastSeenAt = qMin(entry.expiresAt, qMax(current, entry.lastSeenAt));
        if (!savePairings(updated)) { error(connection, 503, "storage-unavailable", "The browser pairing could not be refreshed."); return; }
        m_Pairings = updated;
        createSession(connection, id, {cookie(PairingCookieName,
            cookieToken(connection->headers.value("cookie"), PairingCookieName), qMax<qint64>(1, (entry.expiresAt - current) / 1000))}, true);
        emit changed(); return;
    }
    const auto key = authenticated(connection);
    if (key.isEmpty()) { error(connection, 401, "unauthorized", "Enter the computer's access code to start a browser session."); return; }
    m_Sessions[key].lastSeen = now();
    if (connection->path == "/api/forget" && connection->method == "POST") {
        const auto id = m_Sessions.value(key).pairingId;
        if (!id.isEmpty() && !revokeBrowser(id)) {
            error(connection, 503, "storage-unavailable", "The browser pairing could not be removed."); return;
        }
        release(key); m_Sessions.remove(key);
        respond(connection, 200, "{\"ok\":true}", "application/json",
            {cookie(CookieName, {}, 0), cookie(PairingCookieName, {}, 0)}); return;
    }
    if (connection->path == "/api/logout" && connection->method == "POST") {
        release(key); m_Sessions.remove(key);
        respond(connection, 200, "{\"ok\":true}", "application/json", {cookie(CookieName, {}, 0)}); return;
    }
    if (connection->path == "/api/status" && connection->method == "GET") {
        auto state = m_Hooks.state ? m_Hooks.state() : QJsonObject();
        // Explicit allowlist prevents a host adapter from accidentally exposing
        // internal credentials or configuration through this public response.
        QJsonObject result{{"ok", true}, {"authenticated", true}};
        for (const auto& field : {"sharing", "busy", "hostName", "mediaAvailable", "capabilities"})
            if (state.contains(field)) result[field] = state[field];
        respond(connection, 200, QJsonDocument(result).toJson(QJsonDocument::Compact)); return;
    }
    const QByteArray prefix("/api/session/");
    if (!connection->path.startsWith(prefix)) { error(connection, 404, "not-found", "API not found."); return; }
    const auto action = QString::fromLatin1(connection->path.mid(prefix.size()));
    if (!QStringList{"start", "answer", "status", "heartbeat", "input", "resize", "stop"}.contains(action) ||
        (connection->method != "POST" && action != "status")) {
        error(connection, 404, "not-found", "Session API not found."); return;
    }
    if (body.contains("id") || body.contains("clientId") || body.contains("action")) {
        error(connection, 400, "invalid-request", "Session identity is assigned by the host."); return;
    }
    const auto state = m_Hooks.state ? m_Hooks.state() : QJsonObject();
    if (action != "stop" && !state.value("sharing").toBool()) {
        release(key); error(connection, 409, "sharing-disabled", "Desktop sharing is disabled on this computer."); return;
    }
    forward(connection, key, action, body);
}

void BrowserGateway::forward(Connection* connection, const QString& key, const QString& action, QJsonObject body) {
    if (!m_Hooks.request) { error(connection, 503, "unsupported", "Browser media is unavailable."); return; }
    auto& session = m_Sessions[key];
    if (action == "start" && (session.pending || session.media)) {
        error(connection, 409, "busy", "This browser already owns a desktop session."); return;
    }
    if (action != "start" && action != "stop" && !session.media) {
        error(connection, 409, "no-session", "Start a desktop session first."); return;
    }
    if (action == "start") { session.pending = true; session.lastHeartbeat = now(); ++session.generation; }
    if (action == "heartbeat") session.lastHeartbeat = now();
    if (action == "stop") { session.media = session.pending = false; ++session.generation; }
    body["action"] = action; body["id"] = session.id;
    const auto generation = session.generation;
    const auto id = session.id;
    const QPointer<Connection> guarded(connection);
    const QPointer<BrowserGateway> self(this);
    // The gateway is the callback context so a closed HTTP request still rolls
    // back a media start. No detached request may leave a live input session.
    m_Hooks.request(body, this, [self, guarded, key, action, generation, id](QJsonObject result) {
        if (!self) return;
        const bool success = result.contains("ok") ? result.value("ok").toBool() : result.value("status").toBool();
        auto found = self->m_Sessions.find(key);
        if (found == self->m_Sessions.end() || found->generation != generation) {
            if (action == "start" && success && self->m_Hooks.request)
                self->m_Hooks.request(QJsonObject{{"action", "stop"}, {"id", id}}, self, [](QJsonObject) {});
            if (guarded && !guarded->replied) self->error(guarded, 409, "cancelled", "This browser session has ended.");
            return;
        }
        if (action == "start") {
            found->pending = false; found->media = success; found->lastHeartbeat = now();
            if (!guarded || guarded->replied) { self->release(key); return; }
        } else if (action == "stop") { found->media = false; found->pending = false; }
        if (!guarded || guarded->replied) return;
        result["ok"] = success;
        result.remove("id"); result.remove("lease");
        if (!success && result.value("code").toString().isEmpty()) result["code"] = "unavailable";
        const auto code = result.value("code").toString();
        const int status = success ? 200 : (code == "busy" || code == "sharing-disabled" || code == "no-session") ? 409
            : (code == "invalid-session" || code == "policy-unavailable") ? 400 : 503;
        self->respond(guarded, status, QJsonDocument(result).toJson(QJsonDocument::Compact));
    });
}
void BrowserGateway::release(const QString& key) {
    auto found = m_Sessions.find(key);
    if (found == m_Sessions.end() || (!found->media && !found->pending)) return;
    const auto id = found->id;
    found->media = found->pending = false;
    ++found->generation;
    if (m_Hooks.request) m_Hooks.request(QJsonObject{{"action", "stop"}, {"id", id}}, this, [](QJsonObject) {});
}
void BrowserGateway::cleanup() {
    const qint64 current = now();
    if (++m_AddressTicks >= AddressRefreshTicks) { m_AddressTicks = 0; refreshAddresses(); }
    const auto keys = m_Sessions.keys();
    for (const auto& key : keys) {
        const auto session = m_Sessions.value(key);
        const bool idle = current - session.lastSeen > m_Options.sessionIdleSeconds * 1000LL;
        const bool expired = current - session.created > 8 * 60 * 60 * 1000LL;
        const bool unpaired = !session.pairingId.isEmpty() && (!m_Pairings.contains(session.pairingId) ||
            m_Pairings.value(session.pairingId).expiresAt <= QDateTime::currentMSecsSinceEpoch());
        if (idle || expired || unpaired || ((session.media || session.pending) && current - session.lastHeartbeat > m_Options.mediaIdleSeconds * 1000LL)) {
            release(key); m_Sessions.remove(key);
        }
    }
    for (auto it = m_Rates.begin(); it != m_Rates.end();) {
        if (current - it->updated > 60 * 60 * 1000LL) it = m_Rates.erase(it); else ++it;
    }
}
void BrowserGateway::error(Connection* connection, int status, const QString& code, const QString& message, const QList<QByteArray>& cookies) {
    respond(connection, status, QJsonDocument(QJsonObject{{"ok", false}, {"code", code}, {"message", message}}).toJson(QJsonDocument::Compact), "application/json", cookies);
}
void BrowserGateway::respond(Connection* connection, int status, const QByteArray& body,
                             const QByteArray& contentType, const QList<QByteArray>& cookies) {
    if (connection->replied) return;
    connection->replied = true;
    QByteArray response = "HTTP/1.1 " + QByteArray::number(status) + " Response\r\nConnection: close\r\n";
    response += "Content-Type: " + contentType + "\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n";
    response += "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\n";
    response += "X-Frame-Options: DENY\r\nPermissions-Policy: camera=(), microphone=(), geolocation=()\r\n";
    response += "Content-Security-Policy: default-src 'self'; script-src 'self'; style-src 'self'; connect-src 'self'; "
                "img-src 'self' data:; media-src 'self' blob:; object-src 'none'; base-uri 'none'; frame-ancestors 'none'; form-action 'self'\r\n";
    for (const auto& value : cookies) response += "Set-Cookie: " + value + "\r\n";
    if (status == 429) response += "Retry-After: 15\r\n";
    connection->socket->write(response + "\r\n" + body);
    connection->socket->disconnectFromHost();
}
