#pragma once
#include "profiles.h"
#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <functional>

namespace Deye {
quint16 modbusCrc(const QByteArray &);
QByteArray readRequest(quint32 logger, quint8 sequence, int start, int count);
bool validateFrame(const QByteArray &, quint32 logger, QString &error);
bool decodeReply(const QByteArray &, int start, int count, Registers &, QString &error);

class Transport : public QObject {
    Q_OBJECT
public:
    explicit Transport(QObject *parent = nullptr);
    void configure(QString host, quint16 port, quint32 logger);
    void read(int start, int count);
    void stop();
    bool isConnected() const { return m_socket.state() == QAbstractSocket::ConnectedState; }
signals:
    void registersReady(Deye::Registers registers);
    void failed(QString message);
    void wire(QString direction, QByteArray frame);
    void heartbeat();

private:
    void receive();
    void send();
    void fail(QString error);
    QTcpSocket m_socket;
    QTimer m_deadline;
    QByteArray m_buffer;
    QString m_host;
    quint16 m_port = 8899;
    quint32 m_logger = 0;
    quint8 m_sequence = 0;
    int m_start = 0, m_count = 0;
    bool m_pending = false, m_sent = false;
};
} // namespace Deye
