#pragma once
#include "chargeestimator.h"
#include "diagnosticprobe.h"
#include "discovery.h"
#include "profiles.h"
#include "protocol.h"
#include <QDateTime>
#include <QElapsedTimer>
#include <QSettings>

namespace Deye {
class Monitor : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap values READ values NOTIFY changed)
    Q_PROPERTY(QVariantMap states READ states NOTIFY changed)
    Q_PROPERTY(QVariantMap settings READ settings NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList history READ history NOTIFY historyChanged)
    Q_PROPERTY(QVariantList detailGroups READ detailGroups NOTIFY changed)
    Q_PROPERTY(QVariantList scanResults READ scanResults NOTIFY scanChanged)
    Q_PROPERTY(QVariantList profileOptions READ profileOptions CONSTANT)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString model READ model NOTIFY changed)
    Q_PROPERTY(QString serial READ serial NOTIFY changed)
    Q_PROPERTY(QString profile READ profile NOTIFY changed)
    Q_PROPERTY(QString host READ host NOTIFY settingsChanged)
    Q_PROPERTY(QString updated READ updated NOTIFY changed)
    Q_PROPERTY(QString chargeEta READ chargeEta NOTIFY changed)
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(bool paused READ paused NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanChanged)
    Q_PROPERTY(bool demo READ demo NOTIFY changed)
    Q_PROPERTY(QObject *probe READ probe CONSTANT)
    Q_PROPERTY(bool probeOffered READ probeOffered NOTIFY changed)
public:
    explicit Monitor(bool persist = true, QObject *parent = nullptr,
                     std::optional<DiscoveryTargets> discoveryTargets = std::nullopt);
    ~Monitor() override;
    QVariantMap values() const { return m_values; }
    QVariantMap states() const { return m_states; }
    QVariantMap settings() const { return m_settings; }
    QVariantList history() const { return m_history; }
    QVariantList detailGroups() const;
    QVariantList profileOptions() const;
    QVariantList scanResults() const { return m_scanResults; }
    QString status() const { return m_status; }
    QString model() const;
    QString serial() const { return m_device.serial; }
    QString profile() const { return m_device.profile; }
    QString host() const { return m_demo ? "Demo preview" : m_settings.value("ip").toString(); }
    QString updated() const { return m_updated.isValid() ? m_updated.toString("HH:mm:ss") : "—"; }
    QString chargeEta() const { return m_eta; }
    bool connected() const { return m_connected; }
    bool paused() const { return m_paused; }
    bool busy() const { return m_busy; }
    bool scanning() const { return m_scan.running(); }
    bool demo() const { return m_demo; }
    QObject *probe() { return &m_probe; }
    DiagnosticProbe *diagnosticProbe() { return &m_probe; }
    bool probeOffered() const { return m_probeOffered; }
    Q_INVOKABLE void applySettings(QVariantMap settings, bool connectNow = true);
    Q_INVOKABLE void scanSettings(QVariantMap settings);
    Q_INVOKABLE void cancelScan();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void togglePaused();
    Q_INVOKABLE void startProbe(QString exactModel, QString notes, bool extended = false);
    Q_INVOKABLE void cancelProbe();
    Q_INVOKABLE void dismissProbeOffer();
    void start();
    void setDemo();
    QVariantMap report() const;
signals:
    void changed();
    void settingsChanged();
    void historyChanged();
    void scanChanged();
    void scanFinished(QVariantList results, double elapsed, QString error);
    void snapshotReady(QVariantMap report);
    void connectionError(QString error, bool fatal);

private:
    void configure();
    void beginSnapshot();
    void received(Registers registers);
    void nextBlock();
    void completeSnapshot();
    void failed(QString error, bool fatal = false);
    void offerStartupProbe(const QString &error);
    void recover();
    void save();
    void estimateCharge();
    ScanOptions options(const QVariantMap &) const;
    static QVariantMap normalized(QVariantMap);
    Profiles m_profiles;
    const Profile *m_profile = nullptr;
    Device m_device;
    Transport m_transport;
    Discovery m_scan, m_discovery;
    DiagnosticProbe m_probe;
    QTimer m_poll, m_retry;
    QSettings m_store;
    QVariantMap m_settings, m_values, m_states;
    QVariantList m_history, m_scanResults, m_wire;
    Registers m_registers;
    QDateTime m_updated, m_lastHeartbeat;
    QElapsedTimer m_scanTime;
    ChargeEstimator m_charge;
    QString m_status = "Choose a logger in Settings", m_eta;
    int m_block = -1, m_generation = 0, m_backoff = 0, m_heartbeats = 0;
    bool m_persist = true, m_connected = false, m_paused = false, m_busy = false, m_demo = false;
    bool m_heartbeatThisSnapshot = false;
    bool m_probeRestorePending = false, m_probeWasPaused = false, m_resumeAfterProbe = false;
    QString m_statusBeforeProbe;
    bool m_probeOffered = false, m_probeOfferSuppressed = false;
    bool m_starting = true, m_tryingSavedHost = false;
    QString m_startupError;
};
} // namespace Deye
