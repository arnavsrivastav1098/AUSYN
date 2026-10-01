#include "local_history_store.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QHash>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QStandardPaths>
#include <QTimeZone>
#include <QUuid>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <utility>

namespace Ausyn {
namespace {

constexpr qint64 kMillisecondsPerDay = 24LL * 60LL * 60LL * 1000LL;
constexpr qint64 kVolumeStoragePersistIntervalMilliseconds = 5LL * 60LL * 1000LL;

QString csvField(const QString& value)
{
    QString escaped = value;
    escaped.replace(QStringLiteral("\""), QStringLiteral("\"\""));
    return QStringLiteral("\"%1\"").arg(escaped);
}

void setError(QString* output, const QSqlError& error)
{
    if (output) {
        *output = error.text();
    }
}

bool pruneHistory(QSqlDatabase& database, qint64 cutoffMs, QString* errorMessage)
{
    if (!database.transaction()) {
        setError(errorMessage, database.lastError());
        return false;
    }

    const auto deleteBefore = [&](const QString& statement) {
        QSqlQuery query(database);
        query.prepare(statement);
        query.addBindValue(cutoffMs);
        if (query.exec()) return true;
        setError(errorMessage, query.lastError());
        return false;
    };

    const bool pruned =
        deleteBefore(QStringLiteral("DELETE FROM telemetry_samples WHERE captured_at_ms < ?")) &&
        deleteBefore(QStringLiteral("DELETE FROM insight_events WHERE resolved_at_ms IS NOT NULL AND resolved_at_ms < ?")) &&
        deleteBefore(QStringLiteral("DELETE FROM volume_storage_samples WHERE captured_at_ms < ?"));
    if (!pruned) {
        database.rollback();
        return false;
    }
    if (!database.commit()) {
        setError(errorMessage, database.lastError());
        database.rollback();
        return false;
    }
    return true;
}

void bindOptional(QSqlQuery& query, const std::optional<double>& value)
{
    query.addBindValue(value ? QVariant(*value) : QVariant(QMetaType::fromType<double>()));
}

bool validateHistoryFile(const QString& path, QString* errorMessage)
{
    const QString connectionName = QStringLiteral("ausyn-history-check-%1")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    bool valid = false;
    QString error;
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(path);
        database.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=3000"));
        if (!database.open()) {
            error = database.lastError().text();
        } else {
            QSqlQuery check(database);
            QSqlQuery version(database);
            if (!check.exec(QStringLiteral("PRAGMA integrity_check")) || !check.next()) {
                error = check.lastError().text().isEmpty()
                    ? QStringLiteral("SQLite integrity check returned no result.") : check.lastError().text();
            } else if (check.value(0).toString() != QStringLiteral("ok")) {
                error = QStringLiteral("SQLite reported database damage: %1").arg(check.value(0).toString());
            } else if (!version.exec(QStringLiteral("PRAGMA user_version")) || !version.next()) {
                error = version.lastError().text().isEmpty()
                    ? QStringLiteral("Database schema version is unavailable.") : version.lastError().text();
            } else if (version.value(0).toInt() < 0 || version.value(0).toInt() > 9) {
                error = QStringLiteral("This history backup uses an unsupported database schema version.");
            } else {
                QSqlQuery tables(database);
                if (!tables.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type='table'"))) {
                    error = tables.lastError().text();
                } else {
                    QSet<QString> found;
                    while (tables.next()) found.insert(tables.value(0).toString());
                    const QSet<QString> required{QStringLiteral("telemetry_samples"),
                        QStringLiteral("insight_events"), QStringLiteral("volume_storage_samples")};
                    valid = std::all_of(required.cbegin(), required.cend(),
                        [&found](const QString& table) { return found.contains(table); });
                    if (!valid) error = QStringLiteral("The selected file is not an Ausyn history backup.");
                }
            }
        }
        database.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
    if (!valid && errorMessage) *errorMessage = error.isEmpty()
        ? QStringLiteral("The selected file is not a valid Ausyn history database.") : error;
    return valid;
}

bool copyFileAtomically(const QString& sourcePath, const QString& destinationPath, QString* errorMessage)
{
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly)) {
        if (errorMessage) *errorMessage = source.errorString();
        return false;
    }
    QSaveFile destination(destinationPath);
    if (!destination.open(QIODevice::WriteOnly)) {
        if (errorMessage) *errorMessage = destination.errorString();
        return false;
    }
    QByteArray buffer(1024 * 1024, Qt::Uninitialized);
    while (true) {
        const qint64 read = source.read(buffer.data(), buffer.size());
        if (read < 0) {
            if (errorMessage) *errorMessage = source.errorString();
            destination.cancelWriting();
            return false;
        }
        if (read == 0) break;
        if (destination.write(buffer.constData(), read) != read) {
            if (errorMessage) *errorMessage = destination.errorString();
            destination.cancelWriting();
            return false;
        }
    }
    if (!destination.commit()) {
        if (errorMessage) *errorMessage = destination.errorString();
        return false;
    }
    return true;
}

} // namespace

struct LocalHistoryStore::Impl {
    QString connectionName;
    QString path;
    QSqlDatabase database;
    bool initialized = false;
    qint64 lastPrunedAtMs = 0;
    QHash<QString, qint64> lastVolumeSampleAtMsByRoot;
    int retentionDays = 30;

    Impl()
        : connectionName(QStringLiteral("ausyn-history-%1")
              .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))
    {
    }

    ~Impl()
    {
        if (database.isValid()) {
            database.close();
        }
        database = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName);
    }
};

LocalHistoryStore::LocalHistoryStore()
    : impl_(std::make_unique<Impl>())
{
}

LocalHistoryStore::~LocalHistoryStore() = default;

