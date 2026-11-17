#include "monitor.h"
#include <QFile>
#include <cmath>

namespace Deye {
QVariantMap Monitor::normalized(QVariantMap s) {
    if (!s.contains("ip"))
        s["ip"] = "";
    s["port"] = qBound(1, s.value("port", 8899).toInt(), 65535);
    s["interval_sec"] = qBound(1, s.value("interval_sec", 5).toInt(), 300);
    if (s.value("profile").toString().isEmpty())
        s["profile"] = "auto";
    if (s.value("http_user").toString().isEmpty())
        s["http_user"] = "admin";
    if (s.value("http_pass").toString().isEmpty())
        s["http_pass"] = "admin";
    return s;
}
Monitor::Monitor(bool persist, QObject *parent, std::optional<DiscoveryTargets> discoveryTargets)
    : QObject(parent), m_scan(discoveryTargets), m_discovery(discoveryTargets),
      m_store(QSettings::IniFormat, QSettings::UserScope, "DeyeMonitor", "DeyeMonitor"), m_persist(persist) {
    if (persist) {
        for (auto key : m_store.allKeys())
            m_settings[key] = m_store.value(key);
    }
    m_settings = normalized(m_settings);
    connect(&m_probe, &DiagnosticProbe::finished, this, [this] {
        if (!m_probeRestorePending)
            return;
        m_probeRestorePending = false;
        if (m_probeWasPaused)
            return;
        if (m_resumeAfterProbe) {
            if (m_paused)
                togglePaused();
        } else {
            m_paused = false;
            m_status = m_statusBeforeProbe;
            emit changed();
        }
    });
    connect(&m_probe, &DiagnosticProbe::exportFinished, this, [this] { dismissProbeOffer(); });
    m_poll.setSingleShot(true);
    m_retry.setSingleShot(true);
    connect(&m_poll, &QTimer::timeout, this, &Monitor::beginSnapshot);
    connect(&m_retry, &QTimer::timeout, this, &Monitor::recover);
    connect(&m_transport, &Transport::registersReady, this, &Monitor::received);
    connect(&m_transport, &Transport::failed, this, [this](QString error) {
        const bool wrongLogger = error.contains("Logger serial does not match");
        failed(error, (wrongLogger && !m_tryingSavedHost) || error.startsWith("Inverter rejected"));
    });
    connect(&m_transport, &Transport::heartbeat, this, [this] {
        ++m_heartbeats;
        m_lastHeartbeat = QDateTime::currentDateTimeUtc();
        m_heartbeatThisSnapshot = true;
    });
    connect(&m_transport, &Transport::wire, this, [this](QString direction, QByteArray frame) {
        m_wire.append(QVariantMap{{"direction", direction},
                                  {"timestamp", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                                  {"hex", QString::fromLatin1(frame.toHex())}});
    });
    connect(&m_scan, &Discovery::loggerFound, this, [this](Logger logger) {
        m_scanResults.append(logger.map());
        emit scanChanged();
    });
    connect(&m_scan, &Discovery::finished, this, [this](QList<Logger> loggers, QString error) {
        m_scanResults.clear();
        for (const auto &l : loggers)
            m_scanResults.append(l.map());
        emit scanChanged();
        emit scanFinished(m_scanResults, m_scanTime.elapsed() / 1000.0, error);
    });
    connect(&m_discovery, &Discovery::stageChanged, this, [this](Discovery::Stage stage) {
        if (m_paused || m_demo || stage == Discovery::Stage::SavedHost)
            return;
        m_status = stage == Discovery::Stage::Broadcast ? "Reconnecting · finding your logger by broadcast"
                                                        : "Reconnecting · scanning local network";
        emit changed();
    });
    connect(&m_discovery, &Discovery::finished, this, [this](QList<Logger> loggers, QString error) {
        if (m_paused)
            return;
        if (!error.isEmpty() || loggers.size() != 1) {
            m_busy = false;
            m_connected = false;
            if (m_tryingSavedHost) {
                // One saved-address attempt is enough. Transport and HTTP
                // requests already have their own short deadlines.
                m_tryingSavedHost = false;
                m_startupError = error;
                recover();
                return;
            } else {
                m_backoff = m_backoff == 0 ? 1 : qMin(m_backoff * 2, 15);
                m_status = loggers.size() > 1 ? "Several loggers found. Choose one in Settings."
                                              : QString("Reconnecting · rescan in %1 s").arg(m_backoff);
                m_retry.start(m_backoff * 1000);
                if (loggers.isEmpty() && !host().isEmpty())
                    offerStartupProbe(m_startupError.isEmpty() ? error : m_startupError);
            }
            emit changed();
            emit connectionError(error.isEmpty() ? m_status : error, false);
            return;
        }
        const auto &l = loggers.first();
        m_settings["ip"] = l.ip;
        m_settings["serial"] = QString::number(l.serial);
        if (!l.mac.isEmpty())
            m_settings["logger_mac"] = l.mac;
        if (m_settings.value("inverter_serial").toString().isEmpty() && !l.inverterSerial.isEmpty())
            m_settings["inverter_serial"] = l.inverterSerial;
        save();
        const int generation = m_generation;
        emit settingsChanged();
        if (generation == m_generation && !m_paused && !m_demo)
            configure();
    });
}
Monitor::~Monitor() {
    m_probeRestorePending = false;
    m_probe.cancel();
    m_poll.stop();
    m_retry.stop();
    m_transport.stop();
    m_scan.cancel();
    m_discovery.cancel();
}
QString Monitor::model() const {
    auto label = m_settings.value("model_override").toString();
    return label.isEmpty() ? (m_device.model.isEmpty() ? "Deye inverter" : m_device.model) : label;
}
QVariantList Monitor::profileOptions() const {
    QVariantList result{QVariantMap{{"id", "auto"}, {"name", "Automatic detection (recommended)"}}};
    for (const auto &p : m_profiles.all())
        result.append(QVariantMap{{"id", p.id}, {"name", p.name}});
    return result;
}
ScanOptions Monitor::options(const QVariantMap &s) const {
    ScanOptions o;
    o.knownIp = s.value("ip").toString();
    o.port = s.value("port", 8899).toInt();
    o.statusPort = s.value("diagnostic_status_port", 80).toInt();
    o.serial = s.value("serial").toString().toUInt();
    o.user = s.value("http_user", "admin").toString();
    o.password = s.value("http_pass", "admin").toString();
    o.mac = s.value("logger_mac").toString();
    o.inverterSerial = s.value("inverter_serial").toString();
    return o;
}
void Monitor::save() {
    if (!m_persist || m_demo)
        return;
    for (const auto &key : m_store.allKeys())
        if (!m_settings.contains(key))
            m_store.remove(key);
    for (auto it = m_settings.cbegin(); it != m_settings.cend(); ++it)
        m_store.setValue(it.key(), it.value());
    m_store.sync();
    QFile::setPermissions(m_store.fileName(), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}
void Monitor::applySettings(QVariantMap s, bool connectNow) {
    m_probeRestorePending = false;
    m_probe.cancel();
    m_probeOffered = false;
    m_probeOfferSuppressed = false;
    m_starting = true;
    m_tryingSavedHost = false;
    m_startupError.clear();
    ++m_generation;
    m_demo = false;
    m_paused = false;
    m_poll.stop();
    m_retry.stop();
    m_scan.cancel();
    m_discovery.cancel();
    m_transport.stop();
    m_profile = nullptr;
    m_connected = false;
    m_busy = false;
    m_backoff = 0;
    if (s.value("serial").toString() == m_settings.value("serial").toString() &&
        !s.value("serial").toString().isEmpty()) {
        if (!s.contains("logger_mac"))
            s["logger_mac"] = m_settings.value("logger_mac");
        if (!s.contains("inverter_serial"))
            s["inverter_serial"] = m_settings.value("inverter_serial");
    }
    m_settings = normalized(s);
    m_values.clear();
    m_states.clear();
    m_registers.clear();
    m_wire.clear();
    m_history.clear();
    m_charge.reset();
    m_eta.clear();
    m_device = {};
    m_updated = {};
    save();
    emit settingsChanged();
    emit historyChanged();
    if (connectNow)
        configure();
    else {
        m_status = host().isEmpty() ? "Choose a logger in Settings" : "Ready to connect";
        emit changed();
    }
}
void Monitor::start() {
    if (m_demo || m_probe.running())
        return;
    m_tryingSavedHost = !host().isEmpty();
    if (!m_tryingSavedHost) {
        recover();
        return;
    }
    configure();
}
void Monitor::configure() {
    ++m_generation;
    m_profile = nullptr;
    m_transport.stop();
    m_poll.stop();
    m_retry.stop();
    m_busy = false;
    m_connected = false;
    if (host().isEmpty()) {
        m_status = "Choose a logger in Settings";
        emit changed();
        return;
    }
    auto o = options(m_settings);
    m_status = "Connecting to " + host();
    const int generation = m_generation;
    emit changed();
    if (generation != m_generation || m_paused || m_demo)
        return;
    if (o.serial == 0) {
        m_busy = true;
        m_discovery.start(o, true);
        return;
    }
    m_transport.configure(o.knownIp, quint16(o.port), o.serial);
    beginSnapshot();
}
void Monitor::beginSnapshot() {
    if (m_busy || m_paused || m_demo)
        return;
    m_busy = true;
    m_heartbeatThisSnapshot = false;
    m_registers.clear();
    m_wire.clear();
    // Identity is cached only for the current TCP session.
    if (!m_transport.isConnected())
        m_profile = nullptr;
    if (!m_profile) {
        m_block = -1;
        m_transport.read(0, 25);
    } else {
        m_block = 0;
        nextBlock();
    }
    emit changed();
}
void Monitor::received(Registers registers) {
    m_registers.insert(registers);
    if (m_block == -1) {
        QString serial = Profiles::serial(m_registers),
                expected = m_settings.value("inverter_serial").toString();
        if (!expected.isEmpty() && serial != expected) {
            failed("Inverter identity changed. Select the device in Settings.", !m_tryingSavedHost);
            return;
        }
        QString error;
        m_profile = m_profiles.select(m_registers, m_settings.value("profile").toString(), m_device, error);
        if (!m_profile) {
            failed(error, true);
            return;
        }
        m_tryingSavedHost = false;
        m_settings["inverter_serial"] = serial;
        m_block = 0;
        m_status = "Logger found · reading inverter telemetry";
    } else
        ++m_block;
    const int generation = m_generation;
    QTimer::singleShot(0, this, [this, generation] {
        if (generation == m_generation)
            nextBlock();
    });
    if (m_block == 0)
        emit changed();
}
void Monitor::nextBlock() {
    if (!m_profile)
        return;
    if (m_block >= m_profile->blocks.size()) {
        completeSnapshot();
        return;
    }
    auto b = m_profile->blocks[m_block];
    m_transport.read(b.first, b.second);
}
void Monitor::completeSnapshot() {
    m_starting = false;
    m_tryingSavedHost = false;
    m_startupError.clear();
    m_probeOffered = false;
    m_values.clear();
    m_states.clear();
    static const QStringList deviceStates = {"Standby", "Self-check", "Normal", "Alarm", "Fault"};
    for (const auto &metric : m_profile->metrics) {
        auto value = metric.decode(m_registers);
        if (!value)
            continue;
        if (metric.key == "device_state")
            m_states[metric.key] = deviceStates.value(int(*value), QString::number(*value));
        else if (metric.key == "work_mode")
            m_states[metric.key] = m_profile->workModes.value(int(*value), QString::number(*value));
        else
            m_values[metric.key] = *value;
    }
    double pv = 0;
    for (int n = 1; n <= 4; ++n)
        pv += m_values.value(QString("pv%1_p").arg(n)).toDouble();
    m_values["pv_total_p"] = pv;
    m_updated = QDateTime::currentDateTime();
    m_connected = true;
    m_busy = false;
    m_backoff = 0;
    m_status = "Connected locally";
    m_history.append(QVariantMap{{"time", m_updated.toMSecsSinceEpoch()},
                                 {"pv", pv},
                                 {"load", m_values.value("load_power")},
                                 {"grid", m_values.value("grid_power")},
                                 {"battery", m_values.value("bat_power")}});
    while (m_history.size() > 240)
        m_history.removeFirst();
    estimateCharge();
    save();
    emit settingsChanged();
    emit changed();
    emit historyChanged();
    emit snapshotReady(report());
    if (!m_paused)
        m_poll.start(m_settings.value("interval_sec").toInt() * 1000);
}
void Monitor::failed(QString error, bool fatal) {
    const bool savedHostFailed = m_tryingSavedHost;
    m_tryingSavedHost = false;
    m_startupError = error;
    ++m_generation;
    m_transport.stop();
    m_profile = nullptr;
    m_busy = false;
    m_connected = false;
    m_charge.reset();
    m_eta.clear();
    m_poll.stop();
    if (fatal || !savedHostFailed)
        offerStartupProbe(error);
    m_status = fatal ? error : "Connection lost · finding your logger";
    const int generation = m_generation;
    emit changed();
    if (generation != m_generation)
        return;
    emit connectionError(error, fatal);
    if (generation == m_generation && !fatal && !m_paused) {
        QTimer::singleShot(0, this, [this, generation] {
            if (generation != m_generation)
                return;
            recover();
        });
    }
}
void Monitor::offerStartupProbe(const QString &error) {
    if (!m_starting || m_probe.running() || m_probeOfferSuppressed)
        return;
    if (!m_probeOffered || !m_probe.ready() ||
        m_registers.size() > m_probe.report(true).value("raw_registers").toMap().size())
        m_probe.captureFailure(m_settings, m_registers, m_wire, error,
                               m_settings.value("model_override").toString());
    m_probeOffered = true;
}
void Monitor::recover() {
    if (m_paused || m_demo || m_discovery.running())
        return;
    m_status = "Reconnecting · finding your logger";
    const int generation = m_generation;
    emit changed();
    if (generation == m_generation && !m_paused && !m_demo)
        m_discovery.start(options(m_settings));
}
void Monitor::refresh() {
    if (m_demo || m_probe.running())
        return;
    if (m_busy || m_discovery.running())
        return;
    m_paused = false;
    if (m_profile)
        beginSnapshot();
    else
        configure();
}
void Monitor::togglePaused() {
    if (m_probe.running()) {
        m_probeRestorePending = false;
        m_probe.cancel();
    }
    m_paused = !m_paused;
    if (m_paused) {
        ++m_generation;
        m_transport.stop();
        m_busy = false;
        m_poll.stop();
        m_retry.stop();
        m_tryingSavedHost = false;
        m_discovery.cancel();
        m_charge.reset();
        m_eta.clear();
        m_status = "Monitoring paused";
    } else {
        m_status = "Resuming monitoring";
        refresh();
    }
    emit changed();
}
void Monitor::startProbe(QString exactModel, QString notes, bool extended) {
    if (m_demo || m_probe.running())
        return;
    m_probeWasPaused = m_paused;
    m_statusBeforeProbe = m_status;
    m_resumeAfterProbe = !m_paused && (m_connected || m_busy || m_discovery.running() || m_poll.isActive() ||
                                       m_retry.isActive());
    m_probeRestorePending = true;
    if (!m_paused)
        togglePaused();
    cancelScan();
    m_probe.start(m_settings, exactModel, notes, extended);
}
void Monitor::cancelProbe() { m_probe.cancel(); }
void Monitor::dismissProbeOffer() {
    if (!m_probeOffered)
        return;
    m_probeOffered = false;
    m_probeOfferSuppressed = true;
    emit changed();
}
void Monitor::scanSettings(QVariantMap s) {
    auto o = options(normalized(s));
    o.serial = 0;
    o.mac.clear();
    o.inverterSerial.clear();
    m_scanTime.start();
    m_scanResults.clear();
    m_scan.start(o);
    emit scanChanged();
}
void Monitor::cancelScan() {
    m_scan.cancel();
    emit scanChanged();
}
QVariantMap Monitor::report() const {
    QVariantMap raw;
    for (auto it = m_registers.cbegin(); it != m_registers.cend(); ++it)
        raw[QString::number(it.key())] = it.value();
    return {{"timestamp", m_updated.toUTC().toString(Qt::ISODateWithMs)},
            {"serial", m_device.serial},
            {"model", model()},
            {"profile", m_device.profile},
            {"host", host()},
            {"values", m_values},
            {"states", m_states},
            {"registers", raw},
            {"heartbeats", m_heartbeats},
            {"heartbeat_now", m_heartbeatThisSnapshot},
            {"last_heartbeat",
             m_lastHeartbeat.isValid() ? m_lastHeartbeat.toUTC().toString(Qt::ISODateWithMs) : QString()},
            {"wire", m_wire}};
}
QVariantList Monitor::detailGroups() const {
    QVariantList groups;
    if (!m_profile)
        return groups;
    for (auto group : QStringList{"Battery", "Advanced battery settings", "Solar", "Grid",
                                  "AC output settings", "Grid voltage limits", "Load", "Energy today",
                                  "Energy total", "Temperatures", "BMS summary"}) {
        QVariantList rows;
        auto add = [&rows](const QString &label, const QVariant &value, const QString &unit = QString()) {
            rows.append(QVariantMap{{"label", label}, {"value", value}, {"unit", unit}});
        };
        if (group == "AC output settings") {
            if (m_values.contains("ac_cfg_output_voltage_code")) {
                const int code = m_values.value("ac_cfg_output_voltage_code").toInt();
                const int gridType = m_values.value("ac_cfg_grid_type_code", -1).toInt();
                static const QList<int> singlePhaseVolts{230, 220, 240, 200, 120, 127};
                const QString value = gridType == 0 && code >= 0 && code < singlePhaseVolts.size()
                                          ? QString::number(singlePhaseVolts[code]) + " V"
                                          : QString("Code %1 (grid type %2)").arg(code).arg(gridType);
                add("Configured output voltage", value);
            }
            if (m_values.contains("inv_output_v"))
                add("Measured inverter output", m_values.value("inv_output_v"), "V");
            if (m_values.contains("ac_cfg_frequency_code")) {
                const int code = m_values.value("ac_cfg_frequency_code").toInt();
                add("Grid frequency setting", code == 0   ? "50 Hz"
                                              : code == 1 ? "60 Hz"
                                                          : QString("Code %1").arg(code));
            }
            if (m_values.contains("ac_cfg_grid_type_code")) {
                const int code = m_values.value("ac_cfg_grid_type_code").toInt();
                add("Grid type", code == 0 ? "Single Phase" : QString("Code %1").arg(code));
            }
            if (m_values.contains("ac_cfg_grid_mode_code")) {
                const int code = m_values.value("ac_cfg_grid_mode_code").toInt();
                add("Grid mode", code == 0 ? "General Standard" : QString("Code %1").arg(code));
            }
        } else if (group == "Grid voltage limits") {
            if (m_values.contains("ac_cfg_grid_high_v"))
                add("High grid-voltage limit", m_values.value("ac_cfg_grid_high_v"), "V");
            if (m_values.contains("ac_cfg_grid_low_v"))
                add("Low grid-voltage limit", m_values.value("ac_cfg_grid_low_v"), "V");
        } else {
            for (const auto &m : m_profile->metrics) {
                bool matches = (group == "Solar" && m.key.startsWith("pv")) ||
                               (group == "Battery" && m.key.startsWith("bat_") &&
                                (!m.key.startsWith("bat_cfg_") || m.key == "bat_cfg_shutdown_soc")) ||
                               (group == "Advanced battery settings" && m.key.startsWith("bat_cfg_") &&
                                m.key != "bat_cfg_shutdown_soc") ||
                               (group == "BMS summary" && m.key.startsWith("bms_")) ||
                               (group == "Grid" && m.key.startsWith("grid_")) ||
                               (group == "Load" && m.key.startsWith("load_")) ||
                               (group == "Energy today" && m.key.startsWith("e_today_")) ||
                               (group == "Energy total" && m.key.startsWith("e_total_")) ||
                               (group == "Temperatures" && m.key.startsWith("temp_"));
                if (matches && m_values.contains(m.key))
                    rows.append(
                        QVariantMap{{"label", m.label}, {"value", m_values[m.key]}, {"unit", m.unit}});
            }
        }
        if (!rows.isEmpty()) {
            QString note;
            if (group == "Battery" && m_values.contains("bat_cfg_shutdown_soc"))
                note = "Shutdown SOC stops off-grid AC output at this inverter-reported SOC. "
                       "Parallel-pack coverage is unverified.";
            else if (group == "Advanced battery settings")
                note = "Read-only inverter settings. Configured capacity is not a detected pack count.";
            else if (group == "BMS summary")
                note = "Inverter-reported BMS values; coverage of parallel packs is unverified.";
            else if (group == "AC output settings")
                note = "Read-only Grid Set1 selections. Configured voltage is the target; measured output "
                       "can vary.";
            else if (group == "Grid voltage limits")
                note = "Read-only grid protection limits, not inverter output-voltage targets.";
            groups.append(
                QVariantMap{{"title", group},
                            {"rows", rows},
                            {"note", note},
                            {"advanced", group == "Advanced battery settings" || group == "BMS summary"}});
        }
    }
    return groups;
}
void Monitor::estimateCharge() {
    m_eta.clear();
    if (!m_values.contains("bat_soc")) {
        m_charge.reset();
        return;
    }
    m_charge.observe(m_updated.toMSecsSinceEpoch(), m_values.value("bat_soc").toDouble());
    if (!m_values.contains("bat_power") || m_values.value("bat_power").toDouble() >= 0)
        return;
    if (const auto remaining = m_charge.timeToFullMs())
        m_eta = formatChargeEta(*remaining);
}
void Monitor::setDemo() {
    m_probeRestorePending = false;
    m_probe.cancel();
    m_probeOffered = false;
    m_probeOfferSuppressed = false;
    m_starting = false;
    m_tryingSavedHost = false;
    m_startupError.clear();
    ++m_generation;
    m_transport.stop();
    m_poll.stop();
    m_retry.stop();
    m_discovery.cancel();
    m_charge.reset();
    m_demo = true;
    m_connected = false;
    m_busy = false;
    m_device = {"DEMO PREVIEW", "Deye 10 kW · 1 phase", "deye-am3", 10000, 1, 3};
    m_profile = m_profiles.find("deye-am3");
    m_status = "Demo preview · no inverter connected";
    m_values = {{"pv_total_p", 5620.0},
                {"pv1_p", 2505.0},
                {"pv2_p", 3115.0},
                {"pv3_p", 0.0},
                {"bat_soc", 68.0},
                {"bat_v", 52.6},
                {"bat_i", -21.3},
                {"bat_temp", 21.0},
                {"bat_power", -1120.0},
                {"bat_cfg_capacity", 200.0},
                {"bat_cfg_max_charge_i", 110.0},
                {"bat_cfg_max_discharge_i", 220.0},
                {"bat_cfg_shutdown_soc", 20.0},
                {"bat_cfg_restart_soc", 50.0},
                {"bat_cfg_low_soc", 35.0},
                {"bat_cfg_shutdown_v", 46.0},
                {"bat_cfg_restart_v", 52.0},
                {"bat_cfg_low_v", 47.5},
                {"ac_cfg_output_voltage_code", 0.0},
                {"ac_cfg_grid_mode_code", 0.0},
                {"ac_cfg_frequency_code", 0.0},
                {"ac_cfg_grid_type_code", 0.0},
                {"ac_cfg_grid_high_v", 265.0},
                {"ac_cfg_grid_low_v", 185.0},
                {"inv_output_v", 230.0},
                {"bms_charge_v_limit", 57.6},
                {"bms_charge_i_limit", 150.0},
                {"bms_discharge_i_limit", 150.0},
                {"bms_soc", 68.0},
                {"bms_v", 52.7},
                {"bms_i", -11.0},
                {"bms_temp", 18.0},
                {"load_power", 4480.0},
                {"grid_power", 20.0},
                {"e_today_pv", 18.4},
                {"e_today_load", 12.6},
                {"e_today_chg", 7.2},
                {"e_today_dis", 4.1},
                {"e_today_imp", .2},
                {"e_today_exp", 0.0},
                {"temp_dc", 35.2},
                {"temp_ac", 43.1},
                {"load_freq", 50.0}};
    m_states = {{"device_state", "Normal"}, {"work_mode", "Zero Export To CT"}};
    m_eta = "1 h 42 min to full";
    m_updated = QDateTime::currentDateTime();
    m_history.clear();
    for (int n = 0; n < 160; ++n)
        m_history.append(QVariantMap{{"time", m_updated.addSecs((n - 159) * 5).toMSecsSinceEpoch()},
                                     {"pv", 5200 + 800 * std::sin(n * .025) + 180 * std::sin(n * .2)},
                                     {"load", 3800 + 650 * std::sin(n * .06)},
                                     {"battery", -1000 + 450 * std::sin(n * .05)},
                                     {"grid", 35 * std::sin(n * .17)}});
    emit changed();
    emit settingsChanged();
    emit historyChanged();
}
} // namespace Deye
