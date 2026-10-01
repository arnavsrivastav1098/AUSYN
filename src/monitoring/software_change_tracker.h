#pragma once

#include "../intelligence/app_inventory_types.h"

#include <QStringList>

namespace Ausyn {

class SoftwareChangeTracker final {
public:
    [[nodiscard]] static QStringList observe(const AppInventoryUpdate& inventory);
    [[nodiscard]] static QStringList recentChanges();
    [[nodiscard]] static QVector<SoftwareChangeRecord> recentChangeRecords();
    [[nodiscard]] static bool clearHistory(QString* errorMessage = nullptr);
};

} // namespace Ausyn