bool LocalHistoryStore::initialize(int retentionDays, QString* errorMessage)
{
    if (impl_->initialized) {
        impl_->retentionDays = std::clamp(retentionDays, 7, 90);
        return true;
    }
    impl_->retentionDays = std::clamp(retentionDays, 7, 90);

    const QString dataDirectory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (dataDirectory.isEmpty() || !QDir().mkpath(dataDirectory)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Ausyn could not create its local data folder.");
        }
        return false;
    }

    impl_->path = QDir(dataDirectory).filePath(QStringLiteral("ausyn-history.sqlite3"));
    impl_->database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), impl_->connectionName);
    impl_->database.setDatabaseName(impl_->path);
    impl_->database.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=3000"));
    if (!impl_->database.open()) {
        setError(errorMessage, impl_->database.lastError());
        return false;
    }

    QSqlQuery pragmas(impl_->database);
    if (!pragmas.exec(QStringLiteral("PRAGMA journal_mode=WAL")) ||
        !pragmas.exec(QStringLiteral("PRAGMA synchronous=NORMAL")) ||
        !pragmas.exec(QStringLiteral("PRAGMA foreign_keys=ON"))) {
        setError(errorMessage, pragmas.lastError());
        return false;
    }

    QSqlQuery versionQuery(impl_->database);
    if (!versionQuery.exec(QStringLiteral("PRAGMA user_version")) || !versionQuery.next()) {
        setError(errorMessage, versionQuery.lastError());
        return false;
    }
    int schemaVersion = versionQuery.value(0).toInt();
    if (schemaVersion > 9) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("The local history database was created by a newer Ausyn version.");
        }
        return false;
    }

    if (schemaVersion == 0) {
        if (!impl_->database.transaction()) {
            setError(errorMessage, impl_->database.lastError());
            return false;
        }

        QSqlQuery schema(impl_->database);
        const QStringList statements{
            QStringLiteral("CREATE TABLE telemetry_samples ("
                "captured_at_ms INTEGER PRIMARY KEY NOT NULL,"
                "cpu_percent REAL CHECK(cpu_percent IS NULL OR (cpu_percent >= 0 AND cpu_percent <= 100)),"
                "memory_percent REAL CHECK(memory_percent IS NULL OR (memory_percent >= 0 AND memory_percent <= 100)),"
                "memory_used_bytes INTEGER NOT NULL DEFAULT 0,"
                "memory_total_bytes INTEGER NOT NULL DEFAULT 0,"
                "gpu_engine_percent REAL CHECK(gpu_engine_percent IS NULL OR (gpu_engine_percent >= 0 AND gpu_engine_percent <= 100)),"
                "system_drive_free_bytes INTEGER NOT NULL DEFAULT 0,"
                "system_drive_total_bytes INTEGER NOT NULL DEFAULT 0,"
                "battery_percent INTEGER CHECK(battery_percent IS NULL OR (battery_percent >= 0 AND battery_percent <= 100)),"
                "battery_health_percent REAL CHECK(battery_health_percent IS NULL OR (battery_health_percent > 0 AND battery_health_percent <= 200)),"
                "network_receive_bytes_per_second REAL,"
                "network_send_bytes_per_second REAL"
                ")"),
            QStringLiteral("CREATE INDEX telemetry_samples_time_idx ON telemetry_samples(captured_at_ms)"),
            QStringLiteral("CREATE TABLE insight_events ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "rule_id TEXT NOT NULL,"
                "severity INTEGER NOT NULL,"
                "title TEXT NOT NULL,"
                "summary TEXT NOT NULL,"
                "evidence TEXT NOT NULL,"
                "recommendation TEXT NOT NULL,"
                "first_seen_ms INTEGER NOT NULL,"
                "last_seen_ms INTEGER NOT NULL,"
                "resolved_at_ms INTEGER,"
                "recommendation_outcome INTEGER CHECK(recommendation_outcome IS NULL OR recommendation_outcome IN (-1,0,1,2)),"
                "recommendation_outcome_at_ms INTEGER,"
                "verification_started_at_ms INTEGER,"
                "verification_cpu_before REAL CHECK(verification_cpu_before IS NULL OR (verification_cpu_before >= 0 AND verification_cpu_before <= 100)),"
                "verification_memory_before REAL CHECK(verification_memory_before IS NULL OR (verification_memory_before >= 0 AND verification_memory_before <= 100)),"
                "verification_checked_at_ms INTEGER,"
                "verification_cpu_after REAL CHECK(verification_cpu_after IS NULL OR (verification_cpu_after >= 0 AND verification_cpu_after <= 100)),"
                "verification_memory_after REAL CHECK(verification_memory_after IS NULL OR (verification_memory_after >= 0 AND verification_memory_after <= 100))"
                ")"),
            QStringLiteral("CREATE INDEX insight_events_active_idx ON insight_events(resolved_at_ms, severity, last_seen_ms)"),
            QStringLiteral("CREATE INDEX insight_outcome_rule_idx ON insight_events(rule_id,recommendation_outcome)"),
            QStringLiteral("PRAGMA user_version=5"),
        };
        for (const QString& statement : statements) {
            if (!schema.exec(statement)) {
                setError(errorMessage, schema.lastError());
                impl_->database.rollback();
                return false;
            }
        }
        if (!impl_->database.commit()) {
            setError(errorMessage, impl_->database.lastError());
            impl_->database.rollback();
            return false;
        }
        schemaVersion = 5;
    }

    if (schemaVersion == 1) {
        if (!impl_->database.transaction()) {
            setError(errorMessage, impl_->database.lastError());
            return false;
        }
        QSqlQuery migration(impl_->database);
        const QStringList statements{
            QStringLiteral("ALTER TABLE insight_events ADD COLUMN recommendation_outcome INTEGER CHECK(recommendation_outcome IS NULL OR recommendation_outcome IN (-1,0,1,2))"),
            QStringLiteral("ALTER TABLE insight_events ADD COLUMN recommendation_outcome_at_ms INTEGER"),
            QStringLiteral("PRAGMA user_version=2"),
        };
        for (const QString& statement : statements) {
            if (!migration.exec(statement)) {
                setError(errorMessage, migration.lastError());
                impl_->database.rollback();
                return false;
            }
        }
        if (!impl_->database.commit()) {
            setError(errorMessage, impl_->database.lastError());
            impl_->database.rollback();
            return false;
        }
        schemaVersion = 2;
    }

    if (schemaVersion == 2) {
        if (!impl_->database.transaction()) {
            setError(errorMessage, impl_->database.lastError());
            return false;
        }
        QSqlQuery migration(impl_->database);
        if (!migration.exec(QStringLiteral("CREATE INDEX insight_outcome_rule_idx ON insight_events(rule_id,recommendation_outcome)")) ||
            !migration.exec(QStringLiteral("PRAGMA user_version=3"))) {
            setError(errorMessage, migration.lastError());
            impl_->database.rollback();
            return false;
        }
        if (!impl_->database.commit()) {
            setError(errorMessage, impl_->database.lastError());
            impl_->database.rollback();
            return false;
        }
        schemaVersion = 3;
    }

    if (schemaVersion == 3) {
        if (!impl_->database.transaction()) {
            setError(errorMessage, impl_->database.lastError());
            return false;
        }
        QSqlQuery migration(impl_->database);
        const QStringList statements{
            QStringLiteral("ALTER TABLE insight_events ADD COLUMN verification_started_at_ms INTEGER"),
            QStringLiteral("ALTER TABLE insight_events ADD COLUMN verification_cpu_before REAL CHECK(verification_cpu_before IS NULL OR (verification_cpu_before >= 0 AND verification_cpu_before <= 100))"),
            QStringLiteral("ALTER TABLE insight_events ADD COLUMN verification_memory_before REAL CHECK(verification_memory_before IS NULL OR (verification_memory_before >= 0 AND verification_memory_before <= 100))"),
            QStringLiteral("ALTER TABLE insight_events ADD COLUMN verification_checked_at_ms INTEGER"),
            QStringLiteral("ALTER TABLE insight_events ADD COLUMN verification_cpu_after REAL CHECK(verification_cpu_after IS NULL OR (verification_cpu_after >= 0 AND verification_cpu_after <= 100))"),
            QStringLiteral("ALTER TABLE insight_events ADD COLUMN verification_memory_after REAL CHECK(verification_memory_after IS NULL OR (verification_memory_after >= 0 AND verification_memory_after <= 100))"),
            QStringLiteral("PRAGMA user_version=4"),
        };
        for (const QString& statement : statements) {
            if (!migration.exec(statement)) {
                setError(errorMessage, migration.lastError());
                impl_->database.rollback();
                return false;
            }
        }
        if (!impl_->database.commit()) {
            setError(errorMessage, impl_->database.lastError());
            impl_->database.rollback();
            return false;
        }
        schemaVersion = 4;
    }

    if (schemaVersion == 4) {
        if (!impl_->database.transaction()) {
            setError(errorMessage, impl_->database.lastError());
            return false;
        }
        QSqlQuery migration(impl_->database);
        const QStringList statements{
            QStringLiteral("ALTER TABLE telemetry_samples ADD COLUMN battery_health_percent REAL CHECK(battery_health_percent IS NULL OR (battery_health_percent > 0 AND battery_health_percent <= 200))"),
            QStringLiteral("PRAGMA user_version=5"),
        };
        for (const QString& statement : statements) {
            if (!migration.exec(statement)) {
                setError(errorMessage, migration.lastError());
                impl_->database.rollback();
                return false;
            }
        }
        if (!impl_->database.commit()) {
            setError(errorMessage, impl_->database.lastError());
            impl_->database.rollback();
            return false;
        }
        schemaVersion = 5;
    }

    if (schemaVersion == 5) {
        if (!impl_->database.transaction()) {
            setError(errorMessage, impl_->database.lastError());
            return false;
        }
        QSqlQuery migration(impl_->database);
        const QStringList statements{
            QStringLiteral("CREATE TABLE volume_storage_samples ("
                "captured_at_ms INTEGER NOT NULL,"
                "root_path TEXT NOT NULL,"
                "free_bytes INTEGER NOT NULL CHECK(free_bytes >= 0),"
                "total_bytes INTEGER NOT NULL CHECK(total_bytes > 0),"
                "PRIMARY KEY(captured_at_ms, root_path))"),
            QStringLiteral("CREATE INDEX volume_storage_samples_time_idx ON volume_storage_samples(captured_at_ms)"),
            QStringLiteral("CREATE INDEX volume_storage_samples_root_time_idx ON volume_storage_samples(root_path, captured_at_ms)"),
            QStringLiteral("PRAGMA user_version=6"),
        };
        for (const QString& statement : statements) {
            if (!migration.exec(statement)) {
                setError(errorMessage, migration.lastError());
                impl_->database.rollback();
                return false;
            }
        }
        if (!impl_->database.commit()) {
            setError(errorMessage, impl_->database.lastError());
            impl_->database.rollback();
            return false;
        }
        schemaVersion = 6;
    }

    if (schemaVersion == 6) {
        if (!impl_->database.transaction()) {
            setError(errorMessage, impl_->database.lastError());
            return false;
        }
        QSqlQuery migration(impl_->database);
        const QStringList statements{
            QStringLiteral("DELETE FROM volume_storage_samples AS old WHERE EXISTS ("
                "SELECT 1 FROM volume_storage_samples AS newer "
                "WHERE newer.root_path=old.root_path "
                "AND CAST(newer.captured_at_ms / 300000 AS INTEGER)=CAST(old.captured_at_ms / 300000 AS INTEGER) "
                "AND newer.captured_at_ms > old.captured_at_ms)"),
            QStringLiteral("PRAGMA user_version=7"),
        };
        for (const QString& statement : statements) {
            if (!migration.exec(statement)) {
                setError(errorMessage, migration.lastError());
                impl_->database.rollback();
                return false;
            }
        }
        if (!impl_->database.commit()) {
            setError(errorMessage, impl_->database.lastError());
            impl_->database.rollback();
            return false;
        }
        schemaVersion = 7;
    }

    if (schemaVersion == 7) {
        if (!impl_->database.transaction()) {
            setError(errorMessage, impl_->database.lastError());
            return false;
        }
        QSqlQuery migration(impl_->database);
        const QStringList statements{
            QStringLiteral("CREATE INDEX IF NOT EXISTS insight_outcome_history_idx "
                "ON insight_events(COALESCE(resolved_at_ms,last_seen_ms),rule_id,recommendation_outcome)"),
            QStringLiteral("PRAGMA user_version=8"),
        };
        for (const QString& statement : statements) {
            if (!migration.exec(statement)) {
                setError(errorMessage, migration.lastError());
                impl_->database.rollback();
                return false;
            }
        }
        if (!impl_->database.commit()) {
            setError(errorMessage, impl_->database.lastError());
            impl_->database.rollback();
            return false;
        }
        schemaVersion = 8;
    }

    if (schemaVersion == 8) {
        if (!impl_->database.transaction()) {
            setError(errorMessage, impl_->database.lastError());
            return false;
        }
        QSqlQuery migration(impl_->database);
        const QStringList statements{
            QStringLiteral("CREATE INDEX IF NOT EXISTS insight_outcome_latest_idx "
                "ON insight_events(rule_id,COALESCE(resolved_at_ms,last_seen_ms) DESC,id DESC) "
                "WHERE recommendation_outcome IN (-1,0,1)"),
            QStringLiteral("PRAGMA user_version=9"),
        };
        for (const QString& statement : statements) {
            if (!migration.exec(statement)) {
                setError(errorMessage, migration.lastError());
                impl_->database.rollback();
                return false;
            }
        }
        if (!impl_->database.commit()) {
            setError(errorMessage, impl_->database.lastError());
            impl_->database.rollback();
            return false;
        }
        schemaVersion = 9;
    }

    const qint64 startupCutoff = QDateTime::currentMSecsSinceEpoch() -
        static_cast<qint64>(impl_->retentionDays) * kMillisecondsPerDay;
    if (!pruneHistory(impl_->database, startupCutoff, errorMessage)) return false;

    QSqlQuery volumeTimes(impl_->database);
    if (!volumeTimes.exec(QStringLiteral(
            "SELECT root_path, MAX(captured_at_ms) FROM volume_storage_samples GROUP BY root_path"))) {
        setError(errorMessage, volumeTimes.lastError());
        return false;
    }
    while (volumeTimes.next())
        impl_->lastVolumeSampleAtMsByRoot.insert(
            volumeTimes.value(0).toString().toCaseFolded(), volumeTimes.value(1).toLongLong());

    impl_->lastPrunedAtMs = QDateTime::currentMSecsSinceEpoch();
    impl_->initialized = true;
    return true;
}

