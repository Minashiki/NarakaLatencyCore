#include "nlc/packet_pool.h"

#include <stdexcept>

namespace nlc {

PacketPool::PacketPool(std::size_t capacity, std::size_t bytesPerPacket)
    : bytesPerPacket_(bytesPerPacket),
      packets_(capacity),
      storage_(capacity * bytesPerPacket),
      freeList_() {
    if (capacity == 0 || bytesPerPacket == 0) {
        throw std::invalid_argument("packet pool capacity and slot size must be non-zero");
    }
    freeList_.reserve(capacity);
    for (std::size_t index = capacity; index > 0; --index) {
        freeList_.push_back(index - 1);
    }
}

std::optional<std::size_t> PacketPool::Acquire() noexcept {
    std::lock_guard lock(mutex_);
    if (freeList_.empty()) {
        return std::nullopt;
    }
    const auto index = freeList_.back();
    freeList_.pop_back();
    return index;
}

void PacketPool::Release(std::size_t index) noexcept {
    std::lock_guard lock(mutex_);
    packets_[index] = {};
    freeList_.push_back(index);
}

DelayedPacket& PacketPool::Packet(std::size_t index) noexcept {
    return packets_[index];
}

const DelayedPacket& PacketPool::Packet(std::size_t index) const noexcept {
    return packets_[index];
}

std::span<std::byte> PacketPool::WritableBytes(std::size_t index) noexcept {
    return {storage_.data() + index * bytesPerPacket_, bytesPerPacket_};
}

std::span<const std::byte> PacketPool::Bytes(std::size_t index) const noexcept {
    const auto length = packets_[index].length;
    return {storage_.data() + index * bytesPerPacket_, length};
}

std::size_t PacketPool::Capacity() const noexcept {
    return packets_.size();
}

std::size_t PacketPool::BytesPerPacket() const noexcept {
    return bytesPerPacket_;
}

std::size_t PacketPool::Available() const noexcept {
    std::lock_guard lock(mutex_);
    return freeList_.size();
}

}  // namespace nlc

