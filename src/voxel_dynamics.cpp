#include <algorithm>
#include <cmath>
#include <utility>

#include "voxel_world.hpp"

namespace wds::voxel {
namespace {

constexpr std::array directions{std::array{1, 0}, std::array{-1, 0}, std::array{0, 1},
                                std::array{0, -1}};

bool in_world(int coordinate) {
  return coordinate >= -SeededTerrain::limit && coordinate < SeededTerrain::limit;
}

struct Fluid {
  Block source;
  Block low;
  Block high;

  int level(Block block) const {
    if (block == source) return 3;
    if (block < low || block > high) return 0;
    return 1 + static_cast<int>(block) - static_cast<int>(low);
  }

  Block at(int strength) const {
    return strength == 0 ? Block::air : static_cast<Block>(static_cast<int>(low) + strength - 1);
  }

  bool accepts(Block block) const {
    return block == Block::air || block == Block::flame || (block >= low && block <= high);
  }
};

constexpr Fluid water{Block::spring, Block::water_low, Block::water_high};
constexpr Fluid lava{Block::vent, Block::lava_low, Block::lava_high};

struct Region {
  int width;
  int depth;

  std::size_t index(int x, int y, int z) const {
    return static_cast<std::size_t>((y * depth + z) * width + x);
  }

  bool contains(int x, int z) const { return x >= 0 && x < width && z >= 0 && z < depth; }