bool LocalHistoryStore::saveSnapshot(const SystemSnapshot& snapshot, QString* errorMessage)
{
    if (!impl_->initialized) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Local history has not been initialized.");
        }
        return false;
    }

    const qint64 nowMs = snapshot.capturedAt.toUTC().toMSecsSinceEpoch();
    if (nowMs - impl_->lastPrunedAtMs >= kMillisecondsPerDay) {
        const qint64 retentionCutoff = nowMs - impl_->retentionDays * kMillisecondsPerDay;
        if (!pruneHistory(impl_->database, retentionCutoff, errorMessage)) return false;
        impl_->lastPrunedAtMs = nowMs;
    }

    if (!impl_->database.transaction()) {
        setError(errorMessage, impl_->database.lastError());
        return false;
    }

    QSqlQuery insert(impl_->database);
    insert.prepare(QStringLiteral(
        "INSERT INTO telemetry_samples (captured_at_ms, cpu_percent, memory_percent, memory_used_bytes, "
        "memory_total_bytes, gpu_engine_percent, system_drive_free_bytes, system_drive_total_bytes, "
        "battery_percent, battery_health_percent, network_receive_bytes_per_second, network_send_bytes_per_second) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(captured_at_ms) DO UPDATE SET "
        "cpu_percent=excluded.cpu_percent, memory_percent=excluded.memory_percent, "
        "memory_used_bytes=excluded.memory_used_bytes, memory_total_bytes=excluded.memory_total_bytes, "
        "gpu_engine_percent=excluded.gpu_engine_percent, "
        "system_drive_free_bytes=excluded.system_drive_free_bytes, "
        "system_drive_total_bytes=excluded.system_drive_total_bytes, battery_percent=excluded.battery_percent, "
        "battery_health_percent=excluded.battery_health_percent, "
        "network_receive_bytes_per_second=excluded.network_receive_bytes_per_second, "
        "network_send_bytes_per_second=excluded.network_send_bytes_per_second"));
    insert.addBindValue(snapshot.capturedAt.toUTC().toMSecsSinceEpoch());
    bindOptional(insert, snapshot.processorUsagePercent);
    bindOptional(insert, snapshot.memoryUsagePercent);
    insert.addBindValue(static_cast<qlonglong>(snapshot.memoryUsedBytes));
    insert.addBindValue(static_cast<qlonglong>(snapshot.memoryTotalBytes));
    bindOptional(insert, snapshot.graphicsUsagePercent);
    insert.addBindValue(static_cast<qlonglong>(snapshot.systemVolumeFreeBytes));
    insert.addBindValue(static_cast<qlonglong>(snapshot.systemVolumeTotalBytes));
    insert.addBindValue(snapshot.batteryPercent
        ? QVariant(static_cast<unsigned int>(*snapshot.batteryPercent))
        : QVariant(QMetaType::fromType<unsigned int>()));
    bindOptional(insert, snapshot.batteryHealthPercent);
    bindOptional(insert, snapshot.networkReceiveBytesPerSecond);
    bindOptional(insert, snapshot.networkSendBytesPerSecond);

    if (!insert.exec()) {
        setError(errorMessage, insert.lastError());
        impl_->database.rollback();
        return false;
    }

    QSqlQuery volumeInsert(impl_->database);
    volumeInsert.prepare(QStringLiteral(
        "INSERT INTO volume_storage_samples(captured_at_ms, root_path, free_bytes, total_bytes) "
        "VALUES(?, ?, ?, ?) ON CONFLICT(captured_at_ms, root_path) DO UPDATE SET "
        "free_bytes=excluded.free_bytes, total_bytes=excluded.total_bytes"));
    QStringList sampledVolumeRoots;
    for (const VolumeSample& volume : snapshot.volumes) {
        if (volume.rootPath.trimmed().isEmpty() || volume.totalBytes == 0 ||
            volume.freeBytes > volume.totalBytes) continue;
        const QString rootPath = volume.rootPath.trimmed();
        const QString rootKey = rootPath.toCaseFolded();
        const qint64 previousSampleAtMs = impl_->lastVolumeSampleAtMsByRoot.value(rootKey, 0);
        if (previousSampleAtMs > 0 && nowMs - previousSampleAtMs <
            kVolumeStoragePersistIntervalMilliseconds) continue;
        volumeInsert.bindValue(0, nowMs);
        volumeInsert.bindValue(1, rootPath);
        volumeInsert.bindValue(2, static_cast<qulonglong>(volume.freeBytes));
        volumeInsert.bindValue(3, static_cast<qulonglong>(volume.totalBytes));
        if (!volumeInsert.exec()) {
            setError(errorMessage, volumeInsert.lastError());
            impl_->database.rollback();
            return false;
        }
        sampledVolumeRoots.append(rootKey);
    }
    volumeInsert.finish();
    insert.finish();
    if (!impl_->database.commit()) {
        setError(errorMessage, impl_->database.lastError());
        impl_->database.rollback();
        return false;
    }
    for (const QString& root : std::as_const(sampledVolumeRoots))
        impl_->lastVolumeSampleAtMsByRoot.insert(root, nowMs);
    return true;
}

MetricAverages LocalHistoryStore::averagesSince(const QDateTime& since, QString* errorMessage)
{
    MetricAverages averages;
    if (!impl_->initialized) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Local history is unavailable.");
        }
        return averages;
    }

    QSqlQuery query(impl_->database);
    query.prepare(QStringLiteral(
        "SELECT AVG(cpu_percent), AVG(memory_percent), COUNT(cpu_percent), COUNT(memory_percent) "
        "FROM telemetry_samples WHERE captured_at_ms >= ?"));
    query.addBindValue(since.toUTC().toMSecsSinceEpoch());
    if (!query.exec() || !query.next()) {
        setError(errorMessage, query.lastError());
        return averages;
    }

    averages.processorSamples = query.value(2).toInt();
    averages.memorySamples = query.value(3).toInt();
    if (!query.value(0).isNull()) {
        averages.processorPercent = query.value(0).toDouble();
    }
    if (!query.value(1).isNull()) {
        averages.memoryPercent = query.value(1).toDouble();
    }
    return averages;
}

