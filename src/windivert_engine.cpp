#include "nlc/windivert_engine.h"

#include "nlc/qpc_clock.h"

#include <windows.h>

#include <cstring>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace nlc {
namespace {

std::string Win32ErrorMessage(const char* operation, DWORD error) {
    std::ostringstream message;
    message << operation << " failed with Win32 error " << error;
    return message.str();
}

}  // namespace

WinDivertEngine::WinDivertEngine(DelaySettings& settings, WinDivertEngineOptions options)
    : settings_(settings), options_(std::move(options)) {
    if (options_.scheduler.maximumBatchSize > WINDIVERT_BATCH_MAX) {
        throw std::invalid_argument("maximum batch size exceeds WINDIVERT_BATCH_MAX");
    }
    sendAddresses_.reserve(options_.scheduler.maximumBatchSize);
    sendBuffer_.reserve(
        options_.scheduler.maximumBatchSize * options_.scheduler.bytesPerPacket);
}

WinDivertEngine::~WinDivertEngine() {
    StopAndFlush();
}

void WinDivertEngine::Start() {
    if (running_.load(std::memory_order_acquire)) {
        return;
    }
    stopping_.store(false, std::memory_order_release);
    handle_ = WinDivertOpen(
        options_.filter.c_str(), WINDIVERT_LAYER_NETWORK, options_.priority, 0);
    if (handle_ == INVALID_HANDLE_VALUE) {
        const auto error = GetLastError();
        SetError(Win32ErrorMessage("WinDivertOpen", error));
        throw std::system_error(
            static_cast<int>(error), std::system_category(), "WinDivertOpen failed");
    }

    // These are kernel capture-queue safety margins, not the delay queue.
    WinDivertSetParam(
        handle_, WINDIVERT_PARAM_QUEUE_LENGTH, WINDIVERT_PARAM_QUEUE_LENGTH_MAX);
    WinDivertSetParam(handle_, WINDIVERT_PARAM_QUEUE_TIME, WINDIVERT_PARAM_QUEUE_TIME_MAX);
    WinDivertSetParam(handle_, WINDIVERT_PARAM_QUEUE_SIZE, WINDIVERT_PARAM_QUEUE_SIZE_MAX);

    try {
        scheduler_ = std::make_unique<DelayScheduler>(
            settings_,
            [this](std::span<const PacketView> packets) { return Inject(packets); },
            options_.scheduler);
        scheduler_->Start();
        running_.store(true, std::memory_order_release);
        receiverThread_ = std::thread(&WinDivertEngine::ReceiverMain, this);
    } catch (...) {
        if (scheduler_) {
            scheduler_->StopAndFlush();
            scheduler_.reset();
        }
        WinDivertClose(handle_);
        handle_ = INVALID_HANDLE_VALUE;
        throw;
    }
}

void WinDivertEngine::StopAndFlush() {
    Stop(true);
}

void WinDivertEngine::StopAndDrop() {
    Stop(false);
}

MetricsSnapshot WinDivertEngine::Metrics() const {
    return scheduler_ ? scheduler_->Metrics() : finalMetrics_;
}

void WinDivertEngine::ResetMetrics() noexcept {
    if (scheduler_) {
        scheduler_->ResetMetrics();
    } else {
        finalMetrics_ = {};
    }
}

bool WinDivertEngine::IsRunning() const noexcept {
    return running_.load(std::memory_order_acquire);
}

bool WinDivertEngine::IsSafetyBypassActive() const noexcept {
    return scheduler_ && scheduler_->IsSafetyBypassActive();
}

std::string WinDivertEngine::LastError() const {
    std::lock_guard lock(errorMutex_);
    return lastError_;
}

