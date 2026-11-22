#pragma once

#include <array>
#include <compare>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace wds::platform {

struct TilePoint {
  int x;
  int y;
  auto operator<=>(const TilePoint&) const = default;
};

enum class Tile { grass, soil, rock, canopy };
std::optional<Tile> tile_kind(std::string_view name);
std::uint32_t tile_color(Tile tile);

struct CharacterProfile {
  std::string id;
  int speed;
  int sight;
};

struct EcologyRules {
  static constexpr int min_refresh = 6;
  static constexpr int max_refresh = 300;
  static constexpr int max_sight = 16;
  int refresh_ticks;
  CharacterProfile character;
};

enum class Species { reed, hare, moth };
std::string_view species_name(Species species);
std::uint32_t species_color(Species species);

struct EcologySite {
  TilePoint point;
  Species species;
};

class TileWorld {
 public:
  static constexpr int tile_size = 16;
  static constexpr int chunk_size = 8;
  static constexpr int chunk_radius = 2;
  static constexpr int limit = 4096;
  static constexpr std::size_t max_edits = 4096;
  static constexpr int side = (2 * chunk_radius + 1) * chunk_size;
  using Edits = std::map<TilePoint, Tile>;

  TileWorld(std::uint32_t seed, int day_ticks, int day_start, Edits edits,
            std::optional<EcologyRules> ecology = {});
  static TilePoint cell(std::int64_t x, std::int64_t y);
  static bool inside(TilePoint point);
  void stream(TilePoint center);
  TilePoint chunk() const { return chunk_; }
  TilePoint corner() const { return corner_; }
  int loaded() const { return loaded_; }
  std::uint32_t seed() const { return seed_; }
  int daylight(std::uint64_t ticks) const;
  const std::optional<EcologyRules>& ecology() const { return ecology_; }
  std::vector<EcologySite> populations(TilePoint viewer, std::uint64_t ticks) const;
  bool resident(TilePoint point) const;
  Tile tile(TilePoint point) const;
  bool solid(TilePoint point) const { return tile(point) == Tile::rock; }
  bool visible(TilePoint from, TilePoint to) const;
  std::vector<TilePoint> route(TilePoint from, TilePoint to) const;

 private:
  std::size_t index(TilePoint point) const;
  bool opaque(TilePoint point) const;
  Tile generated(TilePoint point) const;
  std::uint32_t seed_;
  int day_ticks_;
  int day_start_;
  Edits edits_;
  std::optional<EcologyRules> ecology_;
  TilePoint chunk_{limit, limit};
  TilePoint corner_{};
  int loaded_ = 0;
  std::array<Tile, side * side> grid_{};
};

}  // namespace wds::platform