MetricAverages LocalHistoryStore::averagesBetween(const QDateTime& since,
                                                   const QDateTime& until,
                                                   QString* errorMessage)
{
    MetricAverages averages;
    if (!impl_->initialized) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Local history is unavailable.");
        return averages;
    }
    if (!since.isValid() || !until.isValid() || until <= since) {
        if (errorMessage)
            *errorMessage = QStringLiteral("The requested history window is invalid.");
        return averages;
    }

    QSqlQuery query(impl_->database);
    query.prepare(QStringLiteral(
        "SELECT AVG(cpu_percent), AVG(memory_percent), COUNT(cpu_percent), COUNT(memory_percent) "
        "FROM telemetry_samples WHERE captured_at_ms >= ? AND captured_at_ms < ?"));
    query.addBindValue(since.toUTC().toMSecsSinceEpoch());
    query.addBindValue(until.toUTC().toMSecsSinceEpoch());
    if (!query.exec() || !query.next()) {
        setError(errorMessage, query.lastError());
        return averages;
    }
    averages.processorSamples = query.value(2).toInt();
    averages.memorySamples = query.value(3).toInt();
    if (!query.value(0).isNull()) averages.processorPercent = query.value(0).toDouble();
    if (!query.value(1).isNull()) averages.memoryPercent = query.value(1).toDouble();
    return averages;
}

QVector<MemoryTrendPoint> LocalHistoryStore::recentMemoryTrend(int windowCount, int windowMinutes,
                                                               QString* errorMessage)
{
    QVector<MemoryTrendPoint> points;
    if (!impl_->initialized) {
        if (errorMessage) *errorMessage = QStringLiteral("Local history is unavailable.");
        return points;
    }
    const int boundedWindowCount = std::clamp(windowCount, 4, 24);
    const int boundedWindowMinutes = std::clamp(windowMinutes, 1, 15);
    const qint64 windowMs = static_cast<qint64>(boundedWindowMinutes) * 60 * 1000;
    const qint64 nowMs = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
    QSqlQuery query(impl_->database);
    query.prepare(QStringLiteral(
        "SELECT AVG(memory_percent), COUNT(*), MAX(captured_at_ms) FROM telemetry_samples "
        "WHERE captured_at_ms >= ? AND captured_at_ms <= ? AND memory_percent IS NOT NULL "
        "GROUP BY CAST(captured_at_ms / ? AS INTEGER) ORDER BY 3"));
    query.addBindValue(nowMs - static_cast<qint64>(boundedWindowCount) * windowMs);
    query.addBindValue(nowMs);
    query.addBindValue(windowMs);
    if (!query.exec()) {
        setError(errorMessage, query.lastError());
        return points;
    }
    while (query.next()) {
        if (query.value(0).isNull()) continue;
        MemoryTrendPoint point;
        point.averagePercent = query.value(0).toDouble();
        point.sampleCount = query.value(1).toInt();
        point.capturedAt = QDateTime::fromMSecsSinceEpoch(
            query.value(2).toLongLong(), QTimeZone(QByteArrayLiteral("UTC"))).toLocalTime();
        points.append(std::move(point));
    }
    return points;
}

QVector<HistoryPoint> LocalHistoryStore::recentHistory(int hours, QString* errorMessage)
{
    QVector<HistoryPoint> points;
    if (!impl_->initialized) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Local history is unavailable.");
        }
        return points;
    }

    const int boundedHours = std::clamp(hours, 1, impl_->retentionDays * 24);
    const qint64 since = QDateTime::currentMSecsSinceEpoch() -
                         static_cast<qint64>(boundedHours) * 60LL * 60LL * 1000LL;
    QSqlQuery query(impl_->database);
    const bool aggregate = boundedHours > 24;
    query.prepare(aggregate
        ? QStringLiteral(
            "SELECT CAST(captured_at_ms / 300000 AS INTEGER) * 300000, AVG(cpu_percent), AVG(memory_percent), "
            "MAX(cpu_percent), MAX(memory_percent), COUNT(*), COUNT(cpu_percent), COUNT(memory_percent) "
            "FROM telemetry_samples WHERE captured_at_ms >= ? "
            "GROUP BY CAST(captured_at_ms / 300000 AS INTEGER) ORDER BY 1")
        : QStringLiteral(
            "SELECT captured_at_ms, cpu_percent, memory_percent, gpu_engine_percent, "
            "system_drive_free_bytes, system_drive_total_bytes, battery_percent "
            "FROM telemetry_samples WHERE captured_at_ms >= ? ORDER BY captured_at_ms ASC"));
    query.addBindValue(since);
    if (!query.exec()) {
        setError(errorMessage, query.lastError());
        return points;
    }

    while (query.next()) {
        HistoryPoint point;
        point.capturedAt = QDateTime::fromMSecsSinceEpoch(
            query.value(0).toLongLong(), QTimeZone(QByteArrayLiteral("UTC"))).toLocalTime();
        if (aggregate) {
            if (!query.value(1).isNull()) point.processorPercent = query.value(1).toDouble();
            if (!query.value(2).isNull()) point.memoryPercent = query.value(2).toDouble();
            if (!query.value(3).isNull()) point.processorPeakPercent = query.value(3).toDouble();
            if (!query.value(4).isNull()) point.memoryPeakPercent = query.value(4).toDouble();
            point.sampleCount = query.value(5).toInt();
            point.processorSampleCount = query.value(6).toInt();
            point.memorySampleCount = query.value(7).toInt();
        } else {
            if (!query.value(1).isNull()) {
                point.processorPercent = query.value(1).toDouble();
                point.processorPeakPercent = point.processorPercent;
                point.processorSampleCount = 1;
            }
            if (!query.value(2).isNull()) {
                point.memoryPercent = query.value(2).toDouble();
                point.memoryPeakPercent = point.memoryPercent;
                point.memorySampleCount = 1;
            }
            if (!query.value(3).isNull()) point.graphicsPercent = query.value(3).toDouble();
            const quint64 freeBytes = query.value(4).toULongLong();
            const quint64 totalBytes = query.value(5).toULongLong();
            if (totalBytes > 0) {
                point.systemDriveUsedPercent = 100.0 * static_cast<double>(totalBytes - std::min(freeBytes, totalBytes)) /
                                               static_cast<double>(totalBytes);
            }
            if (!query.value(6).isNull()) point.batteryPercent = query.value(6).toUInt();
        }
        points.append(std::move(point));
    }
    return points;
}

QVector<StorageTrendPoint> LocalHistoryStore::dailyStorageTrend(int days, QString* errorMessage)
{
    QVector<StorageTrendPoint> points;
    if (!impl_->initialized) {
        if (errorMessage) *errorMessage = QStringLiteral("Local history is unavailable.");
        return points;
    }
    const int boundedDays = std::clamp(days, 1, impl_->retentionDays);
    QSqlQuery query(impl_->database);
    query.prepare(QStringLiteral(
        "SELECT CAST(captured_at_ms / 86400000 AS INTEGER), AVG(system_drive_free_bytes), "
        "AVG(system_drive_total_bytes), COUNT(*), MAX(captured_at_ms) FROM telemetry_samples "
        "WHERE captured_at_ms >= ? AND system_drive_total_bytes > 0 "
        "GROUP BY CAST(captured_at_ms / 86400000 AS INTEGER) ORDER BY 1"));
    query.addBindValue(QDateTime::currentMSecsSinceEpoch() -
                       static_cast<qint64>(boundedDays) * kMillisecondsPerDay);
    if (!query.exec()) {
        setError(errorMessage, query.lastError());
        return points;
    }
    while (query.next()) {
        const double freeBytes = query.value(1).toDouble();
        const double totalBytes = query.value(2).toDouble();
        if (totalBytes <= 0.0) continue;
        StorageTrendPoint point;
        point.capturedAt = QDateTime::fromMSecsSinceEpoch(
            query.value(4).toLongLong(), QTimeZone(QByteArrayLiteral("UTC"))).toLocalTime();
        point.freePercent = 100.0 * std::clamp(freeBytes / totalBytes, 0.0, 1.0);
        point.sampleCount = query.value(3).toInt();
        point.totalBytes = static_cast<quint64>(std::max(0.0, totalBytes));
        points.append(point);
    }
    return points;
}

QHash<QString, QVector<StorageTrendPoint>> LocalHistoryStore::dailyVolumeStorageTrends(
    int days, QString* errorMessage)
{
    QHash<QString, QVector<StorageTrendPoint>> trends;
    if (!impl_->initialized) {
        if (errorMessage) *errorMessage = QStringLiteral("Local history is unavailable.");
        return trends;
    }
    const int boundedDays = std::clamp(days, 1, impl_->retentionDays);
    QSqlQuery query(impl_->database);
    query.prepare(QStringLiteral(
        "SELECT root_path, CAST(captured_at_ms / 86400000 AS INTEGER), AVG(free_bytes), "
        "AVG(total_bytes), COUNT(*), MAX(captured_at_ms) FROM volume_storage_samples "
        "WHERE captured_at_ms >= ? AND total_bytes > 0 "
        "GROUP BY root_path, CAST(captured_at_ms / 86400000 AS INTEGER) "
        "ORDER BY root_path, 2"));
    query.addBindValue(QDateTime::currentMSecsSinceEpoch() -
                       static_cast<qint64>(boundedDays) * kMillisecondsPerDay);
    if (!query.exec()) {
        setError(errorMessage, query.lastError());
        return trends;
    }
    while (query.next()) {
        const QString rootPath = query.value(0).toString();
        const double freeBytes = query.value(2).toDouble();
        const double totalBytes = query.value(3).toDouble();
        if (rootPath.isEmpty() || totalBytes <= 0.0) continue;
        StorageTrendPoint point;
        point.rootPath = rootPath;
        point.capturedAt = QDateTime::fromMSecsSinceEpoch(
            query.value(5).toLongLong(), QTimeZone(QByteArrayLiteral("UTC"))).toLocalTime();
        point.freePercent = 100.0 * std::clamp(freeBytes / totalBytes, 0.0, 1.0);
        point.sampleCount = query.value(4).toInt();
        point.totalBytes = static_cast<quint64>(std::max(0.0, totalBytes));
        trends[rootPath].append(std::move(point));
    }
    return trends;
}

