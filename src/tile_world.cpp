#include "tile_world.hpp"

#include <algorithm>
#include <cstdlib>
#include <utility>

#include "seeded_grid.hpp"

namespace wds::platform {

std::optional<Tile> tile_kind(std::string_view name) {
  if (name == "grass") return Tile::grass;
  if (name == "soil") return Tile::soil;
  if (name == "rock") return Tile::rock;
  if (name == "canopy") return Tile::canopy;
  return {};
}

std::uint32_t tile_color(Tile tile) {
  switch (tile) {
    case Tile::grass:
      return 0x749f69;
    case Tile::soil:
      return 0xb09378;
    case Tile::rock:
      return 0x606981;
    case Tile::canopy:
      return 0x315946;
  }
  return 0x000000;
}

TileWorld::TileWorld(std::uint32_t seed, int day_ticks, int day_start, Edits edits)
    : seed_(seed), day_ticks_(day_ticks), day_start_(day_start), edits_(std::move(edits)) {}

TilePoint TileWorld::cell(std::int64_t x, std::int64_t y) {
  constexpr std::int64_t edge = limit * tile_size;
  return {floor_div(static_cast<int>(std::clamp(x, -edge - 1, edge)), tile_size),
          floor_div(static_cast<int>(std::clamp(y, -edge - 1, edge)), tile_size)};
}

bool TileWorld::inside(TilePoint point) {
  return point.x >= -limit && point.y >= -limit && point.x < limit && point.y < limit;
}

std::size_t TileWorld::index(TilePoint point) const {
  return static_cast<std::size_t>((point.y - corner_.y) * side + point.x - corner_.x);
}

Tile TileWorld::generated(TilePoint point) const {
  if (const auto found = edits_.find(point); found != edits_.end()) return found->second;
  const auto hash = coordinate_hash(seed_, floor_div(point.x, 2), floor_div(point.y, 2));
  return hash % 5U == 0 ? Tile::soil : Tile::grass;
}

void TileWorld::stream(TilePoint center) {
  const TilePoint chunk{floor_div(center.x, chunk_size), floor_div(center.y, chunk_size)};
  if (chunk == chunk_) return;
  chunk_ = chunk;
  corner_ = {(chunk.x - chunk_radius) * chunk_size, (chunk.y - chunk_radius) * chunk_size};
  loaded_ = 0;
  for (int y = corner_.y; y < corner_.y + side; ++y) {
    for (int x = corner_.x; x < corner_.x + side; ++x) {
      const TilePoint point{x, y};
      if (inside(point) && floor_mod(x, chunk_size) == 0 && floor_mod(y, chunk_size) == 0)
        ++loaded_;
      grid_[index(point)] = inside(point) ? generated(point) : Tile::rock;
    }
  }
}

int TileWorld::daylight(std::uint64_t ticks) const {
  const auto phase = (day_start_ + static_cast<int>(ticks % day_ticks_)) % day_ticks_;
  const auto rise = std::min(phase, day_ticks_ - phase);
  return 72 + 183 * (2 * rise) / day_ticks_;
}

bool TileWorld::resident(TilePoint point) const {
  return loaded_ > 0 && inside(point) && point.x >= corner_.x && point.x < corner_.x + side &&
         point.y >= corner_.y && point.y < corner_.y + side;
}

Tile TileWorld::tile(TilePoint point) const {
  return resident(point) ? grid_[index(point)] : Tile::rock;
}

bool TileWorld::opaque(TilePoint point) const {
  const auto kind = tile(point);
  return kind == Tile::rock || kind == Tile::canopy;
}

bool TileWorld::visible(TilePoint from, TilePoint to) const {
  if (!resident(from) || !resident(to) || opaque(from)) return false;
  const int dx = std::abs(to.x - from.x);
  const int dy = -std::abs(to.y - from.y);
  const int sx = from.x < to.x ? 1 : -1;
  const int sy = from.y < to.y ? 1 : -1;
  int error = dx + dy;
  while (from != to) {
    const int twice = 2 * error;
    if (twice >= dy && twice <= dx && opaque({from.x + sx, from.y}) &&
        opaque({from.x, from.y + sy}))
      return false;
    if (twice >= dy) {
      error += dy;
      from.x += sx;
    }
    if (twice <= dx) {
      error += dx;
      from.y += sy;
    }
    if (opaque(from)) return false;
  }
  return true;
}

std::vector<TilePoint> TileWorld::route(TilePoint from, TilePoint to) const {
  if (solid(from) || solid(to)) return {};
  constexpr std::array offsets{TilePoint{1, 0}, TilePoint{0, 1}, TilePoint{-1, 0},
                               TilePoint{0, -1}};
  std::array<std::optional<TilePoint>, side * side> parent{};
  std::vector<TilePoint> queue{from};
  parent[index(from)] = from;
  for (std::size_t i = 0; i < queue.size(); ++i) {
    const auto current = queue[i];
    if (current == to) {
      std::vector<TilePoint> path{to};
      while (path.back() != from) path.push_back(*parent[index(path.back())]);
      std::ranges::reverse(path);
      return path;
    }
    for (const auto direction : offsets) {
      const TilePoint next{current.x + direction.x, current.y + direction.y};
      if (solid(next) || parent[index(next)]) continue;
      parent[index(next)] = current;
      queue.push_back(next);
    }
  }
  return {};
}

}  // namespace wds::platform
