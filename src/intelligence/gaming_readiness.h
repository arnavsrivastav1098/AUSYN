#pragma once

#include "insight_types.h"
#include "../monitoring/system_snapshot.h"

namespace Ausyn {

class GamingReadinessEngine final {
public:
    [[nodiscard]] static GameReadiness analyze(const GameRequirements& requirements,
                                               const SystemSnapshot& snapshot);
};

} // namespace Ausyn
