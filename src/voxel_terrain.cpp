#include "voxel_terrain.hpp"

#include <algorithm>
#include <cstdlib>
#include <istream>
#include <ostream>
#include <sstream>
#include <string>

#include "seeded_grid.hpp"

namespace wds::voxel {
namespace {

bool inside(int coordinate) {
  return coordinate >= -SeededTerrain::limit && coordinate < SeededTerrain::limit;
}

int blend(std::uint32_t seed, int x, int z, int spacing, int levels) {
  const auto vx = floor_div(x, spacing);
  const auto vz = floor_div(z, spacing);
  const auto u = floor_mod(x, spacing);
  const auto v = floor_mod(z, spacing);
  const auto sample = [&](int i, int j) {
    return static_cast<int>(coordinate_hash(seed, i, j) % static_cast<unsigned>(levels));
  };
  const auto top = sample(vx, vz) * (spacing - u) + sample(vx + 1, vz) * u;
  const auto bottom = sample(vx, vz + 1) * (spacing - u) + sample(vx + 1, vz + 1) * u;
  return (top * (spacing - v) + bottom * v) / (spacing * spacing);
}

constexpr int district = 64;

enum class Structure { none, lane, house, marker };

struct Footprint {
  Structure structure;
  int x;
  int z;
  int door;
};

Footprint footprint(std::uint32_t seed, int x, int z) {
  const auto rx = floor_div(x, district);
  const auto rz = floor_div(z, district);
  const auto dx = floor_mod(x, district) - district / 2;
  const auto dz = floor_mod(z, district) - district / 2;
  const auto choice = coordinate_hash(seed ^ 0xb0ac14d3U, rx, rz) % 4U;
  if (choice == 0)
    return {std::abs(dx) <= 1 && std::abs(dz) <= 1 ? Structure::marker : Structure::none, dx, dz,
            0};
  if (std::abs(dx) <= 12 && std::abs(dz) <= 1) return {Structure::lane, dx, dz, 0};
  for (const auto center : {std::array{-8, 4}, std::array{8, 4}, std::array{0, -4}}) {
    const auto local_x = dx - center[0];
    const auto local_z = dz - center[1];
    if (std::abs(local_x) <= 2 && std::abs(local_z) <= 2)
      return {Structure::house, local_x, local_z, center[1] > 0 ? -2 : 2};
  }
  return {Structure::none, dx, dz, 0};
}

std::string_view feature_name(Structure structure) {
  switch (structure) {
    case Structure::lane:
    case Structure::house:
      return "hamlet";
    case Structure::marker:
      return "waystone";
    case Structure::none:
      return "none";
  }
  return "none";
}

}  // namespace

SeededTerrain::SeededTerrain(std::uint32_t seed, bool frontier)
    : seed_(seed), frontier_(frontier), chunks_(diameter * diameter) {}

int SeededTerrain::ground(int x, int z) const {
  const int depth = 2 + blend(seed_, x, z, 24, 7);
  // Keep the original spawn and approach usable in both generators.
  const int from_spawn = std::max({0, 10 - x, x - 15, 15 - z, z - 19});
  return std::min(depth, 2 + from_spawn / 3);
}

std::string_view SeededTerrain::biome(int x, int z) const {
  if (!frontier_ || !inside(x) || !inside(z)) return {};
  const auto climate = blend(seed_ ^ 0xa73ba093U, x, z, district, 12);
  if (climate < 4) return "meadow";
  if (climate < 7) return "woodland";
  return "ridge";
}

std::string_view SeededTerrain::landmark(int x, int z) const {
  if (!frontier_ || !inside(x) || !inside(z)) return {};
  return feature_name(footprint(seed_, x, z).structure);
}

SeededTerrain::Column SeededTerrain::generated(int x, int z) const {
  Column blocks{};
  const auto climate = biome(x, z);
  const auto site = frontier_ ? footprint(seed_, x, z) : Footprint{Structure::none, 0, 0, 0};
  int surface = ground(x, z);
  if (site.structure != Structure::none) {
    const auto center = [](int at) { return floor_div(at, district) * district + district / 2; };
    surface = std::min(ground(center(x), center(z)), height - 4);
  }

  blocks[0] = Block::dirt;
  for (int y = 1; y < surface - 1; ++y) {
    blocks[y] = y <= 2 ? Block::dirt : Block::stone;
    if (!frontier_ || y < 2 || site.structure != Structure::none) continue;
    const auto vein = coordinate_hash(seed_ ^ (0x81462f97U * static_cast<unsigned>(y / 2)),
                                      floor_div(x, 2), floor_div(z, 2));
    if (vein % 17U == 0) blocks[y] = Block::copper;
    if (vein % 23U == 0 && climate == "ridge") blocks[y] = Block::quartz;
  }
  blocks[surface - 1] = climate == "ridge" ? Block::stone : Block::grass;
  switch (site.structure) {
    case Structure::lane:
      blocks[surface - 1] = Block::dirt;
      break;
    case Structure::house:
      blocks[surface - 1] = Block::timber;
      blocks[surface + 2] = Block::timber;
      if ((std::abs(site.x) == 2 || std::abs(site.z) == 2) &&
          !(site.x == 0 && site.z == site.door)) {
        blocks[surface] = Block::timber;
        blocks[surface + 1] = Block::timber;
      }
      break;
    case Structure::marker:
      blocks[surface] = Block::stone;
      if (site.x == 0 && site.z == 0) blocks[surface + 1] = Block::quartz;
      break;
    case Structure::none:
      if (surface >= 4 && climate == "woodland" &&
          coordinate_hash(seed_ ^ 0xc61f75abU, x, z) % 19U == 0) {
        blocks[surface] = Block::timber;
        blocks[surface + 1] = Block::grass;
      }
      break;
  }
  return blocks;
}

std::size_t SeededTerrain::slot(int x, int z) {
  return static_cast<std::size_t>(floor_mod(z, diameter) * diameter + floor_mod(x, diameter));
}

std::size_t SeededTerrain::index(int x, int y, int z) {
  return static_cast<std::size_t>((y * chunk_size + floor_mod(z, chunk_size)) * chunk_size +
                                  floor_mod(x, chunk_size));
}

bool SeededTerrain::resident(int x, int z) const {
  const auto cx = floor_div(x, chunk_size);
  const auto cz = floor_div(z, chunk_size);
  const auto& chunk = chunks_[slot(cx, cz)];
  return chunk.x == cx && chunk.z == cz;
}

Block SeededTerrain::block(int x, int y, int z) const {
  if (!inside(x) || !inside(z) || y < 0 || y >= height || !resident(x, z)) return Block::air;
  const auto cx = floor_div(x, chunk_size);
  const auto cz = floor_div(z, chunk_size);
  return chunks_[slot(cx, cz)].cells[index(x, y, z)];
}

bool SeededTerrain::set(int x, int y, int z, Block block) {
  if (!inside(x) || !inside(z) || y <= 0 || y >= height || !resident(x, z)) return false;
  const Position key{x, z, y};
  if (generated(x, z)[y] == block)
    edits_.erase(key);
  else {
    if (edits_.size() == max_edits && !edits_.contains(key)) return false;
    edits_[key] = block;
  }
  chunks_[slot(floor_div(x, chunk_size), floor_div(z, chunk_size))].cells[index(x, y, z)] = block;
  return true;
}

void SeededTerrain::stream(int x, int z) const {
  const auto cx = floor_div(x, chunk_size);
  const auto cz = floor_div(z, chunk_size);
  if (center_x_ == cx && center_z_ == cz) return;
  center_x_ = cx;
  center_z_ = cz;
  // Ring slots retain overlapping chunks and are reused only for incoming chunks.
  loaded_ = 0;
  for (int dz = -radius; dz <= radius; ++dz) {
    for (int dx = -radius; dx <= radius; ++dx) {
      const int next_x = cx + dx;
      const int next_z = cz + dz;
      auto& chunk = chunks_[slot(next_x, next_z)];
      const int left = next_x * chunk_size;
      const int front = next_z * chunk_size;
      if (!inside(left) || !inside(front)) {
        chunk.x = chunk.z = limit;
        continue;
      }
      ++loaded_;
      if (chunk.x == next_x && chunk.z == next_z) continue;
      chunk.x = next_x;
      chunk.z = next_z;
      for (int iz = front; iz < front + chunk_size; ++iz) {
        for (int ix = left; ix < left + chunk_size; ++ix) {
          const auto column = generated(ix, iz);
          for (int iy = 0; iy < height; ++iy) chunk.cells[index(ix, iy, iz)] = column[iy];
        }
      }
      for (auto edit = edits_.lower_bound({left, -limit, 0});
           edit != edits_.end() && edit->first[0] < left + chunk_size; ++edit) {
        const auto& [ex, ez, ey] = edit->first;
        if (ez >= front && ez < front + chunk_size) chunk.cells[index(ex, ey, ez)] = edit->second;
      }
    }
  }
}

bool SeededTerrain::read(std::istream& input) {
  std::string line;
  const auto header =
      frontier_ ? "wds-voxel-frontier-v1 " + std::to_string(seed_) : "wds-voxel-seeded-v1";
  if (!std::getline(input, line) || line != header) return false;
  const auto max_block = frontier_ ? Block::timber : Block::stone;
  Edits candidate;
  while (std::getline(input, line)) {
    std::istringstream row(line);
    int x, y, z, value;
    std::string extra;
    if (candidate.size() == max_edits || !(row >> x >> y >> z >> value) || row >> extra ||
        !inside(x) || !inside(z) || y <= 0 || y >= height || value < 0 ||
        value > static_cast<int>(max_block) || generated(x, z)[y] == static_cast<Block>(value) ||
        !candidate.emplace(Position{x, z, y}, static_cast<Block>(value)).second)
      return false;
  }
  if (!input.eof() || input.bad()) return false;
  edits_ = std::move(candidate);
  for (auto& chunk : chunks_) chunk.x = chunk.z = limit;
  center_x_ = center_z_ = limit;
  return true;
}

void SeededTerrain::write(std::ostream& output) const {
  if (frontier_)
    output << "wds-voxel-frontier-v1 " << seed_ << '\n';
  else
    output << "wds-voxel-seeded-v1\n";
  for (const auto& [key, block] : edits_) {
    const auto& [x, z, y] = key;
    output << x << ' ' << y << ' ' << z << ' ' << static_cast<int>(block) << '\n';
  }
}

}  // namespace wds::voxel
