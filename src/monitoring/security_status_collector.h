#pragma once

#include "../intelligence/security_status_types.h"

namespace Ausyn {

class SecurityStatusCollector final {
public:
    [[nodiscard]] static SecurityStatusUpdate readSecurityStatus();
    [[nodiscard]] static UpdateCacheResult scanLocalUpdateCache();
};

} // namespace Ausyn
