#pragma once

#include <QDateTime>
#include <QString>
#include <QVector>

namespace Ausyn {

struct StartupEntry {
    QString name;
    QString command;
    QString source;
    QString detail;
};

struct InstalledAppEntry {
    QString name;
    QString publisher;
    QString version;
    QString installDate;
    QString source;
};

struct SoftwareChangeRecord {
    QDateTime observedAt;
    QString description;
};

struct AppInventoryUpdate {
    QVector<StartupEntry> startupEntries;
    QVector<InstalledAppEntry> installedApps;
    QVector<SoftwareChangeRecord> recentSoftwareChanges;
    QDateTime checkedAt;
    bool available = false;
    QString status;
};

struct StartupResourceSignal {
    QString key;
    QString name;
    QString currentUse;
};

} // namespace Ausyn
