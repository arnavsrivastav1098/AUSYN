#include "software_change_tracker.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QMap>
#include <QSettings>
#include <QVariantMap>

namespace Ausyn {
namespace {

constexpr auto kBaselineKey = "monitoring/softwareInventoryBaseline";
constexpr auto kInitializedKey = "monitoring/softwareInventoryInitialized";
constexpr auto kHistoryKey = "monitoring/softwareChangeHistory";
constexpr qsizetype kMaximumHistoryEntries = 100;
constexpr qsizetype kMaximumBaselineEntries = 5000;

QString identityFor(const InstalledAppEntry& app)
{
    const QString scope = app.source.section(QStringLiteral(" · "), 0, 0).trimmed().toCaseFolded();
    const QString identity = scope + QLatin1Char('|') + app.name.trimmed().toCaseFolded() +
        QLatin1Char('|') + app.publisher.trimmed().toCaseFolded();
    return QString::fromLatin1(QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256).toHex());
}

QString versionText(const QString& version)
{
    return version.trimmed().isEmpty() ? QStringLiteral("version not reported") :
        QStringLiteral("version %1").arg(version.trimmed());
}

QVariantMap identityRecord(const InstalledAppEntry& app)
{
    return {{QStringLiteral("name"), app.name.trimmed()},
            {QStringLiteral("publisher"), app.publisher.trimmed()},
            {QStringLiteral("version"), app.version.trimmed()},
            {QStringLiteral("source"), app.source.trimmed()}};
}

} // namespace

QStringList SoftwareChangeTracker::observe(const AppInventoryUpdate& inventory)
{
    if (!inventory.available) return {};

    QMap<QString, QVariantMap> observed;
    for (const InstalledAppEntry& app : inventory.installedApps) {
        if (app.name.trimmed().isEmpty()) continue;
        observed.insert(identityFor(app), identityRecord(app));
    }

    QSettings settings;
    QVariantMap baseline = settings.value(QString::fromLatin1(kBaselineKey)).toMap();
    const bool initialized = settings.value(QString::fromLatin1(kInitializedKey), false).toBool();
    if (!initialized) {
        for (auto it = observed.cbegin(); it != observed.cend(); ++it)
            baseline.insert(it.key(), it.value());
        if (baseline.size() > kMaximumBaselineEntries) {
            const QStringList keys = baseline.keys();
            for (qsizetype index = 0; index < keys.size() - kMaximumBaselineEntries; ++index)
                baseline.remove(keys.at(index));
        }
        settings.setValue(QString::fromLatin1(kBaselineKey), baseline);
        settings.setValue(QString::fromLatin1(kInitializedKey), true);
        settings.sync();
        return {};
    }

    QStringList changes;
    QVariantList history = settings.value(QString::fromLatin1(kHistoryKey)).toList();
    const QString timestamp = (inventory.checkedAt.isValid() ? inventory.checkedAt.toUTC()
        : QDateTime::currentDateTimeUtc()).toString(Qt::ISODateWithMs);
    for (auto it = observed.cbegin(); it != observed.cend(); ++it) {
        const QVariantMap previous = baseline.value(it.key()).toMap();
        const QVariantMap current = it.value();
        const QString name = current.value(QStringLiteral("name")).toString();
        const QString oldVersion = previous.value(QStringLiteral("version")).toString().trimmed();
        const QString newVersion = current.value(QStringLiteral("version")).toString().trimmed();
        QString description;
        if (previous.isEmpty()) {
            description = QStringLiteral("First observed in inventory: %1 (%2)")
                .arg(name, versionText(newVersion));
        } else if (!oldVersion.isEmpty() && !newVersion.isEmpty() && oldVersion != newVersion) {
            description = QStringLiteral("Observed version change: %1 · %2 → %3")
                .arg(name, oldVersion, newVersion);
        }
        if (!description.isEmpty()) {
            const QVariantMap entry{{QStringLiteral("timestampUtc"), timestamp},
                                    {QStringLiteral("description"), description}};
            history.append(entry);
            changes.append(description);
        }
        if (newVersion.isEmpty() && !oldVersion.isEmpty()) {
            QVariantMap retained = current;
            retained.insert(QStringLiteral("version"), oldVersion);
            baseline.insert(it.key(), retained);
        } else {
            baseline.insert(it.key(), current);
        }
    }

    if (baseline.size() > kMaximumBaselineEntries) {
        const QStringList keys = baseline.keys();
        for (qsizetype index = 0; index < keys.size() - kMaximumBaselineEntries; ++index)
            baseline.remove(keys.at(index));
    }
    if (history.size() > kMaximumHistoryEntries)
        history.erase(history.begin(), history.end() - kMaximumHistoryEntries);
    settings.setValue(QString::fromLatin1(kBaselineKey), baseline);
    settings.setValue(QString::fromLatin1(kHistoryKey), history);
    settings.sync();
    return changes;
}

QStringList SoftwareChangeTracker::recentChanges()
{
    QStringList entries;
    for (const SoftwareChangeRecord& record : recentChangeRecords()) {
        entries.append(QStringLiteral("%1  ·  %2")
            .arg(record.observedAt.toLocalTime().toString(QStringLiteral("d MMM yyyy, h:mm ap")),
                 record.description));
    }
    return entries;
}

QVector<SoftwareChangeRecord> SoftwareChangeTracker::recentChangeRecords()
{
    QSettings settings;
    const QVariantList history = settings.value(QString::fromLatin1(kHistoryKey)).toList();
    QVector<SoftwareChangeRecord> entries;
    const qsizetype first = std::max<qsizetype>(0, history.size() - 20);
    for (qsizetype index = history.size(); index > first; --index) {
        const QVariantMap entry = history.at(index - 1).toMap();
        const QDateTime timestamp = QDateTime::fromString(
            entry.value(QStringLiteral("timestampUtc")).toString(), Qt::ISODateWithMs);
        const QString description = entry.value(QStringLiteral("description")).toString().trimmed();
        if (!timestamp.isValid() || description.isEmpty()) continue;
        entries.append(SoftwareChangeRecord{timestamp, description});
    }
    return entries;
}

bool SoftwareChangeTracker::clearHistory(QString* errorMessage)
{
    QSettings settings;
    settings.remove(QString::fromLatin1(kHistoryKey));
    settings.remove(QString::fromLatin1(kBaselineKey));
    settings.remove(QString::fromLatin1(kInitializedKey));
    settings.sync();
    if (settings.status() != QSettings::NoError || settings.contains(QString::fromLatin1(kHistoryKey)) ||
        settings.contains(QString::fromLatin1(kBaselineKey)) || settings.contains(QString::fromLatin1(kInitializedKey))) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Ausyn could not clear the local software-change history.");
        return false;
    }
    return true;
}

} // namespace Ausyn
