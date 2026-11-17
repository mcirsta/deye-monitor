#include "discovery.h"
#include <QHostAddress>
#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QNetworkProxy>
#include <QRegularExpression>
#include <optional>

namespace Deye {
namespace {
constexpr int broadcastWindowMs = 1500;
constexpr int broadcastDeadlineMs = 3250;
constexpr int networkDeadlineMs = 6000;
constexpr int maximumVersionCandidates = 128;
bool isLocalAddress(quint32 ip) {
    return (ip >> 24) == 10 || (ip >> 20) == 0xac1 || (ip >> 16) == 0xc0a8 || (ip >> 16) == 0xa9fe;
}
bool eligibleInterface(const QNetworkInterface &iface) {
    const auto flags = iface.flags();
    return flags.testFlag(QNetworkInterface::IsUp) && !flags.testFlag(QNetworkInterface::IsLoopBack) &&
           !flags.testFlag(QNetworkInterface::IsPointToPoint);
}
} // namespace
QVariantMap Logger::map() const {
    return {
        {"ip", ip}, {"serial", QString::number(serial)}, {"inverterSerial", inverterSerial}, {"mac", mac}};
}
Discovery::Discovery(QObject *parent) : QObject(parent) {
    m_network.setProxy(QNetworkProxy::NoProxy);
    m_deadline.setSingleShot(true);
    m_broadcastWindow.setSingleShot(true);
    m_broadcastWindow.setTimerType(Qt::PreciseTimer);
    connect(&m_broadcastWindow, &QTimer::timeout, this, [this] {
        if (!m_running || m_stage != Stage::Broadcast)
            return;
        closeBroadcastSockets();
        completeBroadcast();
    });
    connect(&m_deadline, &QTimer::timeout, this, [this] {
        if (!m_running)
            return;
        if (m_stage == Stage::Broadcast) {
            if (m_results.isEmpty())
                beginNetworkScan();
            else
                finish();
        } else
            finish(m_results.isEmpty() ? "No matching logger found before scan timed out" : "");
    });
}
Discovery::Discovery(std::optional<DiscoveryTargets> targets, QObject *parent) : Discovery(parent) {
    m_injectedTargets = std::move(targets);
}
QStringList Discovery::localHosts(QString knownIp) {
    QStringList result;
    QSet<quint32> seen;
    auto add = [&](quint32 ip) {
        if (!seen.contains(ip)) {
            seen.insert(ip);
            result.append(QHostAddress(ip).toString());
        }
    };
    bool ok = false;
    auto known = QHostAddress(knownIp).toIPv4Address(&ok);
    if (ok)
        add(known);
    for (auto iface : QNetworkInterface::allInterfaces()) {
        if (!eligibleInterface(iface))
            continue;
        for (auto addr : iface.addressEntries()) {
            auto ip = addr.ip().toIPv4Address(&ok);
            if (!ok)
                continue;
            if (!isLocalAddress(ip))
                continue;
            int prefix = addr.prefixLength();
            if (prefix < 0 || prefix > 32)
                continue;
            if (prefix < 22)
                prefix = 24;
            quint32 mask = prefix == 32 ? 0xffffffffu : (0xffffffffu << (32 - prefix));
            quint32 first = ip & mask, last = first | ~mask;
            if (prefix < 31) {
                ++first;
                --last;
            }
            for (quint64 host = first; host <= last; ++host)
                add(quint32(host));
        }
    }
    return result;
}
QList<DiscoveryTargets::Broadcast> Discovery::localBroadcasts() {
    QList<DiscoveryTargets::Broadcast> result;
    QSet<QString> seen;
    for (const auto &iface : QNetworkInterface::allInterfaces()) {
        if (!eligibleInterface(iface) || !iface.flags().testFlag(QNetworkInterface::CanBroadcast))
            continue;
        for (const auto &entry : iface.addressEntries()) {
            bool valid = false;
            const auto ip = entry.ip().toIPv4Address(&valid);
            if (!valid || !isLocalAddress(ip) || entry.prefixLength() < 1 || entry.prefixLength() > 30 ||
                entry.broadcast().protocol() != QAbstractSocket::IPv4Protocol || entry.broadcast().isNull())
                continue;
            const QString pair = entry.ip().toString() + "/" + entry.broadcast().toString();
            if (!seen.contains(pair)) {
                seen.insert(pair);
                result.append({entry.ip(), entry.broadcast()});
            }
        }
    }
    return result;
}
std::optional<Logger> Discovery::parseStatus(QString ip, const QByteArray &body) {
    if (body.size() > 1024 * 1024)
        return {};
    QString page = QString::fromUtf8(body);
    auto field = [&](QString key) {
        return QRegularExpression("\\b" + key + "\\s*=\\s*\"([^\"]*)\"").match(page).captured(1).trimmed();
    };
    bool ok = false;
    quint64 serial = field("cover_mid").toULongLong(&ok);
    if (!ok || serial == 0 || serial > 0xffffffffu)
        return {};
    return Logger{ip, field("webdata_sn"), field("cover_sta_mac"), quint32(serial)};
}
void Discovery::cancel() {
    ++m_generation;
    m_running = false;
    m_deadline.stop();
    closeBroadcastSockets();
    abortRequests();
    m_active = 0;
}
void Discovery::closeBroadcastSockets() {
    m_broadcastWindow.stop();
    m_listening = false;
    const auto sockets = m_udpSockets;
    m_udpSockets.clear();
    for (auto socket : sockets) {
        socket->disconnect(this);
        socket->close();
        socket->deleteLater();
    }
}
void Discovery::abortRequests() {
    const auto sockets = m_sockets;
    m_sockets.clear();
    for (auto socket : sockets) {
        for (auto timer : socket->findChildren<QTimer *>()) {
            timer->stop();
            timer->disconnect(this);
        }
        socket->disconnect(this);
        socket->abort();
        socket->deleteLater();
    }
    const auto replies = m_replies;
    m_replies.clear();
    for (auto reply : replies) {
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
}
void Discovery::start(ScanOptions options, bool singleHost) {
    cancel();
    m_options = options;
    m_results.clear();
    m_next = 0;
    m_running = true;
    m_versionHosts.clear();
    if (singleHost) {
        if (!enterStage(Stage::SavedHost))
            return;
        if (options.knownIp.isEmpty()) {
            finish("No logger address available");
            return;
        }
        m_deadline.start(networkDeadlineMs);
        m_active = 1;
        status(options.knownIp);
    } else {
        m_targets = m_injectedTargets.value_or(DiscoveryTargets{});
        if (!m_injectedTargets)
            m_targets.broadcasts = localBroadcasts();
        beginBroadcast();
    }
}
bool Discovery::enterStage(Stage stage) {
    const int generation = m_generation;
    m_stage = stage;
    emit stageChanged(stage);
    return m_running && generation == m_generation;
}
void Discovery::beginBroadcast() {
    if (!enterStage(Stage::Broadcast))
        return;
    const int generation = m_generation;
    QSet<QString> sentPairs;
    bool sent = false;
    m_listening = true;
    for (const auto &target : m_targets.broadcasts) {
        if (target.localAddress.protocol() != QAbstractSocket::IPv4Protocol ||
            target.broadcastAddress.protocol() != QAbstractSocket::IPv4Protocol || m_targets.versionPort == 0)
            continue;
        const QString pair = target.localAddress.toString() + "/" + target.broadcastAddress.toString();
        if (sentPairs.contains(pair))
            continue;
        sentPairs.insert(pair);
        auto socket = new QUdpSocket(this);
        socket->setProxy(QNetworkProxy::NoProxy);
        if (!socket->bind(target.localAddress, 0)) {
            socket->deleteLater();
            continue;
        }
        m_udpSockets.insert(socket);
        connect(socket, &QUdpSocket::readyRead, this,
                [this, socket, generation] { receiveVersions(socket, generation); });
        sent |= socket->writeDatagram(QByteArrayLiteral("AT+YZVER"), target.broadcastAddress,
                                      m_targets.versionPort) == 8;
    }
    if (!sent) {
        beginNetworkScan();
        return;
    }
    m_deadline.start(broadcastDeadlineMs);
    m_broadcastWindow.start(broadcastWindowMs);
}
void Discovery::receiveVersions(QUdpSocket *socket, int generation) {
    static const QRegularExpression versionReply(
        QStringLiteral("\\A\\+ok=[A-Za-z0-9][A-Za-z0-9._-]{0,95}(?:\\r\\n)?\\z"));
    while (m_running && generation == m_generation && m_listening && socket->hasPendingDatagrams()) {
        const qint64 size = socket->pendingDatagramSize();
        const auto datagram = socket->receiveDatagram(129);
        const auto source = datagram.senderAddress();
        if (size < 5 || size > 128 || datagram.senderPort() != m_targets.versionPort ||
            source.protocol() != QAbstractSocket::IPv4Protocol || source.isNull() || source.isMulticast() ||
            source == QHostAddress::Broadcast ||
            !versionReply.match(QString::fromLatin1(datagram.data())).hasMatch())
            continue;
        const QString host = source.toString();
        if (m_versionHosts.contains(host) || m_versionHosts.size() >= maximumVersionCandidates)
            continue;
        m_versionHosts.insert(host);
        ++m_active;
        status(host);
    }
}
void Discovery::completeBroadcast() {
    if (!m_running || m_stage != Stage::Broadcast || m_listening || m_active != 0)
        return;
    if (m_results.isEmpty())
        beginNetworkScan();
    else
        finish();
}
void Discovery::beginNetworkScan() {
    if (!m_running)
        return;
    ++m_generation;
    closeBroadcastSockets();
    abortRequests();
    m_active = 0;
    m_hosts = m_injectedTargets ? m_targets.hosts : localHosts(m_options.knownIp);
    m_hosts.removeDuplicates();
    m_next = 0;
    if (!enterStage(Stage::NetworkScan))
        return;
    if (m_hosts.isEmpty()) {
        finish("No local IPv4 network available");
        return;
    }
    m_deadline.start(networkDeadlineMs);
    pump();
}
void Discovery::pump() {
    if (!m_running || m_stage != Stage::NetworkScan)
        return;
    while (m_active < 128 && m_next < m_hosts.size()) {
        ++m_active;
        probe(m_hosts[m_next++]);
    }
    if (m_active == 0 && m_next >= m_hosts.size())
        finish();
}
void Discovery::probe(QString host) {
    const int generation = m_generation;
    auto socket = new QTcpSocket(this);
    m_sockets.insert(socket);
    auto timer = new QTimer(socket);
    timer->setSingleShot(true);
    timer->start(650);
    auto dispose = [this, socket, timer] {
        if (!m_sockets.remove(socket))
            return false;
        timer->stop();
        timer->disconnect(this);
        socket->disconnect(this);
        socket->abort();
        socket->deleteLater();
        return true;
    };
    connect(timer, &QTimer::timeout, this, [this, generation, dispose] {
        if (generation != m_generation)
            return;
        if (dispose())
            completedProbe();
    });
    connect(socket, &QTcpSocket::errorOccurred, this,
            [this, generation, dispose](QAbstractSocket::SocketError) {
                if (generation != m_generation)
                    return;
                if (dispose())
                    completedProbe();
            });
    connect(socket, &QTcpSocket::connected, this, [this, generation, host, dispose] {
        if (generation != m_generation)
            return;
        if (dispose())
            status(host);
    });
    socket->setProxy(QNetworkProxy::NoProxy);
    socket->connectToHost(host, quint16(m_options.port));
}
void Discovery::status(QString host) {
    const int generation = m_generation;
    QUrl url;
    url.setScheme("http");
    url.setHost(host);
    url.setPort(m_options.statusPort);
    url.setPath("/status.html");
    QNetworkRequest request(url);
    request.setTransferTimeout(1500);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    QString user = m_options.user.isEmpty() ? "admin" : m_options.user;
    QString password = m_options.password.isEmpty() ? "admin" : m_options.password;
    request.setRawHeader("Authorization", "Basic " + (user + ":" + password).toUtf8().toBase64());
    auto reply = m_network.get(request);
    m_replies.insert(reply);
    connect(reply, &QNetworkReply::readyRead, this, [reply] {
        if (reply->bytesAvailable() > 1024 * 1024)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, generation, host, reply] {
        if (generation != m_generation || !m_running) {
            reply->deleteLater();
            return;
        }
        m_replies.remove(reply);
        auto logger = reply->error() == QNetworkReply::NoError &&
                              reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200
                          ? parseStatus(host, reply->readAll())
                          : std::nullopt;
        reply->deleteLater();
        if (logger && (m_options.serial == 0 || logger->serial == m_options.serial) &&
            (m_options.mac.isEmpty() || m_options.mac.compare(logger->mac, Qt::CaseInsensitive) == 0) &&
            (m_options.inverterSerial.isEmpty() || logger->inverterSerial.isEmpty() ||
             m_options.inverterSerial == logger->inverterSerial)) {
            bool duplicate = false;
            for (const auto &old : m_results)
                duplicate |= old.serial == logger->serial;
            if (!duplicate) {
                m_results.append(*logger);
                emit loggerFound(*logger);
                if (!m_running || generation != m_generation)
                    return;
            }
            if (m_options.serial != 0 || !m_options.mac.isEmpty() || !m_options.inverterSerial.isEmpty()) {
                finish();
                return;
            }
        }
        completedProbe();
    });
}
void Discovery::completedProbe() {
    --m_active;
    if (m_stage == Stage::Broadcast)
        completeBroadcast();
    else if (m_stage == Stage::SavedHost) {
        if (m_active == 0)
            finish();
    } else
        pump();
}
void Discovery::finish(QString error) {
    if (!m_running)
        return;
    if (m_results.isEmpty() && error.isEmpty())
        error = "No matching logger found. Check the network and logger web credentials.";
    auto results = m_results;
    cancel();
    emit finished(results, error);
}
} // namespace Deye
