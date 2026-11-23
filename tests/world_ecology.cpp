#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <set>
#include <string_view>

#include "platform_project.hpp"

namespace {

using wds::platform::start_scene;
using wds::platform::TilePoint;
using wds::platform::TileWorld;
using wds::voxel::Block;
using wds::voxel::Player;
using wds::voxel::SeededTerrain;
using wds::voxel::World;

void require(bool condition, const char* message) {
  if (condition) return;
  std::cerr << message << '\n';
  std::exit(1);
}

auto column(const World& world, int x, int z) {
  std::array<Block, World::height> blocks{};
  for (int y = 0; y < World::height; ++y) blocks[y] = world.block(x, y, z);
  return blocks;
}

void mine_copper(wds::platform::VoxelScene& scene) {
  auto& world = scene.world;
  world.stream({0, 0, 0});
  for (int z = -24; z < 24; ++z) {
    for (int x = -24; x < 24; ++x) {
      const auto blocks = column(world, x, z);
      const auto ore = std::ranges::find(blocks, Block::copper);
      if (ore == blocks.end()) continue;
      const auto bottom = static_cast<int>(ore - blocks.begin());
      for (int y = World::height - 1; y >= bottom; --y) {
        if (blocks[y] == Block::air) continue;
        require(scene.edit(Player{}.feet, {x + 0.5, y + 1.5, z + 0.5}, {0, -1, 0}, false),
                "Could not mine a generated column");
      }
      require(scene.workshop && scene.workshop->apply({"pickup", "copper-ore"}) == "ok" &&
                  scene.workshop->has_item("copper-ore"),
              "Generated copper did not drop a collectible item");
      return;
    }
  }
  require(false, "Frontier example has no copper in the test area");
}

void frontier(const std::filesystem::path& project) {
  auto loaded = start_scene(project, 0, true);
  require(loaded.voxel_scene.has_value(), "Cannot open the frontier example");
  auto& scene = *loaded.voxel_scene;
  auto& world = scene.world;
  require(world.terrain()->frontier() && !world.collides(Player{}.feet),
          "Frontier profile or spawn is invalid");

  std::set<std::string_view> biomes;
  std::set<std::string_view> landmarks;
  std::set<Block> materials;
  std::set<std::uint64_t> fingerprints;
  for (const auto seed : {73021U, 16431U, 0U, 4294967295U}) {
    World map;
    map.use_seed(seed, true);
    std::uint64_t hash = 14695981039346656037ULL;
    for (int z = -96; z < 96; z += 16) {
      for (int x = -96; x < 96; x += 16) {
        map.stream({static_cast<double>(x), 0, static_cast<double>(z)});
        for (int dz = 0; dz < 16; ++dz) {
          for (int dx = 0; dx < 16; ++dx) {
            biomes.insert(map.terrain()->biome(x + dx, z + dz));
            landmarks.insert(map.terrain()->landmark(x + dx, z + dz));
            const auto blocks = column(map, x + dx, z + dz);
            require(blocks[0] == Block::dirt, "A feature cut through the foundation");
            for (const auto block : blocks) {
              materials.insert(block);
              hash = (hash ^ static_cast<unsigned>(block)) * 1099511628211ULL;
            }
          }
        }
      }
    }
    fingerprints.insert(hash);
  }
  require(fingerprints.size() == 4 &&
              biomes == std::set<std::string_view>{"meadow", "ridge", "woodland"},
          "Seed variation or biome distribution is missing");
  require(landmarks.contains("hamlet") && landmarks.contains("waystone") &&
              materials.contains(Block::copper) && materials.contains(Block::quartz) &&
              materials.contains(Block::timber),
          "Landmarks, timber or minerals are missing");

  constexpr std::array seam{31, 32};
  constexpr int z = 32;
  world.stream({31, 0, z});
  const auto west = column(world, seam[0], z);
  const auto east = column(world, seam[1], z);
  require(
      world.terrain()->landmark(31, z) == "hamlet" && world.terrain()->landmark(32, z) == "hamlet",
      "The sample hamlet must span adjacent chunks");
  require(west == east && std::ranges::find(west, Block::air) != west.end(),
          "Lane elevation changed at a chunk seam");
  world.stream({-128, 0, -128});
  require(!world.terrain()->resident(31, z), "Seam did not leave the cache");
  world.stream({32, 0, z});
  require(column(world, 31, z) == west && column(world, 32, z) == east,
          "Re-entering from the east changed a structure");

  constexpr int edge = -SeededTerrain::limit;
  world.stream({edge, 0, edge});
  require(world.terrain()->loaded() == 9 && !world.terrain()->resident(edge - 1, edge) &&
              world.block(edge - 1, 0, edge) == Block::air,
          "Negative world edge wrapped into a chunk");
  world.stream({SeededTerrain::limit - 1, 0, SeededTerrain::limit - 1});
  require(world.terrain()->loaded() == 9 &&
              world.block(SeededTerrain::limit, 0, SeededTerrain::limit - 1) == Block::air,
          "Positive world edge wrapped into a chunk");

  mine_copper(scene);
  world.stream({31, 0, z});
  const auto first_air = std::ranges::find(west, Block::air);
  const auto top = static_cast<int>(first_air - west.begin()) - 1;
  require(top > 0, "Missing editable surface");
  require(world.edit({30.5, 10, 32.5}, {31.5, top + 2.5, 32.5}, {0, -1, 0}, false),
          "Cannot edit the surface of a new terrain profile");
  require(save_voxel_scene(project, scene).code == "ok", "Cannot save the new terrain profile");
  const auto reopened = start_scene(project, 1, true);
  require(reopened.voxel_scene.has_value(), "Cannot reopen the new terrain profile");
  require(reopened.voxel_scene->workshop && reopened.voxel_scene->workshop->has_item("copper-ore"),
          "Collected copper did not persist when reopening");
  const auto& fresh = reopened.voxel_scene->world;
  fresh.stream({31, 0, z});
  require(fresh.block(31, top, z) == Block::air && column(fresh, 32, z) == east,
          "Reopening did not preserve the sparse edit at the chunk seam");
}

auto sites(const TileWorld& world, TilePoint observer, std::uint64_t tick) {
  std::set<std::pair<TilePoint, wds::platform::Species>> result;
  for (const auto& site : world.populations(observer, tick))
    result.emplace(site.point, site.species);
  return result;
}

void ecology() {
  constexpr TilePoint observer{-1, 0};
  const wds::platform::EcologyRules profile{36, {"observer", 4, 15}};
  for (const auto seed : {73021U, 16431U, 0U, 4294967295U}) {
    TileWorld world(seed, 240, 120, {}, profile);
    world.stream(observer);
    const auto initial = sites(world, observer, 0);
    require(!initial.empty() && initial != sites(world, observer, 18) &&
                initial == sites(world, observer, 36),
            "Population did not spawn, refresh and repeat");
    world.stream({100, 100});
    require(sites(world, observer, 0).empty(), "Unloaded regions still expose population");
    world.stream({0, 0});
    require(initial == sites(world, observer, 0), "Crossing the chunk origin changed population");
    const auto blocked = initial.begin()->first;
    TileWorld altered(seed, 240, 120, {{blocked, wds::platform::Tile::rock}}, profile);
    altered.stream(observer);
    require(!sites(altered, observer, 0).contains(*initial.begin()),
            "Overridden habitat still produced the original population");
    constexpr TilePoint edge{-TileWorld::limit, -TileWorld::limit};
    world.stream(edge);
    require(world.loaded() == 9 && !world.resident({edge.x - 1, edge.y}),
            "Ecology region wrapped at the negative world edge");
    for (const auto& site : world.populations(edge, 0))
      require(TileWorld::inside(site.point), "Population outside the world boundary");
  }
}

}  // namespace

int main(int argc, char** argv) {
  require(argc == 2, "Expected a mutable copy of the frontier project");
  frontier(argv[1]);
  ecology();
}
