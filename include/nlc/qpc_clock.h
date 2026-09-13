#pragma once

#include <cstdint>

namespace nlc {

class QpcClock final {
public:
    using Tick = std::int64_t;

    [[nodiscard]] static Tick Now() noexcept;
    [[nodiscard]] static Tick Frequency() noexcept;
    [[nodiscard]] static Tick MicrosecondsToTicks(std::int64_t microseconds) noexcept;
    [[nodiscard]] static double TicksToMicroseconds(Tick ticks) noexcept;
};

}  // namespace nlc

