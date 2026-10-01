#pragma once

#include "../intelligence/app_inventory_types.h"

namespace Ausyn {

class AppInventoryCollector final {
public:
    [[nodiscard]] static AppInventoryUpdate collect();
};

} // namespace Ausyn
