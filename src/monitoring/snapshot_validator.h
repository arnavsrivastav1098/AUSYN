#pragma once

#include "system_snapshot.h"

namespace Ausyn {

class SnapshotValidator final {
public:
    // Validates and sanitizes impossible measurements in place. Returns false
    // only when the snapshot timestamp itself cannot be trusted.
    [[nodiscard]] static bool validate(SystemSnapshot& snapshot);
};

} // namespace Ausyn
