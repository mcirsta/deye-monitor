#pragma once
#include <QMap>
#include <QString>
#include <QVariantMap>
#include <QVector>
#include <optional>

namespace Deye {
using Registers = QMap<int, quint16>;
struct Metric {
    QString key, label, unit;
    QList<int> registers;
    int rule = 1;
    quint16 mask = 0;
    double scale = 1, offset = 0;
    std::optional<double> decode(const Registers &values) const;
};
struct Device {
    QString serial, model, profile;
    double ratedPower = 0;
    int phases = 0, mppts = 0;
};
struct Profile {
    QString id, name;
    QList<int> rating;
    int phaseRegister = 0, phases = 0, mppts = 0;
    double maxPower = 0;
    QVector<QPair<int, int>> blocks;
    QVector<Metric> metrics;
    QStringList workModes;
    Device device(const Registers &) const;
};
class Profiles {
public:
    Profiles();
    const Profile *select(const Registers &, const QString &requested, Device &, QString &error) const;
    const QVector<Profile> &all() const { return m_profiles; }
    const Profile *find(const QString &id) const;
    static QString serial(const Registers &);

private:
    QVector<Profile> m_profiles;
};
} // namespace Deye
