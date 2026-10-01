#include "hardware_change_tracker.h"

#include <QSettings>
#include <QSysInfo>
#include <QDateTime>
#include <QStringList>
#include <QVariantMap>
#include <algorithm>

namespace Ausyn {
namespace {

QStringList profileKeys()
{
    return {QStringLiteral("processor"), QStringLiteral("graphics"), QStringLiteral("memory"),
        QStringLiteral("storage"), QStringLiteral("windows"), QStringLiteral("firmware")};
}

QString displayName(const QString& key)
{
    if (key == QStringLiteral("processor")) return QStringLiteral("Processor");
    if (key == QStringLiteral("graphics")) return QStringLiteral("Graphics adapter");
    if (key == QStringLiteral("memory")) return QStringLiteral("Installed memory");
    if (key == QStringLiteral("storage")) return QStringLiteral("Storage inventory");
    if (key == QStringLiteral("windows")) return QStringLiteral("Windows version");
    return QStringLiteral("System firmware profile");
}

QStringList currentProfile(const SystemSnapshot& snapshot)
{
    QStringList profile;
    profile << snapshot.processorName.trimmed()
            << snapshot.graphicsName.trimmed()
            << (snapshot.memoryTotalBytes > 0 ? QString::number(snapshot.memoryTotalBytes) : QString())
            << QString()
            << (snapshot.operatingSystem + QStringLiteral(" · ") + QSysInfo::kernelVersion()).trimmed()
            << (snapshot.systemManufacturer + QLatin1Char(' ') + snapshot.systemModel +
                QLatin1Char(' ') + snapshot.biosVendor + QLatin1Char(' ') + snapshot.biosVersion).simplified();

    QStringList disks;
    for (const PhysicalDiskSample& disk : snapshot.physicalDisks) {
        const QString model = disk.model.trimmed();
        if (!model.isEmpty()) disks.append(QStringLiteral("%1|%2|%3")
            .arg(disk.busType, disk.vendor.trimmed(), model));
    }
    std::sort(disks.begin(), disks.end(), [](const QString& left, const QString& right) {
        return left.compare(right, Qt::CaseInsensitive) < 0;
    });
    disks.removeDuplicates();
    profile[3] = disks.join(QStringLiteral("; "));
    return profile;
}

} // namespace

HardwareChangeTracker::HardwareChangeTracker()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("monitoring/hardwareProfile"));
    for (const QString& key : profileKeys()) {
        const QString value = settings.value(key).toString();
        if (!value.isEmpty()) lastProfile_.insert(key, value);
    }
}

QStringList HardwareChangeTracker::observe(const SystemSnapshot& snapshot)
{
    const QStringList keys = profileKeys();
    const QStringList values = currentProfile(snapshot);
    QStringList changes;
    QHash<QString, QString> updated = lastProfile_;
    for (qsizetype index = 0; index < keys.size(); ++index) {
        const QString& key = keys.at(index);
        const QString& value = values.at(index);
        if (value.isEmpty()) continue;
        const QString previous = lastProfile_.value(key);
        if (!previous.isEmpty() && previous != value) {
            QString oldDisplay = previous;
            QString newDisplay = value;
            if (key == QStringLiteral("memory")) {
                oldDisplay = QStringLiteral("%1 GB").arg(previous.toDouble() / 1'000'000'000.0, 0, 'f', 1);
                newDisplay = QStringLiteral("%1 GB").arg(value.toDouble() / 1'000'000'000.0, 0, 'f', 1);
            }
            changes.append(QStringLiteral("%1 changed · %2 → %3")
                .arg(displayName(key), oldDisplay, newDisplay));
        }
        updated.insert(key, value);
    }
    if (updated == lastProfile_) return changes;

    QSettings settings;
    settings.beginGroup(QStringLiteral("monitoring/hardwareProfile"));
    for (auto it = updated.cbegin(); it != updated.cend(); ++it)
        settings.setValue(it.key(), it.value());
    settings.sync();
    lastProfile_ = std::move(updated);
    if (!changes.isEmpty()) {
        settings.endGroup();
        settings.beginGroup(QStringLiteral("monitoring"));
        QVariantList history = settings.value(QStringLiteral("hardwareChangeHistory")).toList();
        const QString timestamp = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        for (const QString& change : changes)
            history.append(QVariantMap{{QStringLiteral("timestampUtc"), timestamp},
                                       {QStringLiteral("description"), change}});
        constexpr qsizetype maximumHistoryEntries = 100;
        if (history.size() > maximumHistoryEntries)
            history.erase(history.begin(), history.end() - maximumHistoryEntries);
        settings.setValue(QStringLiteral("hardwareChangeHistory"), history);
        settings.sync();
    }
    return changes;
}

QStringList HardwareChangeTracker::recentChanges()
{
    QSettings settings;
    const QVariantList history = settings.value(QStringLiteral("monitoring/hardwareChangeHistory")).toList();
    QStringList entries;
    const qsizetype first = std::max<qsizetype>(0, history.size() - 12);
    for (qsizetype index = history.size(); index > first; --index) {
        const QVariantMap entry = history.at(index - 1).toMap();
        const QDateTime timestamp = QDateTime::fromString(
            entry.value(QStringLiteral("timestampUtc")).toString(), Qt::ISODateWithMs).toLocalTime();
        const QString description = entry.value(QStringLiteral("description")).toString().trimmed();
        if (!timestamp.isValid() || description.isEmpty()) continue;
        entries.append(QStringLiteral("%1  ·  %2")
            .arg(timestamp.toString(QStringLiteral("d MMM yyyy, h:mm ap")), description));
    }
    return entries;
}

bool HardwareChangeTracker::clearChangeHistory(QString* errorMessage)
{
    QSettings settings;
    settings.remove(QStringLiteral("monitoring/hardwareChangeHistory"));
    settings.sync();
    if (settings.status() != QSettings::NoError || settings.contains(QStringLiteral("monitoring/hardwareChangeHistory"))) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Ausyn could not clear the local device-change timeline.");
        return false;
    }
    return true;
}

} // namespace Ausyn
