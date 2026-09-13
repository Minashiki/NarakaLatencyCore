#pragma once

#include "nlc/metrics.h"

#include <string>

namespace nlc {

class IEngineBackend {
public:
    virtual ~IEngineBackend() = default;
    virtual void Start() = 0;
    virtual void StopAndFlush() = 0;
    [[nodiscard]] virtual MetricsSnapshot Metrics() const = 0;
    virtual void ResetMetrics() noexcept = 0;
    [[nodiscard]] virtual bool IsRunning() const noexcept = 0;
    [[nodiscard]] virtual bool IsSafetyBypassActive() const noexcept = 0;
    [[nodiscard]] virtual std::string LastError() const = 0;
};

}  // namespace nlc

