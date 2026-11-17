#pragma once

#include "profiles.h"
#include "protocol.h"
#include <QDateTime>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

namespace Deye {

// Collects a fixed, read-only diagnostic report without discovering other hosts
// or requiring a supported decoder for the initial identity register block.
class DiagnosticProbe : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY changed)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(int completed READ completed NOTIFY changed)
    Q_PROPERTY(int total READ total NOTIFY changed)
    Q_PROPERTY(QString exactModel READ exactModel NOTIFY changed)
    Q_PROPERTY(QString notes READ notes NOTIFY changed)
public:
    explicit DiagnosticProbe(QObject *parent = nullptr);
    ~DiagnosticProbe() override;
    bool running() const { return m_running; }
    bool ready() const { return m_ready; }
    QString status() const { return m_status; }
    int completed() const { return m_completed; }
    int total() const { return m_total; }
    QString exactModel() const { return m_exactModel; }
    QString notes() const { return m_notes; }
    void start(QVariantMap settings, QString exactModel, QString notes, bool extended = false);
    void captureFailure(QVariantMap settings, Registers registers, QVariantList wire, QString error,
                        QString exactModel = {});
    Q_INVOKABLE void setDetails(QString exactModel, QString notes);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE bool save(QUrl file, bool includeIdentifiers = false);
    QVariantMap report(bool includeIdentifiers = false) const;
signals:
    void changed();
    void finished();
    void exportFinished(QString path);
    void exportFailed(QString message);

private:
    struct ReadBlock {
        int start = 0, count = 0;
        QString purpose;
    };
    void requestStatus();
    void processStatus(QNetworkReply *reply, int generation);
    void beginIdentity();
    void planReads();
    void readNext();
    void received(Registers registers);
    void readFailed(QString error);
    void recordBlock(const Registers &registers, const QString &error = {});
    void addError(const QString &stage, const QString &message);
    void finish(QString outcome, QString status);
    void stopIo();
    bool current(int generation) const { return m_running && generation == m_generation; }

    Profiles m_profiles;
    const Profile *m_profile = nullptr;
    Transport m_transport;
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_httpReply;
    QTimer m_deadline, m_httpDeadline, m_step;
    QVariantMap m_settings, m_http, m_logger, m_detection, m_decoded;
    QVariantList m_blocks, m_wire, m_errors;
    Registers m_registers, m_initialIdentity;
    QList<ReadBlock> m_plan;
    QStringList m_identifiers, m_secrets;
    QString m_status, m_exactModel, m_notes, m_host, m_expectedInverter, m_httpInverter;
    QString m_outcome;
    QDateTime m_started, m_finished, m_readStarted;
    quint32 m_loggerSerial = 0;
    int m_port = 8899, m_statusPort = 80, m_generation = 0;
    int m_completed = 0, m_total = 0, m_next = 0;
    bool m_running = false, m_ready = false, m_extended = false;
    bool m_waitingRead = false, m_httpTimedOut = false, m_identityVerifiedAtEnd = false;
};

} // namespace Deye
