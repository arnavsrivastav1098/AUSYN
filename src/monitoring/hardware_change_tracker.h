#pragma once

#include "system_snapshot.h"

#include <QHash>
#include <QString>
#include <QStringList>

namespace Ausyn {

class HardwareChangeTracker final {
public:
    HardwareChangeTracker();
    [[nodiscard]] QStringList observe(const SystemSnapshot& snapshot);
    [[nodiscard]] static QStringList recentChanges();
    [[nodiscard]] static bool clearChangeHistory(QString* errorMessage = nullptr);

private:
    QHash<QString, QString> lastProfile_;
};

} // namespace Ausyn
