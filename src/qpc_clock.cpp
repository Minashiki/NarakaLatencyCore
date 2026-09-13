#include "nlc/qpc_clock.h"

#include <windows.h>

#include <cmath>
#include <limits>

namespace nlc {

QpcClock::Tick QpcClock::Now() noexcept {
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return value.QuadPart;
}

QpcClock::Tick QpcClock::Frequency() noexcept {
    static const Tick frequency = [] {
        LARGE_INTEGER value{};
        QueryPerformanceFrequency(&value);
        return value.QuadPart;
    }();
    return frequency;
}

QpcClock::Tick QpcClock::MicrosecondsToTicks(std::int64_t microseconds) noexcept {
    if (microseconds <= 0) {
        return 0;
    }
    const long double ticks = static_cast<long double>(microseconds) *
        static_cast<long double>(Frequency()) / 1'000'000.0L;
    if (ticks >= static_cast<long double>((std::numeric_limits<Tick>::max)())) {
        return (std::numeric_limits<Tick>::max)();
    }
    return static_cast<Tick>(std::ceil(ticks));
}

double QpcClock::TicksToMicroseconds(Tick ticks) noexcept {
    return static_cast<double>(ticks) * 1'000'000.0 / static_cast<double>(Frequency());
}

}  // namespace nlc
