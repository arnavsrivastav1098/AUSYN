#pragma once

#include "../intelligence/event_log_types.h"

namespace Ausyn {

class EventLogCollector final {
public:
    [[nodiscard]] static EventLogUpdate collect();
    [[nodiscard]] static EventLogUpdate collectRecent(int lookbackMinutes = 10);
};

} // namespace Ausyn
