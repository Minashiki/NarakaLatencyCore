#pragma once

#include "nlc/direction.h"

#include <windivert.h>

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <vector>

namespace nlc {

struct DelayedPacket {
    WINDIVERT_ADDRESS address{};
    std::uint32_t length{0};
    std::uint64_t sequence{0};
    std::int64_t captureTime{0};
    std::int64_t dueTime{0};
    Direction direction{Direction::Inbound};
};

class PacketPool final {
public:
    PacketPool(std::size_t capacity, std::size_t bytesPerPacket);

    PacketPool(const PacketPool&) = delete;
    PacketPool& operator=(const PacketPool&) = delete;

    [[nodiscard]] std::optional<std::size_t> Acquire() noexcept;
    void Release(std::size_t index) noexcept;

    [[nodiscard]] DelayedPacket& Packet(std::size_t index) noexcept;
    [[nodiscard]] const DelayedPacket& Packet(std::size_t index) const noexcept;
    [[nodiscard]] std::span<std::byte> WritableBytes(std::size_t index) noexcept;
    [[nodiscard]] std::span<const std::byte> Bytes(std::size_t index) const noexcept;
    [[nodiscard]] std::size_t Capacity() const noexcept;
    [[nodiscard]] std::size_t BytesPerPacket() const noexcept;
    [[nodiscard]] std::size_t Available() const noexcept;

private:
    std::size_t bytesPerPacket_;
    std::vector<DelayedPacket> packets_;
    std::vector<std::byte> storage_;
    mutable std::mutex mutex_;
    std::vector<std::size_t> freeList_;
};

}  // namespace nlc

