#include "profiles.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <cmath>
#include <stdexcept>

static void initializeProfileResources() { Q_INIT_RESOURCE(profile_data); }
namespace Deye {
std::optional<double> Metric::decode(const Registers &values) const {
    quint64 raw = 0;
    if (registers.isEmpty() || registers.size() > 2)
        return {};
    for (qsizetype i = 0; i < registers.size(); ++i) {
        if (!values.contains(registers[i]))
            return {};
        raw |= quint64(values.value(registers[i])) << (16 * i);
    }
    if (mask)
        raw &= mask;
    qint64 number = qint64(raw);
    int bits = 16 * registers.size();
    if ((rule == 2 || rule == 4) && (raw & (quint64(1) << (bits - 1))))
        number -= qint64(1) << bits;
    return std::round((number - offset) * scale * 1000) / 1000;
}
QString Profiles::serial(const Registers &r) {
    QByteArray bytes;
    for (int n = 3; n <= 7; ++n) {
        if (!r.contains(n))
            return {};
        bytes.append(char(r[n] >> 8));
        bytes.append(char(r[n] & 255));
    }
    return QString::fromLatin1(bytes).remove(QChar::Null).trimmed();
}
Device Profile::device(const Registers &r) const {
    Metric power;
    power.registers = rating;
    power.rule = 4;
    power.scale = .1;
    Device d;
    d.serial = Profiles::serial(r);
    d.profile = id;
    d.ratedPower = power.decode(r).value_or(0);
    d.phases = r.value(phaseRegister) & 15;
    d.mppts = (r.value(phaseRegister) >> 8) & 15;
    d.model = QString("Deye %1 kW · %2 phase").arg(d.ratedPower / 1000, 0, 'g', 4).arg(d.phases);
    return d;
}
Profiles::Profiles() {
    initializeProfileResources();
    const auto files = QDir(":/profiles").entryList({"*.json"}, QDir::Files, QDir::Name);
    if (files.isEmpty())
        throw std::runtime_error("No inverter profiles found");
    for (const auto &filename : files) {
        QFile f(":/profiles/" + filename);
        if (!f.open(QIODevice::ReadOnly))
            throw std::runtime_error("Cannot load inverter profiles");
        auto o = QJsonDocument::fromJson(f.readAll()).object();
        Profile p;
        p.id = o["id"].toString();
        p.name = o["name"].toString();
        for (auto n : o["rating"].toArray())
            p.rating.append(n.toInt());
        p.phaseRegister = o["phase_register"].toInt();
        p.phases = o["phases"].toInt();
        p.mppts = o["mppts"].toInt();
        p.maxPower = o["max_power"].toDouble();
        if (p.id != QFileInfo(filename).completeBaseName() || p.name.isEmpty() || p.rating.size() != 2 ||
            p.phaseRegister < 0 || p.phaseRegister >= 25 || p.phases < 1 || p.phases > 3 || p.mppts < 1 ||
            p.mppts > 4 || p.maxPower < 1000 || p.rating[0] < 0 || p.rating[1] >= 25)
            throw std::runtime_error("Invalid inverter profile identity fields");
        for (auto v : o["blocks"].toArray()) {
            auto a = v.toArray();
            if (a.size() != 2)
                throw std::runtime_error("Invalid profile read block");
            int start = a[0].toInt(), count = a[1].toInt();
            if (count < 1 || count > 32 || start < 0 || start + count > 65536)
                throw std::runtime_error("Invalid profile read block");
            p.blocks.append({start, count});
        }
        for (auto v : o["work_modes"].toArray())
            p.workModes.append(v.toString());
        for (auto v : o["metrics"].toArray()) {
            auto m = v.toObject();
            Metric metric;
            metric.key = m["key"].toString();
            metric.label = m["label"].toString();
            metric.unit = m["unit"].toString();
            metric.rule = m["rule"].toInt(1);
            if (m.contains("mask")) {
                const int mask = m["mask"].toInt();
                if (mask < 1 || mask > 0xffff || metric.rule != 1)
                    throw std::runtime_error("Invalid profile metric mask");
                metric.mask = quint16(mask);
            }
            metric.scale = m["scale"].toDouble(1);
            metric.offset = m["offset"].toDouble(0);
            for (auto n : m["registers"].toArray()) {
                int reg = n.toInt();
                bool covered = false;
                for (auto b : p.blocks)
                    covered |= reg >= b.first && reg < b.first + b.second;
                if (!covered)
                    throw std::runtime_error("Profile metric is not covered by its read plan");
                metric.registers.append(reg);
            }
            if (metric.key.isEmpty() || metric.rule < 1 || metric.rule > 4 ||
                metric.registers.size() != (metric.rule <= 2 ? 1 : 2) || !std::isfinite(metric.scale) ||
                !std::isfinite(metric.offset))
                throw std::runtime_error("Invalid profile metric");
            p.metrics.append(metric);
        }
        m_profiles.append(p);
    }
}
const Profile *Profiles::find(const QString &id) const {
    for (const auto &profile : m_profiles)
        if (profile.id == id)
            return &profile;
    return nullptr;
}
const Profile *Profiles::select(const Registers &r, const QString &requested, Device &device,
                                QString &error) const {
    if (!QRegularExpression("^[0-9]{10}$").match(serial(r)).hasMatch()) {
        error = "Unsupported inverter: invalid serial number";
        return nullptr;
    }
    const Profile *selected = nullptr;
    bool automatic = requested.isEmpty() || requested == "auto";
    for (const auto &p : m_profiles) {
        if (!automatic && requested != p.id)
            continue;
        Device d = p.device(r);
        bool match = d.ratedPower >= 1000 && d.ratedPower <= p.maxPower && d.phases == p.phases;
        if (automatic)
            match &= d.mppts == p.mppts;
        if (!match)
            continue;
        if (selected) {
            error = "Unsupported inverter: ambiguous profile; choose a profile in Settings";
            return nullptr;
        }
        selected = &p;
        device = d;
    }
    if (!selected)
        error = "Unsupported inverter: no register profile matches this device";
    return selected;
}
} // namespace Deye
