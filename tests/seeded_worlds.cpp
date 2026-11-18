#include <cstdlib>
#include <filesystem>
#include <iostream>

#include "platform_project.hpp"

namespace {

using namespace wds::platform;
using wds::voxel::Block;
using wds::voxel::Player;
using wds::voxel::Vec3;
using wds::voxel::World;

void require(bool condition, const char* detail) {
  if (condition) return;
  std::cerr << detail << '\n';
  std::exit(1);
}

int surface(const World& world, int x, int z) {
  int y = World::height - 1;
  while (y > 0 && world.block(x, y, z) == Block::air) --y;
  return y;
}

std::uint64_t signature(const World& world, int x, int z) {
  std::uint64_t fingerprint = 14695981039346656037ULL;
  for (int dz = 0; dz < 16; ++dz)
    for (int dx = 0; dx < 16; ++dx)
      for (int y = 0; y < World::height; ++y) {
        fingerprint ^= static_cast<std::uint64_t>(world.block(x + dx, y, z + dz));
        fingerprint *= 1099511628211ULL;
      }
  return fingerprint;
}

void voxel_checks(const std::filesystem::path& project) {
  auto loaded = start_scene(project, 0, true);
  require(loaded.voxel_scene.has_value(), "Seeded 3D project did not load");
  auto& scene = *loaded.voxel_scene;
  auto& world = scene.world;
  require(world.terrain() && world.terrain()->loaded() == 25, "Expected 25 cached chunks at spawn");
  require(!world.terrain()->resident(48, 17), "Far-eastern chunk loaded before moving");
  Player player;
  for (int i = 0; i < 32; ++i) player.step(world, 0, 1, false, 0.025);
  require(player.feet.x > 16 && world.terrain()->resident(48, 17),
          "Movement did not stream the next chunk");
  for (int i = 0; i < 80; ++i) player.step(world, 0, 0, false, 0.025);
  require(player.grounded && !world.collides(player.feet) &&
              world.collides({player.feet.x, player.feet.y - 0.01, player.feet.z}),
          "Player did not land on solid seeded terrain at the seam");

  constexpr Vec3 west{-55.5, 9.5, 24.5};
  constexpr Vec3 east{80.5, 9.5, 24.5};
  world.stream(west);
  const auto terrain_hash = signature(world, -64, 16);
  World replay;
  replay.use_seed(world.terrain()->seed());
  replay.stream(east);
  replay.stream(west);
  require(signature(replay, -64, 16) == terrain_hash, "Same seed depends on traversal order");
  World another;
  another.use_seed(world.terrain()->seed() + 1);
  another.stream(west);
  require(signature(another, -64, 16) != terrain_hash, "Different seed produced the same terrain");

  constexpr int x = -56;
  constexpr int z = 24;
  const auto y = surface(world, x, z);
  require(world.block(x, y, z) == Block::grass &&
              world.edit(west, {west.x, y + 2.5, west.z}, {0, -1, 0}, false) &&
              world.block(x, y, z) == Block::air,
          "Unable to break a seeded tile in a negative chunk");
  world.stream(east);
  require(!world.terrain()->resident(x, z) && world.terrain()->loaded() == 25,
          "Old chunk was not evicted");
  world.stream(west);
  require(world.block(x, y, z) == Block::air, "Eviction lost the unsaved edit");
  const auto saved = save_voxel_scene(project, scene);
  require(saved.code == "ok" && saved.revision == 1, "Seeded edit did not save");
  require(save_voxel_scene(project, scene).code == "revision_conflict",
          "Stale voxel save accepted");
  auto reopened = start_scene(project, 1, true);
  require(reopened.voxel_scene.has_value(), "Seeded edit did not reopen");
  reopened.voxel_scene->world.stream(west);
  require(reopened.voxel_scene->world.block(x, y, z) == Block::air &&
              signature(reopened.voxel_scene->world, -64, 16) == signature(world, -64, 16),
          "Reopened world differs from the saved edits");
  std::cout << "Voxel seed fingerprint " << terrain_hash
            << ", collision, 25 chunks and saved negative edit passed.\n";
}

void advance(SceneSession& session, SceneKey key, std::uint64_t ticks) {
  SceneInput input;
  input.set(static_cast<std::size_t>(key));
  require(session.advance(ticks, input), "Invalid test input");
}

void tile_checks(const std::filesystem::path& project) {
  auto loaded = start_scene(project, 0);
  require(loaded.session && loaded.session->terrain(), "Tile project did not load");
  auto& session = *loaded.session;
  const auto& terrain = *session.terrain();
  const auto path = terrain.route({-1, 0}, {2, 0});
  require(!terrain.visible({-1, 0}, {2, 0}) && path.size() == 6,
          "Expected a shortest route around the opaque rock");
  require(terrain.daylight(0) == 255 && terrain.daylight(120) == 72 && terrain.daylight(240) == 255,
          "Daylight did not complete a deterministic cycle");
  require(session.advance(120) && terrain.daylight(session.snapshot().ticks) == 72,
          "Scene stepping did not advance to night");
  session.restart();
  advance(session, SceneKey::right, 12);
  require(session.snapshot().entities.front().x == -6, "Player did not stop at rock edge");
  advance(session, SceneKey::up, 4);
  advance(session, SceneKey::right, 50);
  require(terrain.chunk().x == 3 && terrain.loaded() == 25 && !terrain.resident({-1, 0}),
          "Tile regions did not follow the player");
  const auto distant_tile = terrain.tile({24, 2});
  auto replay = start_scene(project, 0);
  require(replay.session.has_value(), "Unable to restart from the same seed");
  advance(*replay.session, SceneKey::right, 12);
  advance(*replay.session, SceneKey::up, 4);
  advance(*replay.session, SceneKey::right, 50);
  require(
      replay.session->terrain()->tile({24, 2}) == distant_tile &&
          replay.session->snapshot().entities.front().x == session.snapshot().entities.front().x,
      "Fixed-seed movement replay changed");
  const TileEdit edit{{0, 0}, "soil", 0};
  const auto preview = edit_tile(project, edit, false);
  const auto unchanged = start_scene(project, 0);
  require(preview.changed && unchanged.session && unchanged.session->terrain() &&
              unchanged.session->terrain()->solid({0, 0}),
          "Tile preview wrote source bytes");
  const auto saved = edit_tile(project, edit, true, true);
  require(saved.code == "ok" && saved.revision == 1 && saved.scene_change,
          "Tile edit did not commit");
  require(edit_tile(project, edit, true).code == "revision_conflict", "Stale tile edit accepted");
  auto reopened = start_scene(project, 1);
  require(reopened.session && reopened.session->terrain()->tile({0, 0}) == Tile::soil,
          "Saved tile edit did not reopen");
  const auto& opened = *reopened.session->terrain();
  require(opened.visible({-1, 0}, {0, 0}) && !opened.visible({-1, 0}, {2, 0}) &&
              opened.route({-1, 0}, {2, 0}).size() == 4,
          "Canopy must block sight while allowing a direct route");
  advance(*reopened.session, SceneKey::right, 12);
  require(reopened.session->snapshot().entities.front().x == 88,
          "Saved path did not allow movement");
  const auto invalid = edit_tile(project, {{-1, 0}, "rock", 1}, true);
  require(invalid.code == "invalid_terrain" && start_scene(project, 1).session.has_value(),
          "Rock spawned on player or damage was written");
  std::cout << "Tiles replayed to chunk 3; route 6 to 4 points, day/night 255/72 and revision 1 "
               "passed.\n";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) return 2;
  voxel_checks(argv[1]);
  tile_checks(argv[2]);
}
