#pragma once

#include <array>
#include <cstdint>
#include <iosfwd>
#include <map>
#include <string>
#include <vector>

#include "voxel_content.hpp"

namespace wds::voxel {

enum class TerrainProfile { seeded, frontier, workyard, thermal_workyard };

class SeededTerrain {
 public:
  static constexpr int limit = 32768;
  static constexpr int height = 12;
  static constexpr int chunk_size = 16;
  static constexpr int radius = 2;
  static constexpr std::size_t max_edits = 8192;

  explicit SeededTerrain(std::uint32_t seed, bool frontier = false)
      : SeededTerrain(seed, frontier ? TerrainProfile::frontier : TerrainProfile::seeded) {}
  SeededTerrain(std::uint32_t seed, TerrainProfile profile);
  std::uint32_t seed() const { return seed_; }
  bool frontier() const { return profile_ != TerrainProfile::seeded; }
  bool workyard() const { return profile_ == TerrainProfile::workyard || thermal_workyard(); }
  bool thermal_workyard() const { return profile_ == TerrainProfile::thermal_workyard; }
  std::string_view profile_name() const;
  std::string_view biome(int x, int z) const;
  std::string_view landmark(int x, int z) const;
  std::size_t loaded() const { return loaded_; }
  bool resident(int x, int z) const;
  Block block(int x, int y, int z) const;
  bool matches_generated(int x, int y, int z, Block block) const;
  bool set(int x, int y, int z, Block block);
  void stream(int x, int z) const;
  bool read(std::istream& input);
  void write(std::ostream& output) const;

 private:
  struct Chunk {
    int x = limit;
    int z = limit;
    std::array<Block, chunk_size * chunk_size * height> cells{};
  };
  using Position = std::array<int, 3>;
  using Edits = std::map<Position, Block>;
  static constexpr int diameter = 2 * radius + 1;

  using Column = std::array<Block, height>;
  Column generated(int x, int z) const;
  std::string save_header() const;
  int ground(int x, int z) const;
  static std::size_t slot(int chunk_x, int chunk_z);
  static std::size_t index(int x, int y, int z);

  std::uint32_t seed_;
  TerrainProfile profile_;
  mutable int center_x_ = limit;
  mutable int center_z_ = limit;
  mutable std::size_t loaded_ = 0;
  mutable std::vector<Chunk> chunks_;
  Edits edits_;
};

}  // namespace wds::voxel
