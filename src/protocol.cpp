#include "protocol.h"
#include <QNetworkProxy>
#include <QRandomGenerator>
#include <QtEndian>

namespace Deye {
static quint8 byte(const QByteArray &a, int n) { return quint8(a.at(n)); }
static quint16 le16(const QByteArray &a, int n) { return qFromLittleEndian<quint16>(a.constData() + n); }
static quint32 le32(const QByteArray &a, int n) { return qFromLittleEndian<quint32>(a.constData() + n); }
static quint8 checksum(const QByteArray &data) {
    quint8 sum = 0;
    for (char c : data)
        sum += quint8(c);
    return sum;
}
quint16 modbusCrc(const QByteArray &data) {
    quint16 crc = 0xffff;
    for (char c : data) {
        crc ^= quint8(c);
        for (int n = 0; n < 8; ++n)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xa001 : 0);
    }
    return crc;
}
QByteArray readRequest(quint32 logger, quint8 sequence, int start, int count) {
    if (logger == 0 || count < 1 || count > 32 || start < 0 || start > 65536 - count)
        return {};
    QByteArray rtu;
    rtu.append(char(1));
    rtu.append(char(3));
    rtu.append(char(start >> 8));
    rtu.append(char(start));
    rtu.append(char(count >> 8));
    rtu.append(char(count));
    quint16 crc = modbusCrc(rtu);
    rtu.append(char(crc));
    rtu.append(char(crc >> 8));
    QByteArray payload(15, char(0));
    payload[0] = char(2);
    payload += rtu;
    QByteArray frame(11, char(0));
    frame[0] = char(0xa5);
    qToLittleEndian<quint16>(quint16(payload.size()), frame.data() + 1);
    qToLittleEndian<quint16>(0x4510, frame.data() + 3);
    frame[5] = char(sequence);
    qToLittleEndian<quint32>(logger, frame.data() + 7);
    frame += payload;
    frame.append(char(checksum(frame.mid(1))));
    frame.append(char(0x15));
    return frame;
}
bool validateFrame(const QByteArray &frame, quint32 logger, QString &error) {
    if (frame.size() < 13 || byte(frame, 0) != 0xa5 || byte(frame, frame.size() - 1) != 0x15)
        error = "Invalid Solarman frame markers";
    else if (le16(frame, 1) > 4096 || frame.size() != le16(frame, 1) + 13)
        error = "Invalid Solarman frame length";
    else if (checksum(frame.mid(1, frame.size() - 3)) != byte(frame, frame.size() - 2))
        error = "Solarman checksum mismatch";
    else if (le32(frame, 7) != logger)
        error = "Logger serial does not match the selected device";
    else
        return true;
    return false;
}
bool decodeReply(const QByteArray &frame, int start, int count, Registers &registers, QString &error) {
    if (frame.size() < 13 || frame.size() != le16(frame, 1) + 13 || count < 1 || count > 32 ||
        start < 0 || start > 65536 - count) {
        error = "Invalid response length or read count";
        return false;
    }
    QByteArray payload = frame.mid(11, le16(frame, 1));
    if (payload.size() < 19 || byte(payload, 0) != 2 || byte(payload, 1) != 1) {
        error = "Invalid Solarman response payload";
        return false;
    }
    QByteArray rtu = payload.mid(14);
    // Some Deye replies append a second, zero CRC word. Remove it only after
    // independently validating the actual RTU CRC immediately before it.
    int expectedSize = rtu.size() > 1 && byte(rtu, 1) == 0x83 ? 5 : count * 2 + 5;
    if (rtu.size() == expectedSize + 2 && rtu.endsWith(QByteArray(2, char(0))) &&
        modbusCrc(rtu.left(rtu.size() - 4)) == le16(rtu, rtu.size() - 4))
        rtu.chop(2);
    if (rtu.size() < 5 || modbusCrc(rtu.left(rtu.size() - 2)) != le16(rtu, rtu.size() - 2)) {
        error = "Modbus CRC mismatch";
        return false;
    }
    if (byte(rtu, 0) != 1) {
        error = "Unexpected Modbus address";
        return false;
    }
    if (byte(rtu, 1) == 0x83) {
        error = QString("Inverter rejected register %1 (exception %2)").arg(start).arg(byte(rtu, 2));
        return false;
    }
    if (byte(rtu, 1) != 3 || byte(rtu, 2) != count * 2 || rtu.size() != count * 2 + 5) {
        error = "Unexpected Modbus function or byte count";
        return false;
    }
    for (int i = 0; i < count; ++i)
        registers[start + i] = qFromBigEndian<quint16>(rtu.constData() + 3 + i * 2);
    return true;
}
Transport::Transport(QObject *parent) : QObject(parent) {
    m_socket.setProxy(QNetworkProxy::NoProxy);
    m_sequence = quint8(QRandomGenerator::global()->bounded(1, 255));
    m_deadline.setSingleShot(true);
    m_deadline.setInterval(2000);
    connect(&m_deadline, &QTimer::timeout, this, [this] { fail("Connection timed out"); });
    connect(&m_socket, &QTcpSocket::connected, this, &Transport::send);
    connect(&m_socket, &QTcpSocket::readyRead, this, &Transport::receive);
    connect(&m_socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (m_pending)
            fail(m_socket.errorString());
    });
    connect(&m_socket, &QTcpSocket::disconnected, this, [this] {
        if (m_pending)
            fail("Logger disconnected");
    });
}
void Transport::configure(QString host, quint16 port, quint32 logger) {
    stop();
    m_host = host;
    m_port = port;
    m_logger = logger;
}
void Transport::stop() {
    m_pending = false;
    m_sent = false;
    m_deadline.stop();
    m_socket.abort();
    m_buffer.clear();
}
void Transport::read(int start, int count) {
    if (m_pending) {
        emit failed("A register read is already active");
        return;
    }
    if (readRequest(m_logger, 0, start, count).isEmpty()) {
        emit failed("Invalid register read plan");
        return;
    }
    m_start = start;
    m_count = count;
    ++m_sequence;
    m_pending = true;
    m_sent = false;
    m_deadline.start();
    if (m_socket.state() == QAbstractSocket::ConnectedState)
        send();
    else
        m_socket.connectToHost(m_host, m_port);
}
void Transport::send() {
    if (!m_pending || m_sent)
        return;
    m_sent = true;
    auto frame = readRequest(m_logger, m_sequence, m_start, m_count);
    emit wire("tx", frame);
    m_socket.write(frame);
}
void Transport::fail(QString error) {
    stop();
    emit failed(error);
}
void Transport::receive() {
    m_buffer += m_socket.readAll();
    while (m_buffer.size() >= 11) {
        if (byte(m_buffer, 0) != 0xa5 || le16(m_buffer, 1) > 4096) {
            fail("Malformed Solarman stream");
            return;
        }
        int size = le16(m_buffer, 1) + 13;
        if (m_buffer.size() < size)
            return;
        QByteArray frame = m_buffer.left(size);
        m_buffer.remove(0, size);
        emit wire("rx", frame);
        QString error;
        if (!validateFrame(frame, m_logger, error)) {
            fail(error);
            return;
        }
        int control = le16(frame, 3);
        if (control == 0x4710)
            emit heartbeat();
        if (!m_pending || control != 0x1510 || byte(frame, 5) != m_sequence)
            continue;
        Registers registers;
        if (!decodeReply(frame, m_start, m_count, registers, error)) {
            fail(error);
            return;
        }
        m_pending = false;
        m_sent = false;
        m_deadline.stop();
        emit registersReady(registers);
    }
}
} // namespace Deye