QVector<BatteryHealthTrendPoint> LocalHistoryStore::dailyBatteryHealthTrend(int days,
                                                                            QString* errorMessage)
{
    QVector<BatteryHealthTrendPoint> points;
    if (!impl_->initialized) {
        if (errorMessage) *errorMessage = QStringLiteral("Local history is unavailable.");
        return points;
    }
    const int boundedDays = std::clamp(days, 1, impl_->retentionDays);
    QSqlQuery query(impl_->database);
    query.prepare(QStringLiteral(
        "SELECT AVG(battery_health_percent), COUNT(*), MAX(captured_at_ms) FROM telemetry_samples "
        "WHERE captured_at_ms >= ? AND battery_health_percent IS NOT NULL "
        "GROUP BY CAST(captured_at_ms / 86400000 AS INTEGER) ORDER BY 3"));
    query.addBindValue(QDateTime::currentMSecsSinceEpoch() -
                       static_cast<qint64>(boundedDays) * kMillisecondsPerDay);
    if (!query.exec()) {
        setError(errorMessage, query.lastError());
        return points;
    }
    while (query.next()) {
        BatteryHealthTrendPoint point;
        point.estimatedHealthPercent = query.value(0).toDouble();
        point.sampleCount = query.value(1).toInt();
        point.capturedAt = QDateTime::fromMSecsSinceEpoch(
            query.value(2).toLongLong(), QTimeZone(QByteArrayLiteral("UTC"))).toLocalTime();
        if (std::isfinite(point.estimatedHealthPercent) &&
            point.estimatedHealthPercent > 0.0 && point.estimatedHealthPercent <= 200.0)
            points.append(point);
    }
    return points;
}

bool LocalHistoryStore::syncActiveFindings(const QVector<Finding>& findings,
                                          const QDateTime& now,
                                          QString* errorMessage)
{
    if (!impl_->initialized || !impl_->database.transaction()) {
        if (errorMessage) {
            *errorMessage = impl_->initialized
                ? impl_->database.lastError().text()
                : QStringLiteral("Local history is unavailable.");
        }
        return false;
    }

    QHash<QString, bool> activeRuleIds;
    for (const Finding& finding : findings) {
        activeRuleIds.insert(finding.ruleId, true);
    }

    QSqlQuery current(impl_->database);
    if (!current.exec(QStringLiteral("SELECT DISTINCT rule_id FROM insight_events WHERE resolved_at_ms IS NULL"))) {
        setError(errorMessage, current.lastError());
        impl_->database.rollback();
        return false;
    }
    QStringList resolvedRuleIds;
    while (current.next()) {
        const QString ruleId = current.value(0).toString();
        if (!activeRuleIds.contains(ruleId)) {
            resolvedRuleIds.append(ruleId);
        }
    }

    const qint64 nowMs = now.toUTC().toMSecsSinceEpoch();
    for (const QString& ruleId : resolvedRuleIds) {
        QSqlQuery resolve(impl_->database);
        resolve.prepare(QStringLiteral(
            "UPDATE insight_events SET resolved_at_ms=? WHERE rule_id=? AND resolved_at_ms IS NULL"));
        resolve.addBindValue(nowMs);
        resolve.addBindValue(ruleId);
        if (!resolve.exec()) {
            setError(errorMessage, resolve.lastError());
            impl_->database.rollback();
            return false;
        }
    }

    for (const Finding& finding : findings) {
        QSqlQuery existing(impl_->database);
        existing.prepare(QStringLiteral(
            "SELECT id FROM insight_events WHERE rule_id=? AND resolved_at_ms IS NULL ORDER BY id DESC LIMIT 1"));
        existing.addBindValue(finding.ruleId);
        if (!existing.exec()) {
            setError(errorMessage, existing.lastError());
            impl_->database.rollback();
            return false;
        }

        if (existing.next()) {
            QSqlQuery update(impl_->database);
            update.prepare(QStringLiteral(
                "UPDATE insight_events SET severity=?, title=?, summary=?, evidence=?, recommendation=?, last_seen_ms=? "
                "WHERE id=?"));
            update.addBindValue(static_cast<int>(finding.severity));
            update.addBindValue(finding.title);
            update.addBindValue(finding.summary);
            update.addBindValue(finding.evidence);
            update.addBindValue(finding.recommendation);
            update.addBindValue(nowMs);
            update.addBindValue(existing.value(0).toLongLong());
            if (!update.exec()) {
                setError(errorMessage, update.lastError());
                impl_->database.rollback();
                return false;
            }
        } else {
            QSqlQuery insert(impl_->database);
            insert.prepare(QStringLiteral(
                "INSERT INTO insight_events (rule_id, severity, title, summary, evidence, recommendation, "
                "first_seen_ms, last_seen_ms, resolved_at_ms) VALUES (?, ?, ?, ?, ?, ?, ?, ?, NULL)"));
            insert.addBindValue(finding.ruleId);
            insert.addBindValue(static_cast<int>(finding.severity));
            insert.addBindValue(finding.title);
            insert.addBindValue(finding.summary);
            insert.addBindValue(finding.evidence);
            insert.addBindValue(finding.recommendation);
            insert.addBindValue(nowMs);
            insert.addBindValue(nowMs);
            if (!insert.exec()) {
                setError(errorMessage, insert.lastError());
                impl_->database.rollback();
                return false;
            }
        }
    }

    if (!impl_->database.commit()) {
        setError(errorMessage, impl_->database.lastError());
        impl_->database.rollback();
        return false;
    }
    return true;
}

bool LocalHistoryStore::recordRecommendationOutcome(const QString& ruleId,
                                                     const QDateTime& firstSeen,
                                                     RecommendationOutcome outcome,
                                                     QString* errorMessage)
{
    if (!impl_->initialized || ruleId.trimmed().isEmpty() || !firstSeen.isValid()) {
        if (errorMessage) *errorMessage = QStringLiteral("The recommendation incident could not be identified.");
        return false;
    }
    const int value = static_cast<int>(outcome);
    if (value < -1 || value > 2) {
        if (errorMessage) *errorMessage = QStringLiteral("The selected recommendation outcome is invalid.");
        return false;
    }
    QSqlQuery query(impl_->database);
    query.prepare(QStringLiteral(
        "UPDATE insight_events SET recommendation_outcome=?, recommendation_outcome_at_ms=? "
        "WHERE rule_id=? AND first_seen_ms=? AND resolved_at_ms IS NULL AND recommendation_outcome IS NULL"));
    query.addBindValue(value);
    query.addBindValue(QDateTime::currentMSecsSinceEpoch());
    query.addBindValue(ruleId);
    query.addBindValue(firstSeen.toUTC().toMSecsSinceEpoch());
    if (!query.exec()) {
        setError(errorMessage, query.lastError());
        return false;
    }
    if (query.numRowsAffected() != 1) {
        if (errorMessage) *errorMessage = QStringLiteral("This incident is no longer active or already has feedback.");
        return false;
    }
    return true;
}

