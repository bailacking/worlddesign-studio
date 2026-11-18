#pragma once

#include <cstdint>

namespace wds {

constexpr int floor_div(int value, int divisor) {
  const auto quotient = value / divisor;
  return value % divisor < 0 ? quotient - 1 : quotient;
}

constexpr int floor_mod(int value, int divisor) {
  const auto remainder = value % divisor;
  return remainder < 0 ? remainder + divisor : remainder;
}

constexpr std::uint32_t coordinate_hash(std::uint32_t seed, int x, int y) {
  auto value = seed ^ (static_cast<std::uint32_t>(x) * 0x9e3779b9U) ^
               (static_cast<std::uint32_t>(y) * 0x85ebca6bU);
  value = (value ^ (value >> 16U)) * 0x7feb352dU;
  value = (value ^ (value >> 15U)) * 0x846ca68bU;
  return value ^ (value >> 16U);
}

}  // namespace wds
