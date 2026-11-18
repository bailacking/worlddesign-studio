#include "voxel_terrain.hpp"

#include <algorithm>
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

}  // namespace

SeededTerrain::SeededTerrain(std::uint32_t seed) : seed_(seed), chunks_(diameter * diameter) {}

Block SeededTerrain::generated(int x, int y, int z) const {
  if (y == 0) return Block::dirt;
  // Blend integer corner heights across a grid larger than a chunk. Adjacent chunks
  // sample the same heights at their seam, independent of traversal order.
  constexpr int spacing = 24;
  const auto vx = floor_div(x, spacing);
  const auto vz = floor_div(z, spacing);
  const auto u = floor_mod(x, spacing);
  const auto v = floor_mod(z, spacing);
  const auto relief = [&](int i, int j) {
    return static_cast<int>(coordinate_hash(seed_, i, j) % 7U);
  };
  const auto top = relief(vx, vz) * (spacing - u) + relief(vx + 1, vz) * u;
  const auto bottom = relief(vx, vz + 1) * (spacing - u) + relief(vx + 1, vz + 1) * u;
  const int depth = 2 + (top * (spacing - v) + bottom * v) / (spacing * spacing);
  // Flat starter clearing so old and new projects can reuse the same spawn.
  const int from_spawn = std::max({0, 10 - x, x - 15, 15 - z, z - 19});
  const int surface = std::min(depth, 2 + from_spawn / 3);
  if (y >= surface) return Block::air;
  if (y == surface - 1) return Block::grass;
  return y <= 2 ? Block::dirt : Block::stone;
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
  if (generated(x, y, z) == block)
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
          for (int iy = 0; iy < height; ++iy)
            chunk.cells[index(ix, iy, iz)] = generated(ix, iy, iz);
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
  if (!std::getline(input, line) || line != "wds-voxel-seeded-v1") return false;
  Edits candidate;
  while (std::getline(input, line)) {
    std::istringstream row(line);
    int x, y, z, value;
    std::string extra;
    if (candidate.size() == max_edits || !(row >> x >> y >> z >> value) || row >> extra ||
        !inside(x) || !inside(z) || y <= 0 || y >= height || value < 0 || value > 3 ||
        generated(x, y, z) == static_cast<Block>(value) ||
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
  output << "wds-voxel-seeded-v1\n";
  for (const auto& [key, block] : edits_) {
    const auto& [x, z, y] = key;
    output << x << ' ' << y << ' ' << z << ' ' << static_cast<int>(block) << '\n';
  }
}

}  // namespace wds::voxel