bool LocalHistoryStore::recordRecommendationVerification(const QString& ruleId,
                                                          const QDateTime& firstSeen,
                                                          const SystemSnapshot& snapshot,
                                                          bool captureBaseline,
                                                          QString* errorMessage)
{
    if (!impl_->initialized || ruleId.trimmed().isEmpty() || !firstSeen.isValid() ||
        !snapshot.capturedAt.isValid()) {
        if (errorMessage) *errorMessage = QStringLiteral("The incident or telemetry sample could not be identified.");
        return false;
    }

    QSqlQuery current(impl_->database);
    current.prepare(QStringLiteral(
        "SELECT verification_started_at_ms, verification_cpu_before, verification_memory_before "
        "FROM insight_events WHERE rule_id=? AND first_seen_ms=? AND resolved_at_ms IS NULL"));
    current.addBindValue(ruleId);
    current.addBindValue(firstSeen.toUTC().toMSecsSinceEpoch());
    if (!current.exec()) {
        setError(errorMessage, current.lastError());
        return false;
    }
    if (!current.next()) {
        if (errorMessage) *errorMessage = QStringLiteral("This finding is no longer active.");
        return false;
    }

    const qint64 capturedAtMs = snapshot.capturedAt.toUTC().toMSecsSinceEpoch();
    QSqlQuery update(impl_->database);
    if (captureBaseline) {
        if (!snapshot.processorUsagePercent && !snapshot.memoryUsagePercent) {
            if (errorMessage) *errorMessage = QStringLiteral("CPU and memory readings are unavailable, so Ausyn cannot start a comparison.");
            return false;
        }
        update.prepare(QStringLiteral(
            "UPDATE insight_events SET verification_started_at_ms=?, verification_cpu_before=?, "
            "verification_memory_before=?, verification_checked_at_ms=NULL, verification_cpu_after=NULL, "
            "verification_memory_after=NULL WHERE rule_id=? AND first_seen_ms=? AND resolved_at_ms IS NULL"));
        update.addBindValue(capturedAtMs);
        bindOptional(update, snapshot.processorUsagePercent);
        bindOptional(update, snapshot.memoryUsagePercent);
    } else {
        if (current.value(0).isNull()) {
            if (errorMessage) *errorMessage = QStringLiteral("Start a before-reading comparison before recording the later reading.");
            return false;
        }
        const bool comparableCpu = !current.value(1).isNull() && snapshot.processorUsagePercent.has_value();
        const bool comparableMemory = !current.value(2).isNull() && snapshot.memoryUsagePercent.has_value();
        if (!comparableCpu && !comparableMemory) {
            if (errorMessage) *errorMessage = QStringLiteral("No CPU or memory reading is available both before and now.");
            return false;
        }
        const qint64 baselineAtMs = current.value(0).toLongLong();
        if (capturedAtMs <= baselineAtMs) {
            if (errorMessage) *errorMessage = QStringLiteral("The later reading must be captured after the baseline.");
            return false;
        }
        update.prepare(QStringLiteral(
            "UPDATE insight_events SET verification_checked_at_ms=?, verification_cpu_after=?, "
            "verification_memory_after=? WHERE rule_id=? AND first_seen_ms=? AND resolved_at_ms IS NULL"));
        update.addBindValue(capturedAtMs);
        bindOptional(update, snapshot.processorUsagePercent);
        bindOptional(update, snapshot.memoryUsagePercent);
    }
    update.addBindValue(ruleId);
    update.addBindValue(firstSeen.toUTC().toMSecsSinceEpoch());
    if (!update.exec()) {
        setError(errorMessage, update.lastError());
        return false;
    }
    if (update.numRowsAffected() != 1) {
        if (errorMessage) *errorMessage = QStringLiteral("The finding changed before its comparison could be saved.");
        return false;
    }
    return true;
}

QVector<Finding> LocalHistoryStore::activeFindings(QString* errorMessage)
{
    QVector<Finding> findings;
    if (!impl_->initialized) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Local history is unavailable.");
        }
        return findings;
    }

    QSqlQuery query(impl_->database);
    if (!query.exec(QStringLiteral(
            "SELECT current.rule_id, current.severity, current.title, current.summary, current.evidence, current.recommendation, "
            "current.first_seen_ms, current.last_seen_ms, current.recommendation_outcome, "
            "(SELECT COUNT(*) FROM insight_events past WHERE past.rule_id=current.rule_id AND past.id<>current.id AND past.recommendation_outcome IN (-1,0,1)), "
            "(SELECT COUNT(*) FROM insight_events past WHERE past.rule_id=current.rule_id AND past.id<>current.id AND past.recommendation_outcome=1), "
            "current.verification_started_at_ms, current.verification_cpu_before, current.verification_memory_before, "
            "current.verification_checked_at_ms, current.verification_cpu_after, current.verification_memory_after "
            "FROM insight_events current WHERE current.resolved_at_ms IS NULL ORDER BY current.severity DESC, current.last_seen_ms DESC"))) {
        setError(errorMessage, query.lastError());
        return findings;
    }
    while (query.next()) {
        Finding finding;
        finding.ruleId = query.value(0).toString();
        finding.severity = static_cast<FindingSeverity>(query.value(1).toInt());
        finding.title = query.value(2).toString();
        finding.summary = query.value(3).toString();
        finding.evidence = query.value(4).toString();
        if (finding.ruleId == QStringLiteral("processor-sustained-load") ||
            finding.ruleId == QStringLiteral("memory-sustained-pressure") ||
            finding.ruleId == QStringLiteral("concurrent-cpu-memory-pressure")) {
            finding.confidence = QStringLiteral("High");
            finding.confidenceBasis = QStringLiteral(
                "Repeated measurements support a sustained pressure pattern; they do not establish its cause.");
        } else if (finding.ruleId == QStringLiteral("system-drive-low-space")) {
            finding.confidence = QStringLiteral("High");
            finding.confidenceBasis = QStringLiteral(
                "Windows directly reports the current volume capacity; practical impact depends on your workload.");
        } else if (finding.ruleId == QStringLiteral("battery-low-charge")) {
            finding.confidence = QStringLiteral("High");
            finding.confidenceBasis = QStringLiteral(
                "Windows reports low charge while the device is not connected to AC power.");
        }
        finding.recommendation = query.value(5).toString();
        const QTimeZone utc(QByteArrayLiteral("UTC"));
        finding.firstSeen = QDateTime::fromMSecsSinceEpoch(query.value(6).toLongLong(), utc).toLocalTime();
        finding.lastSeen = QDateTime::fromMSecsSinceEpoch(query.value(7).toLongLong(), utc).toLocalTime();
        if (!query.value(8).isNull())
            finding.recommendationOutcome = static_cast<RecommendationOutcome>(query.value(8).toInt());
        finding.priorRatedOutcomeReports = query.value(9).toInt();
        finding.priorImprovementReports = query.value(10).toInt();
        if (!query.value(11).isNull())
            finding.verificationStartedAt = QDateTime::fromMSecsSinceEpoch(query.value(11).toLongLong(), utc).toLocalTime();
        if (!query.value(12).isNull()) finding.verificationCpuBefore = query.value(12).toDouble();
        if (!query.value(13).isNull()) finding.verificationMemoryBefore = query.value(13).toDouble();
        if (!query.value(14).isNull())
            finding.verificationCheckedAt = QDateTime::fromMSecsSinceEpoch(query.value(14).toLongLong(), utc).toLocalTime();
        if (!query.value(15).isNull()) finding.verificationCpuAfter = query.value(15).toDouble();
        if (!query.value(16).isNull()) finding.verificationMemoryAfter = query.value(16).toDouble();
        findings.append(std::move(finding));
    }
    return findings;
}

QVector<Finding> LocalHistoryStore::incidentHistorySince(const QDateTime& since, int limit,
                                                          QString* errorMessage)
{
    QVector<Finding> findings;
    if (!impl_->initialized || !since.isValid()) {
        if (errorMessage) *errorMessage = QStringLiteral("Local incident history is unavailable.");
        return findings;
    }
    limit = std::clamp(limit, 1, 1000);
    QSqlQuery query(impl_->database);
    query.prepare(QStringLiteral(
        "SELECT rule_id,severity,title,summary,evidence,recommendation,first_seen_ms,last_seen_ms,resolved_at_ms,"
        "recommendation_outcome,recommendation_outcome_at_ms,verification_started_at_ms,verification_cpu_before,"
        "verification_memory_before,verification_checked_at_ms,verification_cpu_after,verification_memory_after "
        "FROM insight_events WHERE COALESCE(resolved_at_ms,last_seen_ms)>=? "
        "ORDER BY COALESCE(resolved_at_ms,last_seen_ms) DESC,id DESC LIMIT ?"));
    query.addBindValue(since.toUTC().toMSecsSinceEpoch());
    query.addBindValue(limit);
    if (!query.exec()) {
        setError(errorMessage, query.lastError());
        return findings;
    }
    const QTimeZone utc(QByteArrayLiteral("UTC"));
    while (query.next()) {
        Finding finding;
        finding.ruleId = query.value(0).toString();
        finding.severity = static_cast<FindingSeverity>(query.value(1).toInt());
        finding.title = query.value(2).toString();
        finding.summary = query.value(3).toString();
        finding.evidence = query.value(4).toString();
        finding.recommendation = query.value(5).toString();
        finding.firstSeen = QDateTime::fromMSecsSinceEpoch(query.value(6).toLongLong(), utc).toLocalTime();
        finding.lastSeen = QDateTime::fromMSecsSinceEpoch(query.value(7).toLongLong(), utc).toLocalTime();
        if (!query.value(8).isNull())
            finding.resolvedAt = QDateTime::fromMSecsSinceEpoch(query.value(8).toLongLong(), utc).toLocalTime();
        if (!query.value(9).isNull())
            finding.recommendationOutcome = static_cast<RecommendationOutcome>(query.value(9).toInt());
        if (!query.value(10).isNull())
            finding.recommendationOutcomeAt = QDateTime::fromMSecsSinceEpoch(query.value(10).toLongLong(), utc).toLocalTime();
        if (!query.value(11).isNull())
            finding.verificationStartedAt = QDateTime::fromMSecsSinceEpoch(query.value(11).toLongLong(), utc).toLocalTime();
        if (!query.value(12).isNull()) finding.verificationCpuBefore = query.value(12).toDouble();
        if (!query.value(13).isNull()) finding.verificationMemoryBefore = query.value(13).toDouble();
        if (!query.value(14).isNull())
            finding.verificationCheckedAt = QDateTime::fromMSecsSinceEpoch(query.value(14).toLongLong(), utc).toLocalTime();
        if (!query.value(15).isNull()) finding.verificationCpuAfter = query.value(15).toDouble();
        if (!query.value(16).isNull()) finding.verificationMemoryAfter = query.value(16).toDouble();
        findings.append(std::move(finding));
    }
    return findings;
}

