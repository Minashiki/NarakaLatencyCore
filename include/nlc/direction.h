#pragma once

#include <cstddef>

namespace nlc {

enum class Direction : unsigned char { Inbound = 0, Outbound = 1 };

constexpr std::size_t DirectionIndex(Direction direction) noexcept {
    return direction == Direction::Outbound ? 1u : 0u;
}

constexpr const char* DirectionName(Direction direction) noexcept {
    return direction == Direction::Outbound ? "outbound" : "inbound";
}

}  // namespace nlc

