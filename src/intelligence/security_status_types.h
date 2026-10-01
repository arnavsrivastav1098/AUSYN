#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

namespace Ausyn {

struct ProviderHealth {
    QString title;
    QString status;
    QString detail;
    bool available = false;
};

struct SecurityStatusUpdate {
    ProviderHealth antivirus;
    ProviderHealth firewall;
    ProviderHealth automaticUpdates;
    bool restartRequired = false;
    QDateTime checkedAt;
};

struct UpdateCacheResult {
    bool available = false;
    bool restartRequired = false;
    int updateCount = 0;
    QStringList updateTitles;
    QString status;
    QDateTime checkedAt;
};

} // namespace Ausyn