QVector<RecommendationOutcomeSummary> LocalHistoryStore::recommendationOutcomeSummariesSince(
    const QDateTime& since, QString* errorMessage)
{
    QVector<RecommendationOutcomeSummary> summaries;
    if (!impl_->initialized || !since.isValid()) {
        if (errorMessage) *errorMessage = QStringLiteral("Local recommendation history is unavailable.");
        return summaries;
    }

    QSqlQuery query(impl_->database);
    query.prepare(QStringLiteral(
        "SELECT grouped.rule_id,"
        "(SELECT title FROM insight_events latest WHERE latest.rule_id=grouped.rule_id "
        "AND latest.recommendation_outcome IN (-1,0,1) "
        "AND COALESCE(latest.resolved_at_ms,latest.last_seen_ms)>=? "
        "ORDER BY COALESCE(latest.resolved_at_ms,latest.last_seen_ms) DESC,latest.id DESC LIMIT 1),"
        "(SELECT recommendation FROM insight_events latest WHERE latest.rule_id=grouped.rule_id "
        "AND latest.recommendation_outcome IN (-1,0,1) "
        "AND COALESCE(latest.resolved_at_ms,latest.last_seen_ms)>=? "
        "ORDER BY COALESCE(latest.resolved_at_ms,latest.last_seen_ms) DESC,latest.id DESC LIMIT 1),"
        "grouped.rated_reports,grouped.improvement_reports FROM ("
        "SELECT rule_id,COUNT(*) AS rated_reports,"
        "SUM(CASE WHEN recommendation_outcome=1 THEN 1 ELSE 0 END) AS improvement_reports "
        "FROM insight_events WHERE COALESCE(resolved_at_ms,last_seen_ms)>=? "
        "AND recommendation_outcome IN (-1,0,1) GROUP BY rule_id) grouped "
        "ORDER BY grouped.rated_reports DESC,grouped.rule_id"));
    const qint64 cutoffMs = since.toUTC().toMSecsSinceEpoch();
    query.addBindValue(cutoffMs);
    query.addBindValue(cutoffMs);
    query.addBindValue(cutoffMs);
    if (!query.exec()) {
        setError(errorMessage, query.lastError());
        return summaries;
    }
    while (query.next()) {
        RecommendationOutcomeSummary summary;
        summary.ruleId = query.value(0).toString();
        summary.title = query.value(1).toString();
        summary.recentRecommendation = query.value(2).toString();
        summary.ratedReports = query.value(3).toInt();
        summary.improvementReports = query.value(4).toInt();
        summaries.append(std::move(summary));
    }
    return summaries;
}

QString LocalHistoryStore::databasePath() const
{
    return impl_->path;
}

qint64 LocalHistoryStore::databaseSizeBytes() const
{
    if (impl_->path.isEmpty()) {
        return 0;
    }
    qint64 bytes = QFileInfo(impl_->path).size();
    bytes += QFileInfo(impl_->path + QStringLiteral("-wal")).size();
    bytes += QFileInfo(impl_->path + QStringLiteral("-shm")).size();
    return bytes;
}

bool LocalHistoryStore::exportCsv(const QString& filePath, QString* errorMessage)
{
    if (!impl_->initialized || filePath.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Local history is unavailable or the export path is empty.");
        return false;
    }
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorMessage) *errorMessage = file.errorString();
        return false;
    }
    const QByteArray header = QByteArrayLiteral("record_type,timestamp_utc,cpu_percent,memory_percent,memory_used_bytes,memory_total_bytes,gpu_engine_percent,system_drive_free_bytes,system_drive_total_bytes,battery_percent,battery_health_estimate_percent,network_receive_bytes_per_second,network_send_bytes_per_second,rule_id,severity,title,summary,evidence,recommendation,recommendation_outcome,recommendation_outcome_at_utc,verification_started_at_utc,verification_cpu_before_percent,verification_memory_before_percent,verification_checked_at_utc,verification_cpu_after_percent,verification_memory_after_percent,volume_root_path\r\n");
    if (file.write(header) != header.size()) {
        if (errorMessage) *errorMessage = file.errorString();
        return false;
    }
    QSqlQuery samples(impl_->database);
    if (!samples.exec(QStringLiteral("SELECT captured_at_ms,cpu_percent,memory_percent,memory_used_bytes,memory_total_bytes,gpu_engine_percent,system_drive_free_bytes,system_drive_total_bytes,battery_percent,battery_health_percent,network_receive_bytes_per_second,network_send_bytes_per_second FROM telemetry_samples ORDER BY captured_at_ms"))) {
        setError(errorMessage, samples.lastError());
        return false;
    }
    while (samples.next()) {
        QStringList fields{QStringLiteral("sample"), csvField(QDateTime::fromMSecsSinceEpoch(samples.value(0).toLongLong(), QTimeZone(QByteArrayLiteral("UTC"))).toString(Qt::ISODateWithMs))};
        for (int i = 1; i <= 11; ++i) fields << (samples.value(i).isNull() ? QString() : samples.value(i).toString());
        while (fields.size() < 28) fields << QString();
        const QByteArray row = (fields.join(QLatin1Char(',')) + QStringLiteral("\r\n")).toUtf8();
        if (file.write(row) != row.size()) { if (errorMessage) *errorMessage = file.errorString(); return false; }
    }
    QSqlQuery volumes(impl_->database);
    if (!volumes.exec(QStringLiteral("SELECT captured_at_ms,root_path,free_bytes,total_bytes FROM volume_storage_samples ORDER BY captured_at_ms,root_path"))) {
        setError(errorMessage, volumes.lastError());
        return false;
    }
    while (volumes.next()) {
        QStringList fields(28, QString());
        fields[0] = QStringLiteral("volume_sample");
        fields[1] = csvField(QDateTime::fromMSecsSinceEpoch(volumes.value(0).toLongLong(),
            QTimeZone(QByteArrayLiteral("UTC"))).toString(Qt::ISODateWithMs));
        fields[7] = volumes.value(2).toString();
        fields[8] = volumes.value(3).toString();
        fields[27] = csvField(volumes.value(1).toString());
        const QByteArray row = (fields.join(QLatin1Char(',')) + QStringLiteral("\r\n")).toUtf8();
        if (file.write(row) != row.size()) { if (errorMessage) *errorMessage = file.errorString(); return false; }
    }
    QSqlQuery events(impl_->database);
    if (!events.exec(QStringLiteral("SELECT rule_id,severity,title,summary,evidence,recommendation,first_seen_ms,recommendation_outcome,recommendation_outcome_at_ms,verification_started_at_ms,verification_cpu_before,verification_memory_before,verification_checked_at_ms,verification_cpu_after,verification_memory_after FROM insight_events ORDER BY first_seen_ms"))) {
        setError(errorMessage, events.lastError());
        return false;
    }
    while (events.next()) {
        QStringList fields(28, QString());
        fields[0] = QStringLiteral("finding");
        fields[1] = csvField(QDateTime::fromMSecsSinceEpoch(events.value(6).toLongLong(), QTimeZone(QByteArrayLiteral("UTC"))).toString(Qt::ISODateWithMs));
        fields[13] = csvField(events.value(0).toString());
        fields[14] = QString::number(events.value(1).toInt());
        for (int i = 0; i < 4; ++i) fields[15 + i] = csvField(events.value(2 + i).toString());
        if (!events.value(7).isNull()) {
            const auto outcome = static_cast<RecommendationOutcome>(events.value(7).toInt());
            switch (outcome) {
            case RecommendationOutcome::Improved: fields[19] = QStringLiteral("improved"); break;
            case RecommendationOutcome::NoChange: fields[19] = QStringLiteral("no_change"); break;
            case RecommendationOutcome::Worse: fields[19] = QStringLiteral("worse"); break;
            case RecommendationOutcome::Unsure: fields[19] = QStringLiteral("unsure"); break;
            }
            fields[20] = csvField(QDateTime::fromMSecsSinceEpoch(events.value(8).toLongLong(),
                QTimeZone(QByteArrayLiteral("UTC"))).toString(Qt::ISODateWithMs));
        }
        if (!events.value(9).isNull())
            fields[21] = csvField(QDateTime::fromMSecsSinceEpoch(events.value(9).toLongLong(),
                QTimeZone(QByteArrayLiteral("UTC"))).toString(Qt::ISODateWithMs));
        if (!events.value(10).isNull()) fields[22] = events.value(10).toString();
        if (!events.value(11).isNull()) fields[23] = events.value(11).toString();
        if (!events.value(12).isNull())
            fields[24] = csvField(QDateTime::fromMSecsSinceEpoch(events.value(12).toLongLong(),
                QTimeZone(QByteArrayLiteral("UTC"))).toString(Qt::ISODateWithMs));
        if (!events.value(13).isNull()) fields[25] = events.value(13).toString();
        if (!events.value(14).isNull()) fields[26] = events.value(14).toString();
        const QByteArray row = (fields.join(QLatin1Char(',')) + QStringLiteral("\r\n")).toUtf8();
        if (file.write(row) != row.size()) { if (errorMessage) *errorMessage = file.errorString(); return false; }
    }
    if (!file.commit()) { if (errorMessage) *errorMessage = file.errorString(); return false; }
    return true;
}

