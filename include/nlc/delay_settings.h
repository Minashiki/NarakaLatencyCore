#pragma once

#include "nlc/direction.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <string>

namespace nlc {

inline constexpr std::int64_t MaximumDelayUs = 100'000;

struct GlobalDelaySettings {
    bool engineEnabled{false};
    bool inboundEnabled{true};
    bool outboundEnabled{true};
    std::int64_t inboundDelayUs{0};
    std::int64_t outboundDelayUs{0};
};

[[nodiscard]] inline bool ValidateSettings(
    const GlobalDelaySettings& settings,
    std::string* error = nullptr) {
    if (settings.inboundDelayUs < 0 || settings.inboundDelayUs > MaximumDelayUs ||
        settings.outboundDelayUs < 0 || settings.outboundDelayUs > MaximumDelayUs) {
        if (error != nullptr) {
            *error = "delay must be between 0 and 100000 microseconds";
        }
        return false;
    }
    if (settings.engineEnabled &&
        ((!settings.inboundEnabled || settings.inboundDelayUs == 0) &&
         (!settings.outboundEnabled || settings.outboundDelayUs == 0))) {
        if (error != nullptr) {
            *error = "at least one enabled direction must have a non-zero delay";
        }
        return false;
    }
    return true;
}

[[nodiscard]] inline bool TryMillisecondsToMicroseconds(
    double milliseconds,
    std::int64_t& microseconds) noexcept {
    if (!std::isfinite(milliseconds) || milliseconds < 0.0 || milliseconds > 100.0) {
        return false;
    }
    microseconds = static_cast<std::int64_t>(
        std::llround(milliseconds * 1'000.0));
    return true;
}

struct DelaySettings {
    std::atomic<bool> inboundEnabled{true};
    std::atomic<bool> outboundEnabled{true};
    std::atomic<std::int64_t> inboundDelayUs{0};
    std::atomic<std::int64_t> outboundDelayUs{0};

    void Store(const GlobalDelaySettings& settings) noexcept {
        inboundEnabled.store(settings.inboundEnabled, std::memory_order_release);
        outboundEnabled.store(settings.outboundEnabled, std::memory_order_release);
        inboundDelayUs.store(settings.inboundDelayUs, std::memory_order_release);
        outboundDelayUs.store(settings.outboundDelayUs, std::memory_order_release);
    }

    [[nodiscard]] GlobalDelaySettings Load(bool engineEnabled = true) const noexcept {
        return GlobalDelaySettings{
            engineEnabled,
            inboundEnabled.load(std::memory_order_acquire),
            outboundEnabled.load(std::memory_order_acquire),
            inboundDelayUs.load(std::memory_order_acquire),
            outboundDelayUs.load(std::memory_order_acquire)};
    }

    [[nodiscard]] bool Enabled(Direction direction) const noexcept {
        return direction == Direction::Outbound
            ? outboundEnabled.load(std::memory_order_relaxed)
            : inboundEnabled.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::int64_t DelayUs(Direction direction) const noexcept {
        const auto value = direction == Direction::Outbound
            ? outboundDelayUs.load(std::memory_order_relaxed)
            : inboundDelayUs.load(std::memory_order_relaxed);
        return value < 0 ? 0 : value;
    }
};

}  // namespace nlc
