#include "diagnosticprobe.h"
#include <QCoreApplication>
#include <QFile>
#include <QHostAddress>
#include <QJsonDocument>
#include <QNetworkProxy>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QtEndian>
#include <algorithm>
#include <limits>

namespace Deye {
namespace {
constexpr int maximumRequests = 24;
constexpr int spacingMs = 150;
constexpr int maximumHttpBytes = 1024 * 1024;
QString timestamp(const QDateTime &value) { return value.toUTC().toString(Qt::ISODateWithMs); }
QVariantMap rawMap(const Registers &registers) {
    QVariantMap result;
    for (auto it = registers.cbegin(); it != registers.cend(); ++it)
        result[QString::number(it.key())] = it.value();
    return result;
}
quint32 loggerSerial(const QString &value) {
    bool valid = false;
    const quint64 serial = value.toULongLong(&valid);
    return valid && serial > 0 && serial <= std::numeric_limits<quint32>::max() ? quint32(serial) : 0;
}
QString field(const QString &page, const QString &key) {
    const QRegularExpression expression("\\b" + QRegularExpression::escape(key) +
                                        "\\s*=\\s*[\"']([^\"']*)[\"']");
    return expression.match(page).captured(1).trimmed();
}
QVariant scrub(QVariant value, const QStringList &redactions) {
    if (value.metaType().id() == QMetaType::QVariantMap) {
        auto map = value.toMap();
        for (auto it = map.begin(); it != map.end(); ++it)
            it.value() = scrub(it.value(), redactions);
        return map;
    }
    if (value.metaType().id() == QMetaType::QVariantList) {
        auto list = value.toList();
        for (auto &entry : list)
            entry = scrub(entry, redactions);
        return list;
    }
    if (value.metaType().id() == QMetaType::QString) {
        auto text = value.toString();
        for (const auto &secret : redactions)
            if (!secret.isEmpty())
                text.replace(secret, "[redacted]", Qt::CaseInsensitive);
        return text;
    }
    return value;
}
} // namespace

DiagnosticProbe::DiagnosticProbe(QObject *parent) : QObject(parent) {
    m_network.setProxy(QNetworkProxy::NoProxy);
    m_deadline.setSingleShot(true);
    m_deadline.setInterval(45000);
    m_httpDeadline.setSingleShot(true);
    m_httpDeadline.setInterval(3000);
    m_step.setSingleShot(true);
    connect(&m_deadline, &QTimer::timeout, this, [this] {
        if (!m_running)
            return;
        const QString error = "The 45-second diagnostic collection deadline was reached.";
        if (m_waitingRead)
            recordBlock({}, error);
        addError("deadline", error);
        finish(m_completed > 0 ? "partial" : "error", "Collection timed out; partial report is ready");
    });
    connect(&m_httpDeadline, &QTimer::timeout, this, [this] {
        if (m_httpReply) {
            m_httpTimedOut = true;
            m_httpReply->abort();
        }
    });
    connect(&m_step, &QTimer::timeout, this, &DiagnosticProbe::readNext);
    connect(&m_transport, &Transport::registersReady, this, &DiagnosticProbe::received);
    connect(&m_transport, &Transport::failed, this, &DiagnosticProbe::readFailed);
    connect(&m_transport, &Transport::wire, this, [this](QString direction, QByteArray frame) {
        if (m_running)
            m_wire.append(QVariantMap{{"direction", direction},
                                      {"time", timestamp(QDateTime::currentDateTimeUtc())},
                                      {"hex", QString::fromLatin1(frame.toHex())}});
    });
}

DiagnosticProbe::~DiagnosticProbe() { stopIo(); }

void DiagnosticProbe::captureFailure(QVariantMap settings, Registers registers, QVariantList wire,
                                     QString error, QString exactModel) {
    if (m_running)
        return;
    ++m_generation;
    stopIo();
    m_settings = std::move(settings);
    m_exactModel = exactModel.trimmed();
    m_notes.clear();
    m_extended = false;
    m_started = m_finished = QDateTime::currentDateTimeUtc();
    m_readStarted = {};
    m_http = {{"attempted", false},
              {"reason", "Existing monitoring evidence; no additional network requests were made."}};
    m_logger.clear();
    m_detection.clear();
    m_decoded.clear();
    m_blocks.clear();
    m_errors.clear();
    m_registers = std::move(registers);
    m_initialIdentity.clear();
    m_plan.clear();
    m_identifiers.clear();
    m_secrets.clear();
    m_profile = nullptr;
    m_completed = m_total = m_next = 0;
    m_waitingRead = false;
    m_httpTimedOut = false;
    m_identityVerifiedAtEnd = false;
    m_host = m_settings.value("ip").toString().trimmed();
    m_expectedInverter = m_settings.value("inverter_serial").toString().trimmed();
    m_httpInverter.clear();
    const QString suppliedSerial = m_settings.value("serial").toString().trimmed();
    m_loggerSerial = loggerSerial(suppliedSerial);
    m_logger = {{"ip", m_host},
                {"serial", suppliedSerial},
                {"mac", m_settings.value("logger_mac").toString()},
                {"firmware", ""}};
    for (const auto &key : {"ip", "serial", "logger_mac", "inverter_serial"})
        m_identifiers.append(m_settings.value(key).toString().trimmed());
    for (auto it = m_settings.cbegin(); it != m_settings.cend(); ++it)
        if (it.key().contains("pass", Qt::CaseInsensitive) ||
            it.key().contains("user", Qt::CaseInsensitive) || it.key().contains("ssid", Qt::CaseInsensitive))
            m_secrets.append(it.value().toString());
    bool completeIdentity = true;
    for (int reg = 0; reg < 25; ++reg) {
        if (m_registers.contains(reg))
            m_initialIdentity[reg] = m_registers.value(reg);
        else
            completeIdentity = false;
    }
    m_identifiers.append(Profiles::serial(m_initialIdentity));
    m_wire = std::move(wire);
    for (const auto &entry : m_wire) {
        const auto frame = entry.toMap();
        const QString time = frame.value("time", frame.value("timestamp")).toString();
        const auto captured = QDateTime::fromString(time, Qt::ISODateWithMs);
        if (captured.isValid() && captured < m_started)
            m_started = captured;
        const auto bytes = QByteArray::fromHex(frame.value("hex").toString().toLatin1());
        if (bytes.size() >= 11 && quint8(bytes[0]) == 0xa5) {
            const auto serial = qFromLittleEndian<quint32>(bytes.constData() + 7);
            if (serial != 0)
                m_identifiers.append(QString::number(serial));
        }
    }
    if (error.isEmpty())
        error = "The monitoring read did not complete.";
    QString selectionReason = "The failed read did not contain a complete identity register block.";
    if (completeIdentity) {
        Device device;
        selectionReason.clear();
        m_profile = m_profiles.select(m_initialIdentity, "auto", device, selectionReason);
    }
    m_detection = {{"status", m_profile ? "supported" : "unsupported"},
                   {"profile", m_profile ? m_profile->id : QString()},
                   {"reason", error},
                   {"selection_reason", selectionReason}};
    m_blocks.append(QVariantMap{{"purpose", "failed_monitoring_snapshot"},
                                {"evidence_only", true},
                                {"start", m_registers.isEmpty() ? 0 : m_registers.firstKey()},
                                {"last", m_registers.isEmpty() ? 0 : m_registers.lastKey()},
                                {"count", m_registers.size()},
                                {"requested_at", timestamp(m_started)},
                                {"completed_at", timestamp(m_finished)},
                                {"ok", false},
                                {"error", error},
                                {"registers", rawMap(m_registers)}});
    addError("monitoring", error);
    m_outcome = "partial";
    m_status = "The failed read was captured; a diagnostic report is ready to save";
    m_ready = true;
    // This is a synchronous evidence snapshot, not a running probe finishing.
    // Emitting finished() here would resume a monitor paused for another reason.
    emit changed();
}

void DiagnosticProbe::setDetails(QString exactModel, QString notes) {
    if (m_running)
        return;
    exactModel = exactModel.trimmed();
    if (m_exactModel == exactModel && m_notes == notes)
        return;
    m_exactModel = std::move(exactModel);
    m_notes = std::move(notes);
    emit changed();
}

void DiagnosticProbe::start(QVariantMap settings, QString exactModel, QString notes, bool extended) {
    if (m_running)
        return;
    ++m_generation;
    stopIo();
    m_settings = std::move(settings);
    m_exactModel = exactModel.trimmed();
    m_notes = std::move(notes);
    m_extended = extended;
    m_started = QDateTime::currentDateTimeUtc();
    m_finished = {};
    m_http.clear();
    m_logger.clear();
    m_detection = {{"status", "unsupported"}, {"profile", ""}, {"reason", "Identity has not been read."}};
    m_decoded.clear();
    m_blocks.clear();
    m_wire.clear();
    m_errors.clear();
    m_registers.clear();
    m_initialIdentity.clear();
    m_plan.clear();
    m_identifiers.clear();
    m_secrets.clear();
    m_profile = nullptr;
    m_completed = 0;
    m_total = 1;
    m_next = 0;
    m_waitingRead = false;
    m_httpTimedOut = false;
    m_identityVerifiedAtEnd = false;
    m_ready = false;
    m_running = true;
    m_outcome = "collecting";
    m_host = m_settings.value("ip").toString().trimmed();
    m_expectedInverter = m_settings.value("inverter_serial").toString().trimmed();
    m_httpInverter.clear();
    for (const auto &key : {"ip", "serial", "logger_mac", "inverter_serial"})
        m_identifiers.append(m_settings.value(key).toString().trimmed());
    for (auto it = m_settings.cbegin(); it != m_settings.cend(); ++it)
        if (it.key().contains("pass", Qt::CaseInsensitive) ||
            it.key().contains("user", Qt::CaseInsensitive) || it.key().contains("ssid", Qt::CaseInsensitive))
            m_secrets.append(it.value().toString());
    const QString suppliedSerial = m_settings.value("serial").toString().trimmed();
    m_loggerSerial = loggerSerial(suppliedSerial);
    m_logger = {{"ip", m_host},
                {"serial", suppliedSerial},
                {"mac", m_settings.value("logger_mac").toString()},
                {"firmware", ""}};
    bool validPort = false, validStatusPort = false;
    m_port = m_settings.value("port", 8899).toInt(&validPort);
    m_statusPort = m_settings.value("diagnostic_status_port", 80).toInt(&validStatusPort);
    const QHostAddress address(m_host);
    if (address.protocol() != QAbstractSocket::IPv4Protocol || address.toIPv4Address() == 0 ||
        address.isMulticast() || address == QHostAddress::Broadcast) {
        addError("configuration", "Select a valid logger IPv4 address before collecting diagnostics.");
        finish("error", "A valid logger address is required");
        return;
    }
    if ((!suppliedSerial.isEmpty() && suppliedSerial != "0" && m_loggerSerial == 0) || !validPort ||
        m_port < 1 || m_port > 65535 || !validStatusPort || m_statusPort < 1 || m_statusPort > 65535) {
        addError("configuration",
                 "The saved logger serial or network port is invalid. Correct it in Settings.");
        finish("error", "Correct the saved logger connection settings");
        return;
    }
    m_status = "Checking the selected logger";
    m_deadline.start();
    const int generation = m_generation;
    emit changed();
    if (current(generation))
        requestStatus();
}

void DiagnosticProbe::requestStatus() {
    QUrl url;
    url.setScheme("http");
    url.setHost(m_host);
    url.setPort(m_statusPort);
    url.setPath("/status.html");
    QNetworkRequest request(url);
    request.setTransferTimeout(2500);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    QString user = m_settings.value("http_user", "admin").toString();
    QString password = m_settings.value("http_pass", "admin").toString();
    m_secrets.append(user);
    m_secrets.append(password);
    request.setRawHeader("Authorization", "Basic " + (user + ":" + password).toUtf8().toBase64());
    m_http = {{"requested_at", timestamp(QDateTime::currentDateTimeUtc())}, {"ok", false}};
    auto reply = m_network.get(request);
    m_httpReply = reply;
    const int generation = m_generation;
    connect(reply, &QNetworkReply::readyRead, this, [this, reply, generation] {
        if (current(generation) && reply->bytesAvailable() > maximumHttpBytes) {
            m_http["error"] = "Logger status response exceeded the diagnostic size limit.";
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, generation] { processStatus(reply, generation); });
    m_httpDeadline.start();
}

void DiagnosticProbe::processStatus(QNetworkReply *reply, int generation) {
    if (!current(generation) || m_httpReply != reply) {
        reply->deleteLater();
        return;
    }
    m_httpDeadline.stop();
    m_httpReply = nullptr;
    m_http["completed_at"] = timestamp(QDateTime::currentDateTimeUtc());
    const bool ok = reply->error() == QNetworkReply::NoError &&
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200 &&
                    reply->bytesAvailable() <= maximumHttpBytes;
    const QString page = ok ? QString::fromUtf8(reply->readAll()) : QString();
    QString error = m_http.value("error").toString();
    if (!ok && error.isEmpty())
        error = m_httpTimedOut ? "Logger status request timed out." : reply->errorString();
    reply->disconnect(this);
    reply->deleteLater();
    m_http["ok"] = ok;
    if (!ok) {
        m_http["error"] = error;
        addError("http_status", error);
    } else {
        QVariantMap fields;
        for (const auto &key : {"cover_ver", "webdata_msvn", "webdata_ssvn", "webdata_hsvn",
                                "webdata_pv_type", "webdata_rate_p"}) {
            const auto value = field(page, key);
            if (!value.isEmpty())
                fields[key] = value.left(256);
        }
        m_http["fields"] = fields;
        m_logger["firmware"] = fields.value("cover_ver");
        for (const auto &key : {"cover_sta_ssid", "cover_ap_ssid", "cover_ssid"})
            m_secrets.append(field(page, key));
        for (const auto &key :
             {"cover_mid", "webdata_sn", "cover_sta_mac", "cover_ap_mac", "cover_sta_ip", "cover_ap_ip"})
            m_identifiers.append(field(page, key));
        const quint32 observedSerial = loggerSerial(field(page, "cover_mid"));
        const QString observedMac = field(page, "cover_sta_mac");
        m_httpInverter = field(page, "webdata_sn");
        const QString expectedMac = m_settings.value("logger_mac").toString().trimmed();
        if ((m_loggerSerial != 0 && observedSerial != 0 && observedSerial != m_loggerSerial) ||
            (!expectedMac.isEmpty() && !observedMac.isEmpty() &&
             expectedMac.compare(observedMac, Qt::CaseInsensitive) != 0) ||
            (!m_expectedInverter.isEmpty() && !m_httpInverter.isEmpty() &&
             m_expectedInverter != m_httpInverter)) {
            addError("identity", "The selected address reports a different logger or inverter identity. No "
                                 "registers were read.");
            finish("error", "The selected address belongs to a different device");
            return;
        }
        if (observedSerial != 0)
            m_loggerSerial = observedSerial;
        if (!observedMac.isEmpty())
            m_logger["mac"] = observedMac;
    }
    if (m_loggerSerial == 0) {
        addError("identity", "Logger serial is unavailable. Enter the logger serial in Settings or correct "
                             "its web credentials, then collect again.");
        finish("error", "Logger serial is required before reading registers");
        return;
    }
    m_logger["serial"] = QString::number(m_loggerSerial);
    m_identifiers.append(QString::number(m_loggerSerial));
    beginIdentity();
}

void DiagnosticProbe::beginIdentity() {
    m_transport.configure(m_host, quint16(m_port), m_loggerSerial);
    m_plan = {{0, 25, "identity"}};
    m_next = 0;
    readNext();
}

void DiagnosticProbe::planReads() {
    QList<ReadBlock> additional;
    if (m_profile) {
        for (const auto &block : m_profile->blocks)
            additional.append({block.first, block.second, "profile"});
    } else if (m_extended) {
        QSet<int> addresses;
        for (const auto &profile : m_profiles.all())
            for (const auto &block : profile.blocks)
                for (int reg = block.first; reg < block.first + block.second; ++reg)
                    if (reg >= 25)
                        addresses.insert(reg);
        auto sorted = addresses.values();
        std::sort(sorted.begin(), sorted.end());
        for (qsizetype i = 0; i < sorted.size();) {
            const int start = sorted[i];
            int count = 1;
            while (i + count < sorted.size() && count < 32 && sorted[i + count] == start + count)
                ++count;
            additional.append({start, count, "reference"});
            i += count;
        }
    }
    if (additional.size() > maximumRequests - 2) {
        additional = additional.first(maximumRequests - 2);
        addError("read_plan", "The reference read plan was truncated to the 24-request diagnostic limit.");
    }
    m_plan.append(additional);
    if (!additional.isEmpty())
        m_plan.append({0, 25, "identity_verification"});
    m_total = m_plan.size();
}

void DiagnosticProbe::readNext() {
    if (!m_running || m_waitingRead)
        return;
    if (m_next >= m_plan.size()) {
        finish(m_errors.isEmpty() ? "success" : "partial", "Diagnostic report is ready");
        return;
    }
    const auto block = m_plan[m_next];
    m_readStarted = QDateTime::currentDateTimeUtc();
    m_waitingRead = true;
    m_status = QString("Reading diagnostic block %1 of %2").arg(m_completed + 1).arg(m_total);
    const int generation = m_generation;
    emit changed();
    if (current(generation) && m_waitingRead)
        m_transport.read(block.start, block.count);
}

void DiagnosticProbe::recordBlock(const Registers &registers, const QString &error) {
    if (!m_waitingRead)
        return;
    const auto block = m_plan[m_next];
    const auto now = QDateTime::currentDateTimeUtc();
    m_blocks.append(QVariantMap{{"start", block.start},
                                {"count", block.count},
                                {"purpose", block.purpose},
                                {"requested_at", timestamp(m_readStarted)},
                                {"completed_at", timestamp(now)},
                                {"duration_ms", m_readStarted.msecsTo(now)},
                                {"ok", error.isEmpty()},
                                {"error", error},
                                {"registers", rawMap(registers)}});
    m_registers.insert(registers);
    m_waitingRead = false;
    ++m_completed;
}

void DiagnosticProbe::received(Registers registers) {
    if (!m_running || !m_waitingRead)
        return;
    const auto purpose = m_plan[m_next].purpose;
    recordBlock(registers);
    if (purpose == "identity") {
        m_initialIdentity = registers;
        const QString serial = Profiles::serial(registers);
        m_identifiers.append(serial);
        if ((!m_expectedInverter.isEmpty() && serial != m_expectedInverter) ||
            (!m_httpInverter.isEmpty() && serial != m_httpInverter)) {
            addError("identity", "The FC03 inverter identity does not match the saved or logger-reported "
                                 "inverter. Further reads stopped.");
            finish("error", "Inverter identity verification failed");
            return;
        }
        Device device;
        QString reason;
        m_profile = m_profiles.select(registers, "auto", device, reason);
        m_detection = {{"status", m_profile ? "supported" : "unsupported"},
                       {"profile", m_profile ? m_profile->id : QString()},
                       {"reason", reason}};
        if (!QRegularExpression("\\A[0-9]{10}\\z").match(serial).hasMatch()) {
            if (m_extended)
                addError("identity", "Extended reads require a legible inverter serial so identity can be "
                                     "checked again at the end.");
        } else
            planReads();
    } else if (purpose == "identity_verification") {
        const QString serial = Profiles::serial(registers);
        m_identifiers.append(serial);
        m_identityVerifiedAtEnd = serial == Profiles::serial(m_initialIdentity);
        if (!m_identityVerifiedAtEnd) {
            addError("identity_verification", "The inverter identity changed during collection. The raw "
                                              "blocks must not be combined as one device snapshot.");
            finish("partial", "Identity changed; partial raw report is ready");
            return;
        }
        if (m_profile) {
            QVariantMap values, states;
            static const QStringList deviceStates{"Standby", "Self-check", "Normal", "Alarm", "Fault"};
            for (const auto &metric : m_profile->metrics) {
                const auto decoded = metric.decode(m_registers);
                if (!decoded)
                    continue;
                if (metric.key == "device_state")
                    states[metric.key] = deviceStates.value(int(*decoded), QString::number(*decoded));
                else if (metric.key == "work_mode")
                    states[metric.key] = m_profile->workModes.value(int(*decoded), QString::number(*decoded));
                else
                    values[metric.key] = *decoded;
            }
            m_decoded = {{"profile", m_profile->id}, {"values", values}, {"states", states}};
        }
    }
    ++m_next;
    if (m_next >= m_plan.size()) {
        finish(m_errors.isEmpty() ? "success" : "partial", "Diagnostic report is ready");
        return;
    }
    const int generation = m_generation;
    emit changed();
    if (current(generation))
        m_step.start(spacingMs);
}

void DiagnosticProbe::readFailed(QString error) {
    if (!m_running || !m_waitingRead)
        return;
    const auto purpose = m_plan[m_next].purpose;
    recordBlock({}, error);
    addError(purpose, error);
    if (purpose == "identity") {
        m_detection["reason"] = "The identity register read failed.";
        finish("error", "Identity read failed; partial report is ready");
    } else if (purpose == "identity_verification" || error.contains("Logger serial does not match"))
        finish("partial", "Identity could not be rechecked; partial report is ready");
    else {
        ++m_next;
        const int generation = m_generation;
        emit changed();
        if (current(generation))
            m_step.start(spacingMs);
    }
}

void DiagnosticProbe::addError(const QString &stage, const QString &message) {
    m_errors.append(QVariantMap{
        {"stage", stage}, {"time", timestamp(QDateTime::currentDateTimeUtc())}, {"message", message}});
}

void DiagnosticProbe::stopIo() {
    m_deadline.stop();
    m_httpDeadline.stop();
    m_step.stop();
    m_transport.stop();
    if (m_httpReply) {
        auto reply = m_httpReply.data();
        m_httpReply = nullptr;
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    m_waitingRead = false;
}

void DiagnosticProbe::finish(QString outcome, QString status) {
    if (!m_running)
        return;
    ++m_generation;
    stopIo();
    m_running = false;
    m_ready = true;
    m_outcome = std::move(outcome);
    m_status = std::move(status);
    m_finished = QDateTime::currentDateTimeUtc();
    if (!m_http.isEmpty() && !m_http.contains("completed_at")) {
        m_http["completed_at"] = timestamp(m_finished);
        m_http["error"] = "Collection ended before the status request completed.";
    }
    const int generation = m_generation;
    emit changed();
    if (generation == m_generation)
        emit finished();
}

void DiagnosticProbe::cancel() {
    if (!m_running)
        return;
    if (m_waitingRead)
        recordBlock({}, "Collection cancelled before the reply arrived.");
    finish("cancelled", "Collection cancelled; partial report is ready");
}

QVariantMap DiagnosticProbe::report(bool includeIdentifiers) const {
    QVariantMap result{{"schema_version", 1},
                       {"application", QVariantMap{{"name", QCoreApplication::applicationName()},
                                                   {"version", QCoreApplication::applicationVersion()}}},
                       {"started_at", timestamp(m_started)},
                       {"finished_at", timestamp(m_finished)},
                       {"outcome", m_outcome},
                       {"exact_model", m_exactModel},
                       {"notes", m_notes},
                       {"extended", m_extended},
                       {"logger", m_logger},
                       {"http_status", m_http},
                       {"profile_detection", m_detection},
                       {"identity", QVariantMap{{"inverter_serial", Profiles::serial(m_initialIdentity)},
                                                {"verified_at_end", m_identityVerifiedAtEnd}}},
                       {"raw_registers", rawMap(m_registers)},
                       {"raw_blocks", m_blocks},
                       {"errors", m_errors},
                       {"completed_requests", m_completed},
                       {"planned_requests", m_total},
                       {"privacy", QVariantMap{{"identifiers_included", includeIdentifiers},
                                               {"wire_omitted", !includeIdentifiers}}}};
    if (!m_decoded.isEmpty())
        result["decoded"] = m_decoded;
    if (!includeIdentifiers) {
        auto logger = result.value("logger").toMap();
        logger.remove("ip");
        logger.remove("serial");
        logger.remove("mac");
        result["logger"] = logger;
        auto identity = result.value("identity").toMap();
        identity.remove("inverter_serial");
        result["identity"] = identity;
    }
    auto redactions = m_secrets;
    if (!includeIdentifiers) {
        redactions.append(m_identifiers);
        static const QRegularExpression mac(QStringLiteral("\\A[0-9a-fA-F]{12}\\z"));
        for (const auto &identifier : m_identifiers) {
            QString compact = identifier;
            compact.remove(':');
            compact.remove('-');
            if (!mac.match(compact).hasMatch())
                continue;
            redactions.append(compact);
            QStringList octets;
            for (int n = 0; n < compact.size(); n += 2)
                octets.append(compact.mid(n, 2));
            redactions.append(octets.join(':'));
            redactions.append(octets.join('-'));
        }
    }
    redactions.removeAll(QString());
    redactions.removeDuplicates();
    std::sort(redactions.begin(), redactions.end(),
              [](const auto &a, const auto &b) { return a.size() > b.size(); });
    if (!includeIdentifiers) {
        auto raw = result.value("raw_registers").toMap();
        for (int reg = 3; reg <= 7; ++reg)
            raw.remove(QString::number(reg));
        result["raw_registers"] = raw;
        auto blocks = result.value("raw_blocks").toList();
        for (auto &entry : blocks) {
            auto block = entry.toMap();
            auto registers = block.value("registers").toMap();
            for (int reg = 3; reg <= 7; ++reg)
                registers.remove(QString::number(reg));
            block["registers"] = registers;
            entry = block;
        }
        result["raw_blocks"] = blocks;
    }
    auto sanitized = scrub(result, redactions).toMap();
    // These are FC03 transport frames, not HTTP requests. Preserve their exact
    // bytes even when a short numeric password happens to match hexadecimal text.
    if (includeIdentifiers)
        sanitized["wire"] = m_wire;
    return sanitized;
}

bool DiagnosticProbe::save(QUrl file, bool includeIdentifiers) {
    if (!m_ready || !file.isLocalFile() || file.toLocalFile().isEmpty()) {
        emit exportFailed("Finish collecting diagnostics and choose a local JSON file.");
        return false;
    }
    const QString path = file.toLocalFile();
    QSaveFile output(path);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly) ||
        !output.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        emit exportFailed("Cannot create the diagnostic file with owner-only permissions: " +
                          output.errorString());
        return false;
    }
    const auto bytes = QJsonDocument::fromVariant(report(includeIdentifiers)).toJson(QJsonDocument::Indented);
    if (output.write(bytes) != bytes.size() || !output.commit()) {
        emit exportFailed("Cannot save the diagnostic file: " + output.errorString());
        return false;
    }
    emit exportFinished(path);
    return true;
}

} // namespace Deye
