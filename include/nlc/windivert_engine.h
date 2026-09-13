#pragma once

#include "nlc/delay_scheduler.h"
#include "nlc/engine_backend.h"

#include <windivert.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace nlc {

struct WinDivertEngineOptions {
    std::string filter{"!loopback and !impostor and !fragment and (tcp or udp)"};
    SchedulerOptions scheduler{};
    std::int16_t priority{0};
};

inline constexpr const char* GlobalWinDivertFilter =
    "!loopback and !impostor and !fragment and (tcp or udp)";

class WinDivertEngine final : public IEngineBackend {
public:
    WinDivertEngine(DelaySettings& settings, WinDivertEngineOptions options = {});
    ~WinDivertEngine();

    WinDivertEngine(const WinDivertEngine&) = delete;
    WinDivertEngine& operator=(const WinDivertEngine&) = delete;

    void Start() override;
    void StopAndFlush() override;
    void StopAndDrop();

    [[nodiscard]] MetricsSnapshot Metrics() const override;
    void ResetMetrics() noexcept override;
    [[nodiscard]] bool IsRunning() const noexcept override;
    [[nodiscard]] bool IsSafetyBypassActive() const noexcept override;
    [[nodiscard]] std::string LastError() const override;

private:
    bool Inject(std::span<const PacketView> packets);
    void ReceiverMain();
    void SetError(std::string message);
    void Stop(bool flush);

    DelaySettings& settings_;
    WinDivertEngineOptions options_;
    HANDLE handle_{INVALID_HANDLE_VALUE};
    std::unique_ptr<DelayScheduler> scheduler_;
    std::thread receiverThread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stopping_{false};
    std::atomic<bool> flushOnStop_{true};

    mutable std::mutex sendMutex_;
    std::vector<std::byte> sendBuffer_;
    std::vector<WINDIVERT_ADDRESS> sendAddresses_;
    mutable std::mutex errorMutex_;
    std::string lastError_;
    MetricsSnapshot finalMetrics_{};
};

}  // namespace nlc