  template <typename Operation>
  void each(Operation operation) const {
    for (int y = 1; y < World::height; ++y)
      for (int z = 0; z < depth; ++z)
        for (int x = 0; x < width; ++x) operation(x, y, z);
  }
};

std::vector<int> flow(const Region& region, const std::vector<Block>& cells, Fluid fluid) {
  std::vector<int> supply(cells.size());
  const auto pour = [&](int x, int y, int z, int power) {
    if (!region.contains(x, z)) return;
    const auto index = region.index(x, y, z);
    if (fluid.accepts(cells[index])) supply[index] = std::max(supply[index], power);
  };
  region.each([&](int x, int y, int z) {
    const auto level = fluid.level(cells[region.index(x, y, z)]);
    if (level == 0) return;
    if (y > 1 && fluid.accepts(cells[region.index(x, y - 1, z)])) {
      pour(x, y - 1, z, level);
    } else if (level > 1) {
      for (const auto& [dx, dz] : directions) pour(x + dx, y, z + dz, level - 1);
    }
  });
  return supply;
}

bool touches_water(const Region& region, const std::vector<Block>& cells, int x, int y, int z) {
  const auto wet = [&](int u, int v, int w) {
    return region.contains(u, w) && v >= 1 && v < World::height &&
           water.level(cells[region.index(u, v, w)]) > 0;
  };
  return wet(x, y - 1, z) || wet(x, y + 1, z) ||
         std::ranges::any_of(
             directions, [&](const auto& offset) { return wet(x + offset[0], y, z + offset[1]); });
}

void evolve(const Region& region, std::vector<Block>& cells, bool thermal, DynamicsStep& result) {
  const auto wet = flow(region, cells, water);
  const auto hot = thermal ? flow(region, cells, lava) : std::vector<int>(cells.size());
  auto next = cells;
  region.each([&](int x, int y, int z) {
    const auto index = region.index(x, y, z);
    const auto old = cells[index];
    if ((wet[index] > 0 && hot[index] > 0) ||
        (lava.level(old) > 0 && touches_water(region, cells, x, y, z))) {
      next[index] = Block::basalt;
      ++result.solidified;
      if (old == Block::flame) ++result.extinguished;
    } else if (wet[index] > 0) {
      next[index] = water.at(wet[index]);
      if (old == Block::flame) ++result.extinguished;
    } else if (hot[index] > 0) {
      next[index] = lava.at(hot[index]);
    } else if (old == Block::flame) {
      next[index] = Block::air;
      for (const auto& [dx, dz] : directions) {
        if (!region.contains(x + dx, z + dz)) continue;
        const auto neighbor = region.index(x + dx, y, z + dz);
        if (cells[neighbor] != Block::timber || next[neighbor] == Block::flame) continue;
        next[neighbor] = Block::flame;
        ++result.ignited;
      }
    } else if (old != Block::spring && water.level(old) > 0) {
      next[index] = water.at(water.level(old) - 1);
    } else if (old != Block::vent && lava.level(old) > 0) {
      next[index] = lava.at(lava.level(old) - 1);
    }
    if (next[index] != old) {
      if (water.level(next[index]) || water.level(old)) ++result.water_updates;
      if (lava.level(next[index]) || lava.level(old)) ++result.lava_updates;
    }
  });
  cells.swap(next);
}

}  // namespace

bool World::cart_track(Cell position) const {
  const auto [x, y, z] = position;
  if (!terrain_ || !terrain_->workyard() || !in_world(x) || !in_world(z) || y < 1 ||
      y >= height - 1)
    return false;
  stream({static_cast<double>(x), 0, static_cast<double>(z)});
  return block(x, y, z) == Block::rail && solid(block(x, y - 1, z)) &&
         block(x, y + 1, z) == Block::air;
}

CartRoute World::cart_route(Cell start, int dx, int dz, int steps) const {
  if (!terrain_ || !terrain_->workyard()) return {"unsupported_terrain", {}, false};
  if (std::ranges::find(directions, std::array{dx, dz}) == directions.end() || steps < 1 ||
      steps > 64)
    return {"invalid_argument", {}, false};
  if (!cart_track(start)) return {"invalid_track", {}, false};

  CartRoute route{"ok", {start}};
  auto [x, y, z] = start;
  for (int step = 0; step < steps; ++step) {
    x += dx;
    z += dz;
    if (!cart_track({x, y, z})) {
      route.blocked = true;
      break;
    }
    route.positions.push_back({x, y, z});
  }
  return route;
}

DynamicsStep World::step_dynamics(int x, int z, int columns, int rows, int steps) {
  if (!terrain_ || !terrain_->workyard()) return {"unsupported_terrain"};
  if (!in_world(x) || !in_world(z) || columns < 1 || columns > 24 || rows < 1 || rows > 24 ||
      !in_world(x + columns - 1) || !in_world(z + rows - 1) || steps < 1 || steps > 32)
    return {"invalid_argument"};

  auto candidate = *terrain_;
  candidate.stream(x + columns / 2, z + rows / 2);
  const Region region{columns, rows};
  std::vector<Block> cells(static_cast<std::size_t>(columns * rows * height));
  region.each([&](int u, int y, int v) {
    cells[region.index(u, y, v)] = candidate.block(x + u, y, z + v);
  });
  const auto before = cells;
  DynamicsStep result{"ok"};
  for (int step = 0; step < steps; ++step)
    evolve(region, cells, candidate.thermal_workyard(), result);
  if (before == cells) return result;

  // Reclaim edit slots before adding records whose generated block is different.
  bool saved = true;
  for (const bool resets : {true, false}) {
    region.each([&](int u, int y, int v) {
      const auto index = region.index(u, y, v);
      if (saved && before[index] != cells[index] &&
          resets == candidate.matches_generated(x + u, y, z + v, cells[index]))
        saved = candidate.set(x + u, y, z + v, cells[index]);
    });
  }
  if (!saved) return {"edit_limit"};
  terrain_ = std::move(candidate);
  stream(Player{}.feet);
  result.changed = true;
  return result;
}

bool Player::toggle_cart(const World& world) {
  if (cart) {
    cart.reset();
    return true;
  }
  if (!world.terrain() || !world.terrain()->workyard() || !grounded) return false;
  const int x = static_cast<int>(std::floor(feet.x));
  const int y = static_cast<int>(std::floor(feet.y));
  const int z = static_cast<int>(std::floor(feet.z));
  cart = Cell{x, y, z};
  if (move_cart(world, 0, 0)) return true;
  for (const auto& [dx, dz] : directions)
    if (move_cart(world, dx, dz)) return true;
  cart.reset();
  world.stream(feet);
  return false;
}

bool Player::move_cart(const World& world, int dx, int dz) {
  if (!cart || (std::array{dx, dz} != std::array{0, 0} &&
                std::ranges::find(directions, std::array{dx, dz}) == directions.end()))
    return false;
  const auto [x, y, z] = *cart;
  const Cell target{x + dx, y, z + dz};
  if (!world.cart_track(target)) return false;
  cart = target;
  feet = {target[0] + 0.5, static_cast<double>(y), target[2] + 0.5};
  fall_speed = 0;
  grounded = true;
  return true;
}

DynamicsStep Player::advance_workyard(World& world) const {
  const auto x = static_cast<int>(std::floor(feet.x)) - 8;
  const auto z = static_cast<int>(std::floor(feet.z)) - 8;
  const auto change = world.step_dynamics(x, z, 17, 17, 1);
  world.stream(feet);
  return change;
}

}  // namespace wds::voxel