bool WinDivertEngine::Inject(std::span<const PacketView> packets) {
    if (packets.empty()) {
        return true;
    }
    std::lock_guard lock(sendMutex_);
    if (handle_ == INVALID_HANDLE_VALUE) {
        return false;
    }

    std::size_t totalLength = 0;
    for (const auto& packet : packets) {
        totalLength += packet.length;
    }
    if (totalLength > (std::numeric_limits<UINT>::max)() ||
        packets.size() * sizeof(WINDIVERT_ADDRESS) >
            (std::numeric_limits<UINT>::max)()) {
        SetError("WinDivertSendEx batch exceeds API length limits");
        return false;
    }

    sendBuffer_.resize(totalLength);
    sendAddresses_.resize(packets.size());
    std::size_t offset = 0;
    for (std::size_t index = 0; index < packets.size(); ++index) {
        const auto& packet = packets[index];
        std::memcpy(sendBuffer_.data() + offset, packet.data, packet.length);
        sendAddresses_[index] = *packet.address;
        offset += packet.length;
    }

    UINT sentLength = 0;
    const auto ok = WinDivertSendEx(
        handle_, sendBuffer_.data(), static_cast<UINT>(sendBuffer_.size()),
        &sentLength, 0, sendAddresses_.data(),
        static_cast<UINT>(sendAddresses_.size() * sizeof(WINDIVERT_ADDRESS)), nullptr);
    if (!ok || sentLength != sendBuffer_.size()) {
        SetError(Win32ErrorMessage("WinDivertSendEx", GetLastError()));
        return false;
    }
    return true;
}

void WinDivertEngine::ReceiverMain() {
    std::vector<std::byte> receiveBuffer(65'535);
    for (;;) {
        WINDIVERT_ADDRESS address{};
        UINT addressLength = sizeof(address);
        UINT receivedLength = 0;
        const auto ok = WinDivertRecvEx(
            handle_, receiveBuffer.data(), static_cast<UINT>(receiveBuffer.size()),
            &receivedLength, 0, &address, &addressLength, nullptr);
        const auto captureTime = QpcClock::Now();
        if (!ok) {
            const auto error = GetLastError();
            if (!stopping_.load(std::memory_order_acquire) &&
                error != ERROR_OPERATION_ABORTED && error != ERROR_NO_DATA) {
                SetError(Win32ErrorMessage("WinDivertRecvEx", error));
                scheduler_->RecordReceiverError();
                scheduler_->ActivateSafetyBypass();
                WinDivertShutdown(handle_, WINDIVERT_SHUTDOWN_RECV);
            }
            break;
        }

        // No sleeping, parsing, or policy work belongs on this thread.
        (void)scheduler_->Enqueue(
            std::span<const std::byte>(receiveBuffer.data(), receivedLength),
            address, captureTime);
        if (stopping_.load(std::memory_order_acquire) &&
            !flushOnStop_.load(std::memory_order_acquire)) {
            break;
        }
    }
    running_.store(false, std::memory_order_release);
}

void WinDivertEngine::SetError(std::string message) {
    std::lock_guard lock(errorMutex_);
    lastError_ = std::move(message);
}

void WinDivertEngine::Stop(bool flush) {
    if (handle_ == INVALID_HANDLE_VALUE && !scheduler_) {
        return;
    }
    flushOnStop_.store(flush, std::memory_order_release);
    if (stopping_.exchange(true, std::memory_order_acq_rel)) {
        if (receiverThread_.joinable()) {
            receiverThread_.join();
        }
        return;
    }

    running_.store(false, std::memory_order_release);
    if (handle_ != INVALID_HANDLE_VALUE) {
        WinDivertShutdown(handle_, WINDIVERT_SHUTDOWN_RECV);
    }
    if (receiverThread_.joinable()) {
        receiverThread_.join();
    }

    if (scheduler_) {
        if (flush) {
            scheduler_->StopAndFlush();
        } else {
            scheduler_->StopAndDrop();
        }
        finalMetrics_ = scheduler_->Metrics();
        scheduler_.reset();
    }
    if (handle_ != INVALID_HANDLE_VALUE) {
        WinDivertClose(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }
}

}  // namespace nlc
