#include "voxel_world.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <istream>
#include <limits>
#include <ostream>
#include <string>

namespace wds::voxel {
namespace {

constexpr double radius = 0.27;
constexpr double body_height = 1.75;
constexpr double epsilon = 1e-7;

int cell(double value) { return static_cast<int>(std::floor(value)); }

bool inside(int x, int y, int z) {
  return x >= 0 && x < World::width && z >= 0 && z < World::depth && y >= 0 && y < World::height;
}

std::size_t index(int x, int y, int z) {
  return static_cast<std::size_t>((y * World::depth + z) * World::width + x);
}

Block terrain(int x, int y, int z) {
  if (y == 0) return Block::dirt;
  if (y == 1) return Block::grass;
  if (y <= 4 && (x == 0 || x == World::width - 1 || z == 0 || z == World::depth - 1)) {
    return Block::stone;
  }
  if (z >= 10 && z <= 11 && x >= 10 && x <= 14 && y <= 4) return Block::stone;
  if (x >= 4 && x <= 7 && z >= 5 && z <= 8 && y <= 3) return Block::dirt;
  if (x >= 17 && x <= 20 && z >= 14 && z <= 17 && y <= 2) return Block::grass;
  return Block::air;
}

}  // namespace

World::World() {
  for (int y = 0; y < height; ++y) {
    for (int z = 0; z < depth; ++z) {
      for (int x = 0; x < width; ++x) blocks_[index(x, y, z)] = terrain(x, y, z);
    }
  }
}

Block World::block(int x, int y, int z) const {
  return inside(x, y, z) ? blocks_[index(x, y, z)] : Block::air;
}

bool World::collides(Vec3 feet) const {
  if (!std::isfinite(feet.x) || !std::isfinite(feet.y) || !std::isfinite(feet.z)) return true;
  if (feet.x < radius || feet.x > width - radius || feet.z < radius || feet.z > depth - radius ||
      feet.y < 0) {
    return true;
  }
  if (feet.y >= height) return false;
  for (int x = cell(feet.x - radius); x <= cell(feet.x + radius - epsilon); ++x) {
    for (int y = cell(feet.y); y <= cell(feet.y + body_height - epsilon); ++y) {
      for (int z = cell(feet.z - radius); z <= cell(feet.z + radius - epsilon); ++z) {
        if (block(x, y, z) != Block::air) return true;
      }
    }
  }
  return false;
}

Hit World::cast(Vec3 origin, Vec3 direction, double limit) const {
  const double length = std::hypot(direction.x, direction.y, direction.z);
  if (!(length > 0) || !std::isfinite(length) || !(limit > 0) || !std::isfinite(limit)) return {};
  // The renderer only casts short rays around this fixed-size world.
  limit = std::min(limit, 64.0);
  if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z) ||
      origin.x < -limit || origin.x > width + limit || origin.y < -limit ||
      origin.y > height + limit || origin.z < -limit || origin.z > depth + limit) {
    return {};
  }
  const std::array pos{origin.x, origin.y, origin.z};
  const std::array ray{direction.x / length, direction.y / length, direction.z / length};
  std::array grid{cell(pos[0]), cell(pos[1]), cell(pos[2])};
  std::array<int, 3> steps{};
  std::array<double, 3> next{}, delta{};
  for (int axis = 0; axis < 3; ++axis) {
    if (ray[axis] == 0) {
      next[axis] = delta[axis] = std::numeric_limits<double>::infinity();
    } else {
      steps[axis] = ray[axis] > 0 ? 1 : -1;
      const int edge = grid[axis] + (steps[axis] > 0 ? 1 : 0);
      next[axis] = (edge - pos[axis]) / ray[axis];
      delta[axis] = std::abs(1 / ray[axis]);
    }
  }

  double distance = 0;
  int side = 1;
  int normal = 0;
  while (distance <= limit) {
    const auto hit = block(grid[0], grid[1], grid[2]);
    if (hit != Block::air) return {hit, distance, side, grid[0], grid[1], grid[2], normal};
    side = static_cast<int>(std::min_element(next.begin(), next.end()) - next.begin());
    distance = next[side];
    if (!std::isfinite(distance) || distance > limit) break;
    // At corners enter the cell across all crossed planes.
    int crossings = 0;
    for (int axis = 0; axis < 3; ++axis) {
      if (next[axis] <= distance + epsilon) {
        grid[axis] += steps[axis];
        next[axis] += delta[axis];
        ++crossings;
      }
    }
    normal = crossings == 1 ? -steps[side] : 0;
  }
  return {};
}

