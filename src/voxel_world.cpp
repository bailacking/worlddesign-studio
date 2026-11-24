#include "voxel_world.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <istream>
#include <limits>
#include <ostream>
#include <string>

#include "seeded_grid.hpp"

namespace wds::voxel {
namespace {

constexpr double radius = 0.27;
constexpr double body_height = 1.75;
constexpr double epsilon = 1e-7;
constexpr std::array adjacent{Cell{1, 0, 0},  Cell{-1, 0, 0}, Cell{0, 1, 0},
                              Cell{0, -1, 0}, Cell{0, 0, 1},  Cell{0, 0, -1}};
constexpr int max_light = 15;

int cell(double value) { return static_cast<int>(std::floor(value)); }

bool inside(int x, int y, int z) {
  return x >= 0 && x < World::width && z >= 0 && z < World::depth && y >= 0 && y < World::height;
}

std::size_t index(int x, int y, int z) {
  return static_cast<std::size_t>((y * World::depth + z) * World::width + x);
}

Block legacy_block(int x, int y, int z) {
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

double rendered_height(Block block) {
  switch (block) {
    case Block::rail:
      return 0.11;
    case Block::water_low:
    case Block::lava_low:
      return 0.25;
    case Block::water_mid:
    case Block::lava_mid:
      return 0.5;
    case Block::water_high:
    case Block::lava_high:
      return 0.75;
    default:
      return 1;
  }
}

bool exposed_to_sky(const World& world, int x, int y, int z) {
  for (; y < World::height; ++y)
    if (solid(world.block(x, y, z))) return false;
  return true;
}

int emitted_light(Block block) {
  switch (block) {
    case Block::vent:
      return 13;
    case Block::lava_high:
      return 12;
    case Block::flame:
    case Block::lava_mid:
      return 11;
    case Block::lava_low:
      return 9;
    default:
      return 0;
  }
}

struct SkyLight {
  std::string_view weather;
  int level;
};

SkyLight sky_light(std::uint32_t seed, std::uint32_t tick) {
  constexpr std::array weather_names{"clear", "rain", "storm"};
  constexpr std::array attenuation{0, 2, 4};
  const auto roll = coordinate_hash(seed, static_cast<int>(tick / 120), 0) % 10U;
  const std::size_t weather = roll < 6U ? 0 : roll < 9U ? 1 : 2;
  const int time = static_cast<int>(tick % 240);
  const int daytime = std::clamp(std::min(time, 180 - time) / 4, 0, max_light - 1) + 1;
  return {weather_names[weather], std::max(1, daytime - attenuation[weather])};
}

int chunk_coordinate(double coordinate) {
  constexpr double edge = SeededTerrain::limit;
  return floor_div(cell(std::clamp(coordinate, -edge, edge - 1)), SeededTerrain::chunk_size);
}

int propagated_light(const World& world, Cell origin, int daylight, bool open_sky) {
  const auto [x, y, z] = origin;
  if (solid(world.block(x, y, z))) return 0;
  constexpr int range = max_light - 1;
  constexpr int width = range * 2 + 1;
  const auto index = [](int column_offset, int row, int depth_offset) {
    return static_cast<std::size_t>((row * width + depth_offset + range) * width + column_offset +
                                    range);
  };
  struct Probe {
    Cell cell;
    int distance;
  };
  std::array<bool, width * width * World::height> visited{};
  visited[index(0, y, 0)] = true;
  std::vector<Probe> pending{{origin, 0}};
  int light = open_sky ? daylight : 0;
  const int brightest = std::max(daylight, emitted_light(Block::vent));
  // Follow open cells outward from the sample. Each face crossed costs one level.
  for (std::size_t next = 0; next < pending.size(); ++next) {
    const auto [position, distance] = pending[next];
    if (brightest - distance <= light) break;
    const auto [u, v, w] = position;
    light = std::max(light, emitted_light(world.block(u, v, w)) - distance);
    if (daylight - distance > light && exposed_to_sky(world, u, v, w)) light = daylight - distance;
    if (brightest - distance - 1 <= light) continue;
    for (const auto& [du, dv, dw] : adjacent) {
      const int column = u + du, row = v + dv, depth = w + dw;
      if (column < -SeededTerrain::limit || column >= SeededTerrain::limit ||
          depth < -SeededTerrain::limit || depth >= SeededTerrain::limit || row < 0 ||
          row >= World::height)
        continue;
      auto& seen = visited[index(column - x, row, depth - z)];
      if (seen) continue;
      seen = true;
      if (!solid(world.block(column, row, depth)))
        pending.push_back({{column, row, depth}, distance + 1});
    }
  }
  return light;
}

}  // namespace

World::World(Content content) : content_(content) {
  for (int y = 0; y < height; ++y) {
    for (int z = 0; z < depth; ++z) {
      for (int x = 0; x < width; ++x) blocks_[index(x, y, z)] = legacy_block(x, y, z);
    }
  }
}

Block World::block(int x, int y, int z) const {
  if (terrain_) return terrain_->block(x, y, z);
  return inside(x, y, z) ? blocks_[index(x, y, z)] : Block::air;
}

void World::use_seed(std::uint32_t seed, bool frontier) {
  use_seed(seed, frontier ? TerrainProfile::frontier : TerrainProfile::seeded);
}

void World::use_seed(std::uint32_t seed, TerrainProfile profile) {
  terrain_.emplace(seed, profile);
  stream(Player{}.feet);
}

void World::stream(Vec3 center) const {
  if (!terrain_ || !std::isfinite(center.x) || !std::isfinite(center.z)) return;
  constexpr double edge = SeededTerrain::limit;
  terrain_->stream(cell(std::clamp(center.x, -edge, edge - 1)),
                   cell(std::clamp(center.z, -edge, edge - 1)));
}

void World::set_placement(Block block) {
  if (block == Block::grass || block == Block::dirt || block == Block::stone) {
    content_.placement = block;
  }
}

bool World::collides(Vec3 feet) const {
  if (!std::isfinite(feet.x) || !std::isfinite(feet.y) || !std::isfinite(feet.z)) return true;
  const int minimum = terrain_ ? -SeededTerrain::limit : 0;
  const int maximum_x = terrain_ ? SeededTerrain::limit : width;
  const int maximum_z = terrain_ ? SeededTerrain::limit : depth;
  if (feet.x < minimum + radius || feet.x > maximum_x - radius || feet.z < minimum + radius ||
      feet.z > maximum_z - radius || feet.y < 0) {
    return true;
  }
  if (feet.y >= height) return false;
  for (int x = cell(feet.x - radius); x <= cell(feet.x + radius - epsilon); ++x) {
    for (int y = cell(feet.y); y <= cell(feet.y + body_height - epsilon); ++y) {
      for (int z = cell(feet.z - radius); z <= cell(feet.z + radius - epsilon); ++z) {
        if ((terrain_ && !terrain_->resident(x, z)) || solid(block(x, y, z))) return true;
      }
    }
  }
  return false;
}

Hit World::cast(Vec3 origin, Vec3 direction, double limit) const {
  const double length = std::hypot(direction.x, direction.y, direction.z);
  if (!(length > 0) || !std::isfinite(length) || !(limit > 0) || !std::isfinite(limit)) return {};
  limit = std::min(limit, 64.0);
  const int minimum = terrain_ ? -SeededTerrain::limit : 0;
  const int maximum_x = terrain_ ? SeededTerrain::limit : width;
  const int maximum_z = terrain_ ? SeededTerrain::limit : depth;
  if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z) ||
      origin.x < minimum - limit || origin.x > maximum_x + limit || origin.y < -limit ||
      origin.y > height + limit || origin.z < minimum - limit || origin.z > maximum_z + limit) {
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
    if (hit != Block::air) {
      const auto surface = rendered_height(hit);
      const auto top = grid[1] + surface;
      if (surface == 1 || pos[1] + ray[1] * distance <= top)
        return {hit, distance, side, grid[0], grid[1], grid[2], normal};
      if (ray[1] < 0) {
        const auto crossing = (top - pos[1]) / ray[1];
        if (crossing <= limit && crossing < std::min({next[0], next[1], next[2]}))
          return {hit, crossing, 1, grid[0], grid[1], grid[2], 1};
      }
    }
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
  const int x = target[0];
  const int y = target[1];
  const int z = target[2];
  if (y <= 0 || y >= height || (!terrain_ && !inside(x, y, z))) return false;
  const auto set = [this, x, y, z](Block value) {
    if (terrain_) return terrain_->set(x, y, z, value);
    blocks_[index(x, y, z)] = value;
    return true;
  };
  const auto previous = block(x, y, z);
  if (!place) return content_.style(previous).breakable && set(Block::air);
  if (previous != Block::air || !set(content_.placement)) return false;
  const auto spawn = Player{}.feet;
  const bool spawn_loaded = !terrain_ || terrain_->resident(cell(spawn.x), cell(spawn.z));
  if (collides(feet) || (spawn_loaded && collides(spawn))) {
    set(previous);
    return false;
  }
  return true;
}

BlockShift World::shift_blocks(int x, int y, int z, int dx, int dz, int capacity) {
  if (!((dx == 0 && (dz == -1 || dz == 1)) || (dz == 0 && (dx == -1 || dx == 1))))
    return {"invalid_direction"};
  if (capacity < 1 || capacity > push_capacity) return {"invalid_argument"};
  const auto valid = [&](int column, int row) {
    if (y <= 0 || y >= height) return false;
    if (!terrain_) return inside(column, y, row);
    return column >= -SeededTerrain::limit && column < SeededTerrain::limit &&
           row >= -SeededTerrain::limit && row < SeededTerrain::limit;
  };
  if (!valid(x, z) || !valid(x + dx, z + dz)) return {"outside_world"};
  auto candidate = *this;
  candidate.stream({static_cast<double>(x), 0, static_cast<double>(z)});
  const auto source = candidate.block(x, y, z);
  if (source == Block::air || !content_.style(source).breakable) return {"immovable"};
  std::array<Block, push_capacity + 1> replacement{};
  replacement[1] = source;
  int moved = 1;
  for (; moved <= capacity; ++moved) {
    const int at_x = x + dx * moved, at_z = z + dz * moved;
    if (!valid(at_x, at_z)) return {"outside_world"};
    const auto next = candidate.block(at_x, y, at_z);
    if (next == Block::air) break;
    if (moved == capacity) return {capacity == 1 ? "target_occupied" : "push_limit"};
    if (!content_.style(next).breakable) return {"immovable"};
    replacement[static_cast<std::size_t>(moved + 1)] = next;
  }
  if (candidate.terrain_) {
    // Make room for new edits by clearing overrides that return to their generated state.
    for (const bool resets : {true, false}) {
      for (int offset = 0; offset <= moved; ++offset) {
        const int at_x = x + dx * offset, at_z = z + dz * offset;
        const auto next = replacement[static_cast<std::size_t>(offset)];
        if (resets == candidate.terrain_->matches_generated(at_x, y, at_z, next) &&
            !candidate.terrain_->set(at_x, y, at_z, next))
          return {"edit_limit"};
      }
    }
  } else {
    for (int offset = 0; offset <= moved; ++offset)
      candidate.blocks_[index(x + dx * offset, y, z + dz * offset)] =
          replacement[static_cast<std::size_t>(offset)];
  }
  const auto spawn = Player{}.feet;
  const bool spawn_loaded =
      !candidate.terrain_ || candidate.terrain_->resident(cell(spawn.x), cell(spawn.z));
  if (spawn_loaded && candidate.collides(spawn)) return {"spawn_blocked"};
  *this = std::move(candidate);
  return {"ok", moved};
}

std::optional<EnvironmentSample> World::environment(int x, int y, int z, std::uint32_t tick) const {
  if (!terrain_ || x < -SeededTerrain::limit || x >= SeededTerrain::limit ||
      z < -SeededTerrain::limit || z >= SeededTerrain::limit || y < 0 || y >= height)
    return {};
  stream({static_cast<double>(x), 0, static_cast<double>(z)});
  const bool open_sky = exposed_to_sky(*this, x, y, z);
  const auto sky = sky_light(terrain_->seed(), tick);
  return EnvironmentSample{sky.weather, open_sky, open_sky ? sky.level : 0,
                           propagated_light(*this, {x, y, z}, sky.level, open_sky)};
}

std::size_t LightField::index(int x, int y, int z) {
  return static_cast<std::size_t>((y * span + z) * span + x);
}

bool LightField::covers(Vec3 center) const {
  return !cells_.empty() && std::isfinite(center.x) && std::isfinite(center.z) &&
         left_ ==
             (chunk_coordinate(center.x) - SeededTerrain::radius) * SeededTerrain::chunk_size &&
         front_ == (chunk_coordinate(center.z) - SeededTerrain::radius) * SeededTerrain::chunk_size;
}

int LightField::at(Cell cell) const {
  const auto [x, y, z] = cell;
  if (y >= World::height) return daylight_;
  if (cells_.empty() || x < left_ || x >= left_ + span || z < front_ || z >= front_ + span || y < 0)
    return 0;
  return std::max(0, static_cast<int>(cells_[index(x - left_, y, z - front_)]) - 1);
}

LightField World::light_field(Vec3 center, std::uint32_t tick) const {
  LightField field;
  if (!terrain_ || !std::isfinite(center.x) || !std::isfinite(center.z)) return field;
  stream(center);
  constexpr int span = LightField::span;
  field.left_ = (chunk_coordinate(center.x) - SeededTerrain::radius) * SeededTerrain::chunk_size;
  field.front_ = (chunk_coordinate(center.z) - SeededTerrain::radius) * SeededTerrain::chunk_size;
  field.daylight_ = sky_light(terrain_->seed(), tick).level;
  field.cells_.resize(span * span * height);
  std::array<std::vector<Cell>, max_light + 1> frontiers;
  for (int z = 0; z < span; ++z)
    for (int x = 0; x < span; ++x) {
      const int column = field.left_ + x, row = field.front_ + z;
      if (!terrain_->resident(column, row)) continue;
      bool open_sky = true;
      for (int y = height - 1; y >= 0; --y) {
        const auto material = block(column, y, row);
        if (solid(material)) {
          open_sky = false;
          continue;
        }
        // Zero marks a blocked cell. A passable cell stores its light level plus one.
        const int level = std::max(open_sky ? field.daylight_ : 0, emitted_light(material));
        field.cells_[LightField::index(x, y, z)] = static_cast<std::uint8_t>(level + 1);
        if (level > 1) frontiers[level].push_back({x, y, z});
      }
    }
  for (int level = max_light; level > 1; --level)
    for (const auto& [x, y, z] : frontiers[level]) {
      if (field.cells_[LightField::index(x, y, z)] != level + 1) continue;
      for (const auto& [dx, dy, dz] : adjacent) {
        const int u = x + dx, v = y + dy, w = z + dz;
        if (u < 0 || u >= span || v < 0 || v >= height || w < 0 || w >= span) continue;
        auto& neighbor = field.cells_[LightField::index(u, v, w)];
        if (neighbor == 0 || neighbor >= level) continue;
        neighbor = static_cast<std::uint8_t>(level);
        if (level > 2) frontiers[level - 1].push_back({u, v, w});
      }
    }
  return field;
}

bool World::read(std::istream& input) {
  if (terrain_) {
    auto candidate = *this;
    if (!candidate.terrain_->read(input)) return false;
    candidate.stream(Player{}.feet);
    if (candidate.collides(Player{}.feet)) return false;
    *this = std::move(candidate);
    return true;
  }
  std::string header;
  if (!std::getline(input, header) || header != "wds-voxel-v1 24 12 24") return false;
  World candidate(content_);
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
  if (terrain_) {
    terrain_->write(output);
    return;
  }
  output << "wds-voxel-v1 24 12 24\n";
  for (const auto block : blocks_) output.put(static_cast<char>('0' + static_cast<int>(block)));
  output.put('\n');
}

void Player::step(const World& world, double forward, double strafe, bool jump, double seconds,
                  int jump_speed) {
  if (cart && world.cart_track(*cart)) return;
  cart.reset();
  if (!(seconds > 0) || !std::isfinite(seconds) || !std::isfinite(forward) ||
      !std::isfinite(strafe)) {
    return;
  }
  forward = std::clamp(forward, -1.0, 1.0);
  strafe = std::clamp(strafe, -1.0, 1.0);
  seconds = std::min(seconds, 0.025);
  const double length = std::max(1.0, std::hypot(forward, strafe));
  const double speed = 5.0 * seconds / length;
  world.stream(feet);
  const double dx = (std::sin(yaw) * forward + std::cos(yaw) * strafe) * speed;
  const double dz = (-std::cos(yaw) * forward + std::sin(yaw) * strafe) * speed;
  if (!world.collides({feet.x + dx, feet.y, feet.z})) feet.x += dx;
  if (!world.collides({feet.x, feet.y, feet.z + dz})) feet.z += dz;
  world.stream(feet);

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

Camera Player::camera() const {
  return {eye(),
          view(),
          {std::cos(yaw), 0, std::sin(yaw)},
          {-std::sin(yaw) * std::sin(pitch), std::cos(pitch), std::cos(yaw) * std::sin(pitch)}};
}

Vec3 Camera::ray(double horizontal, double vertical) const {
  return {forward.x + horizontal * right.x + vertical * up.x,
          forward.y + horizontal * right.y + vertical * up.y,
          forward.z + horizontal * right.z + vertical * up.z};
}

std::uint32_t pixel(const World& world, Vec3 origin, Vec3 direction, const LightField* light) {
  const auto hit = world.cast(origin, direction, 36);
  const bool sky = hit.block == Block::air;
  const auto color = sky ? world.content().sky : world.content().style(hit.block).color;
  const double side = hit.side == 1 ? 1.0 : hit.side == 0 ? 0.76 : 0.86;
  double shade = sky ? 1.0 : side * std::max(0.45, 1 - hit.distance * 0.014);
  if (light) {
    Cell sample{hit.x, sky ? World::height : hit.y, hit.z};
    if (solid(hit.block) && hit.normal) sample[hit.side] += hit.normal;
    const int level = std::max(light->at(sample), emitted_light(hit.block));
    shade *= 0.22 + 0.78 * level / max_light;
  }
  const auto scale = [shade](std::uint32_t value) {
    return static_cast<std::uint32_t>((value & 255U) * shade);
  };
  return (scale(color >> 16U) << 16U) | (scale(color >> 8U) << 8U) | scale(color);
}

}  // namespace wds::voxel