bool LocalHistoryStore::backupTo(const QString& filePath, QString* errorMessage)
{
    if (!impl_->initialized || filePath.trimmed().isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Local history is unavailable or the backup path is empty.");
        return false;
    }
    const QString destination = QFileInfo(filePath).absoluteFilePath();
    if (destination.compare(QFileInfo(impl_->path).absoluteFilePath(), Qt::CaseInsensitive) == 0) {
        if (errorMessage) *errorMessage = QStringLiteral("Choose a separate file for the backup.");
        return false;
    }
    const QString stagingPath = destination + QStringLiteral(".tmp-") +
        QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString escapedPath = QDir::toNativeSeparators(stagingPath);
    escapedPath.replace(QLatin1Char('\''), QStringLiteral("''"));
    {
        QSqlQuery vacuum(impl_->database);
        if (!vacuum.exec(QStringLiteral("VACUUM INTO '%1'").arg(escapedPath))) {
            setError(errorMessage, vacuum.lastError());
            QFile::remove(stagingPath);
            return false;
        }
    }
    if (!validateHistoryFile(stagingPath, errorMessage)) {
        QFile::remove(stagingPath);
        return false;
    }
    const bool copied = copyFileAtomically(stagingPath, destination, errorMessage);
    QFile::remove(stagingPath);
    return copied;
}

bool LocalHistoryStore::restoreFrom(const QString& filePath, QString* errorMessage)
{
    if (filePath.trimmed().isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("The backup path is empty.");
        return false;
    }
    if (impl_->path.isEmpty()) {
        const QString dataDirectory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        if (dataDirectory.isEmpty() || !QDir().mkpath(dataDirectory)) {
            if (errorMessage) *errorMessage = QStringLiteral("Ausyn could not access its local history folder.");
            return false;
        }
        impl_->path = QDir(dataDirectory).filePath(QStringLiteral("ausyn-history.sqlite3"));
    }
    const QString sourcePath = QFileInfo(filePath).absoluteFilePath();
    if (sourcePath.compare(QFileInfo(impl_->path).absoluteFilePath(), Qt::CaseInsensitive) == 0) {
        if (errorMessage) *errorMessage = QStringLiteral("The active database cannot be restored as its own backup.");
        return false;
    }
    if (!validateHistoryFile(sourcePath, errorMessage)) return false;

    const QString stagingPath = impl_->path + QStringLiteral(".restore-") +
        QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString rollbackPath = impl_->path + QStringLiteral(".rollback-") +
        QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!QFile::copy(sourcePath, stagingPath)) {
        if (errorMessage) *errorMessage = QStringLiteral("Ausyn could not stage the selected backup beside the history database.");
        return false;
    }
    if (!validateHistoryFile(stagingPath, errorMessage)) {
        QFile::remove(stagingPath);
        return false;
    }
    if (impl_->initialized) {
        {
            QSqlQuery checkpoint(impl_->database);
            if (!checkpoint.exec(QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE)"))) {
                if (errorMessage) *errorMessage = checkpoint.lastError().text();
                QFile::remove(stagingPath);
                return false;
            }
        }
    }
    if (impl_->database.isValid()) {
        impl_->database.close();
        impl_->database = QSqlDatabase();
        QSqlDatabase::removeDatabase(impl_->connectionName);
    }
    impl_->initialized = false;
    const bool hadOriginal = QFileInfo::exists(impl_->path);
    if (hadOriginal && !QFile::rename(impl_->path, rollbackPath)) {
        QFile::remove(stagingPath);
        QString reopenError;
        const bool reopened = initialize(impl_->retentionDays, &reopenError);
        if (errorMessage) *errorMessage = QStringLiteral("Ausyn could not preserve the current database before restoring.%1")
            .arg(reopened ? QString{} : QStringLiteral(" Reopening it also failed: %1").arg(reopenError));
        return false;
    }
    QFile::remove(impl_->path + QStringLiteral("-wal"));
    QFile::remove(impl_->path + QStringLiteral("-shm"));
    if (!QFile::rename(stagingPath, impl_->path)) {
        const bool rolledBack = hadOriginal && QFile::rename(rollbackPath, impl_->path) &&
            initialize(impl_->retentionDays, nullptr);
        if (errorMessage) *errorMessage = rolledBack
            ? QStringLiteral("Ausyn could not put the backup in place; the original history database was restored.")
            : (!hadOriginal
                ? QStringLiteral("Ausyn could not put the backup in place; no previous history database was present.")
            : QStringLiteral("Ausyn could not put the backup in place or reopen the original. Recovery file: %1")
                  .arg(rollbackPath));
        QFile::remove(stagingPath);
        return false;
    }

    QString restoreError;
    if (initialize(impl_->retentionDays, &restoreError)) {
        if (hadOriginal) QFile::remove(rollbackPath);
        impl_->lastPrunedAtMs = 0;
        impl_->lastVolumeSampleAtMsByRoot.clear();
        return true;
    }

    impl_->database.close();
    impl_->database = QSqlDatabase();
    QSqlDatabase::removeDatabase(impl_->connectionName);
    impl_->initialized = false;
    QFile::remove(impl_->path);
    QFile::remove(impl_->path + QStringLiteral("-wal"));
    QFile::remove(impl_->path + QStringLiteral("-shm"));
    const bool rolledBack = hadOriginal && QFile::rename(rollbackPath, impl_->path) &&
        initialize(impl_->retentionDays, nullptr);
    if (!rolledBack && errorMessage && hadOriginal)
        *errorMessage = QStringLiteral("Restore failed and automatic recovery could not reopen the previous database. Recovery file: %1. Error: %2")
            .arg(rollbackPath, restoreError);
    else if (rolledBack && errorMessage)
        *errorMessage = QStringLiteral("Restore failed; the previous database was restored. %1").arg(restoreError);
    else if (errorMessage)
        *errorMessage = QStringLiteral("The backup could not be opened and there was no previous history database to restore. %1")
            .arg(restoreError);
    return false;
}

bool LocalHistoryStore::clear(QString* errorMessage)
{
    if (!impl_->initialized) {
        if (errorMessage) *errorMessage = QStringLiteral("Local history is unavailable.");
        return false;
    }
    if (!impl_->database.transaction()) { setError(errorMessage, impl_->database.lastError()); return false; }
    QSqlQuery query(impl_->database);
    if (!query.exec(QStringLiteral("DELETE FROM telemetry_samples")) ||
        !query.exec(QStringLiteral("DELETE FROM volume_storage_samples")) ||
        !query.exec(QStringLiteral("DELETE FROM insight_events")) ||
        !query.exec(QStringLiteral("DELETE FROM sqlite_sequence WHERE name='insight_events'")) ||
        !impl_->database.commit()) {
        if (errorMessage) *errorMessage = query.lastError().text().isEmpty()
            ? impl_->database.lastError().text() : query.lastError().text();
        impl_->database.rollback();
        return false;
    }
    impl_->lastPrunedAtMs = 0;
    impl_->lastVolumeSampleAtMsByRoot.clear();
    return true;
}

bool LocalHistoryStore::setRetentionDays(int days, QString* errorMessage)
{
    if (!impl_->initialized) {
        if (errorMessage) *errorMessage = QStringLiteral("Local history is unavailable.");
        return false;
    }
    const int newRetentionDays = std::clamp(days, 7, 90);
    const qint64 cutoff = QDateTime::currentMSecsSinceEpoch() -
        static_cast<qint64>(newRetentionDays) * kMillisecondsPerDay;
    if (!pruneHistory(impl_->database, cutoff, errorMessage)) return false;
    impl_->retentionDays = newRetentionDays;
    impl_->lastPrunedAtMs = QDateTime::currentMSecsSinceEpoch();
    return true;
}

} // namespace Ausyn