bool World::edit(Vec3 feet, Vec3 eye, Vec3 direction, bool place) {
  const auto hit = cast(eye, direction, 7);
  if (hit.block == Block::air) return false;
  std::array target{hit.x, hit.y, hit.z};
  if (place) {
    if (hit.normal == 0) return false;
    target[hit.side] += hit.normal;
  }
  const auto [x, y, z] = target;
  if (!inside(x, y, z) || y == 0) return false;
  auto& value = blocks_[index(x, y, z)];
  if (!place) {
    value = Block::air;
    return true;
  }
  if (value != Block::air) return false;
  value = Block::stone;
  if (collides(feet) || collides(Player{}.feet)) {
    value = Block::air;
    return false;
  }
  return true;
}

bool World::read(std::istream& input) {
  std::string header;
  if (!std::getline(input, header) || header != "wds-voxel-v1 24 12 24") return false;
  World candidate;
  for (auto& block : candidate.blocks_) {
    const int value = input.get();
    if (value < '0' || value > '3') return false;
    block = static_cast<Block>(value - '0');
  }
  if (input.get() != '\n' || input.get() != std::char_traits<char>::eof() || !input.eof() ||
      input.bad() || candidate.collides(Player{}.feet)) {
    return false;
  }
  for (int z = 0; z < depth; ++z) {
    for (int x = 0; x < width; ++x) {
      if (candidate.block(x, 0, z) != Block::dirt) return false;
    }
  }
  *this = candidate;
  return true;
}

void World::write(std::ostream& output) const {
  output << "wds-voxel-v1 24 12 24\n";
  for (const auto block : blocks_) output.put(static_cast<char>('0' + static_cast<int>(block)));
  output.put('\n');
}

void Player::step(const World& world, double forward, double strafe, bool jump, double seconds,
                  int jump_speed) {
  if (!(seconds > 0) || !std::isfinite(seconds) || !std::isfinite(forward) ||
      !std::isfinite(strafe)) {
    return;
  }
  forward = std::clamp(forward, -1.0, 1.0);
  strafe = std::clamp(strafe, -1.0, 1.0);
  seconds = std::min(seconds, 0.025);
  const double length = std::max(1.0, std::hypot(forward, strafe));
  const double speed = 5.0 * seconds / length;
  const double dx = (std::sin(yaw) * forward + std::cos(yaw) * strafe) * speed;
  const double dz = (-std::cos(yaw) * forward + std::sin(yaw) * strafe) * speed;
  if (!world.collides({feet.x + dx, feet.y, feet.z})) feet.x += dx;
  if (!world.collides({feet.x, feet.y, feet.z + dz})) feet.z += dz;

  if (jump && grounded) fall_speed = jump_speed;
  fall_speed = std::max(-12.0, fall_speed - 18 * seconds);
  const double dy = fall_speed * seconds;
  if (!world.collides({feet.x, feet.y + dy, feet.z})) {
    feet.y += dy;
    grounded = false;
  } else {
    // Move to the collision boundary instead of hovering a full step above it.
    double free = 0;
    double blocked = 1;
    for (int i = 0; i < 16; ++i) {
      const auto mid = (free + blocked) / 2;
      if (world.collides({feet.x, feet.y + mid * dy, feet.z})) {
        blocked = mid;
      } else {
        free = mid;
      }
    }
    feet.y += free * dy;
    grounded = dy < 0;
    fall_speed = 0;
  }
}

Vec3 Player::eye() const { return {feet.x, feet.y + 1.55, feet.z}; }

Vec3 Player::view() const {
  return {std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)};
}

std::uint32_t pixel(const World& world, Vec3 origin, Vec3 direction) {
  const auto hit = world.cast(origin, direction, 36);
  if (hit.block == Block::air) return 0x0087bce8U;
  constexpr std::array<std::uint32_t, 4> colors{0, 0x659645U, 0x90643eU, 0x87929fU};
  const auto color = colors[static_cast<std::size_t>(hit.block)];
  const double shade = (hit.side == 1   ? 1.0
                        : hit.side == 0 ? 0.76
                                        : 0.86) *
                       std::max(0.45, 1 - hit.distance * 0.014);
  const auto scale = [shade](std::uint32_t value) {
    return static_cast<std::uint32_t>((value & 255U) * shade);
  };
  return (scale(color >> 16U) << 16U) | (scale(color >> 8U) << 8U) | scale(color);
}

}  // namespace wds::voxel
