#pragma once
#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QSet>
#include <QTcpSocket>
#include <QTimer>
#include <QUdpSocket>
#include <QVariantMap>
#include <optional>

namespace Deye {
struct Logger {
    QString ip, inverterSerial, mac;
    quint32 serial = 0;
    QVariantMap map() const;
};
struct ScanOptions {
    QString knownIp, user = "admin", password = "admin", mac, inverterSerial;
    int port = 8899;
    int statusPort = 80;
    quint32 serial = 0;
};
// An injected inventory lets discovery run against a controlled network in tests.
// Production discovers the current interface addresses whenever a scan starts.
struct DiscoveryTargets {
    struct Broadcast {
        QHostAddress localAddress, broadcastAddress;
        bool operator==(const Broadcast &) const = default;
    };
    QList<Broadcast> broadcasts;
    QStringList hosts;
    quint16 versionPort = 6021;
};
class Discovery : public QObject {
    Q_OBJECT
public:
    enum class Stage { SavedHost, Broadcast, NetworkScan };
    Q_ENUM(Stage)
    explicit Discovery(QObject *parent = nullptr);
    explicit Discovery(std::optional<DiscoveryTargets> targets, QObject *parent = nullptr);
    ~Discovery() override { cancel(); }
    void start(ScanOptions options, bool singleHost = false);
    void cancel();
    bool running() const { return m_running; }
    Stage stage() const { return m_stage; }
    static QStringList localHosts(QString knownIp);
    static QList<DiscoveryTargets::Broadcast> localBroadcasts();
    static std::optional<Logger> parseStatus(QString ip, const QByteArray &body);
signals:
    void stageChanged(Deye::Discovery::Stage stage);
    void loggerFound(Deye::Logger logger);
    void finished(QList<Deye::Logger> loggers, QString error);

private:
    bool enterStage(Stage stage);
    void beginBroadcast();
    void beginNetworkScan();
    void receiveVersions(QUdpSocket *socket, int generation);
    void completeBroadcast();
    void closeBroadcastSockets();
    void abortRequests();
    void pump();
    void probe(QString host);
    void status(QString host);
    void completedProbe();
    void finish(QString error = {});
    ScanOptions m_options;
    QNetworkAccessManager m_network;
    QTimer m_deadline, m_broadcastWindow;
    std::optional<DiscoveryTargets> m_injectedTargets;
    DiscoveryTargets m_targets;
    QStringList m_hosts;
    QSet<QTcpSocket *> m_sockets;
    QSet<QUdpSocket *> m_udpSockets;
    QSet<QNetworkReply *> m_replies;
    QSet<QString> m_versionHosts;
    QList<Logger> m_results;
    int m_next = 0, m_active = 0, m_generation = 0;
    Stage m_stage = Stage::SavedHost;
    bool m_running = false, m_listening = false;
};
} // namespace Deye
