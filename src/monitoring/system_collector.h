#pragma once

#include "system_snapshot.h"

#include <memory>

namespace Ausyn {

class SystemCollector final {
public:
    SystemCollector();
    ~SystemCollector();

    SystemCollector(const SystemCollector&) = delete;
    SystemCollector& operator=(const SystemCollector&) = delete;

    [[nodiscard]] SystemSnapshot collect();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace Ausyn
