#include <cstdlib>
#include <iostream>
#include <string_view>

#include "platform_project.hpp"

namespace {

using wds::platform::SceneKey;
using wds::platform::SceneOutcome;
using wds::platform::start_scene;
using wds::voxel::Block;

void require(bool condition, const char* detail) {
  if (condition) return;
  std::cerr << detail << '\n';
  std::exit(1);
}

void voxel_checks() {
  auto loaded = start_scene(".", 0, true);
  require(loaded.voxel_scene.has_value(), "Load authored voxel project at revision 0");
  auto& session = *loaded.voxel_scene;
  auto& world = session.world;
  require(world.content().sky == 0xb5dcd9 && world.content().style(Block::stone).color == 0x5969a9,
          "Material palette was not loaded");
  wds::voxel::Player player;
  require(!world.edit(player.feet, player.eye(), player.view(), false) &&
              world.block(12, 3, 12) == Block::stone,
          "Protected crystal face was removed");
  player.feet.x += 1;
  require(world.block(13, 3, 12) == Block::dirt &&
              world.edit(player.feet, player.eye(), player.view(), false) &&
              world.block(13, 3, 12) == Block::air,
          "Failed to remove the adjacent dirt block");
  require(world.edit(player.feet, player.eye(), player.view(), true) &&
              world.block(13, 3, 12) == Block::grass,
          "Replacement used the wrong material or cell");
  const auto saved = wds::platform::save_voxel_scene(".", session);
  const auto reopened = start_scene(".", 1, true);
  require(saved.code == "ok" && saved.changed && saved.revision == 1 && reopened.voxel_scene &&
              reopened.voxel_scene->world.block(12, 3, 12) == Block::stone &&
              reopened.voxel_scene->world.block(13, 3, 12) == Block::grass,
          "Saved revision does not retain the protected face and the replacement");
  std::cout
      << R"({"palette_loaded":true,"protected_face":true,"break_place":true,"reopened_revision":1})"
      << '\n';
}

void planar_checks() {
  auto loaded = start_scene(".", 0);
  require(loaded.session.has_value(), "Load authored planar project at revision 0");
  auto& scene = *loaded.session;
  require(scene.sprites().size() == 4, "All four generated sprites must load");
  require(scene.advance(5) && scene.snapshot().behavior_state &&
              scene.snapshot().behavior_state->at("speed") == 4 &&
              scene.snapshot().entities.at(4).x == -64,
          "Windsock event, condition or movement did not fire");

  const auto advance = [&](SceneKey key, std::uint64_t ticks) {
    wds::platform::SceneInput input;
    input.set(static_cast<std::size_t>(key));
    require(scene.advance(ticks, input), "Invalid number of steps");
  };
  advance(SceneKey::right, 40);
  require(scene.snapshot().entities.front().x == -17 &&
              scene.snapshot().outcome == SceneOutcome::playing,
          "Courier did not stop at the wall");
  advance(SceneKey::up, 9);
  advance(SceneKey::right, 24);
  advance(SceneKey::down, 9);
  require(scene.snapshot().outcome == SceneOutcome::won, "Detour did not reach the beacon");
  scene.restart();
  advance(SceneKey::down, 10);
  require(scene.snapshot().outcome == SceneOutcome::lost, "Downburst did not end the run");
  std::cout
      << R"({"sprites_loaded":4,"behavior_state_after_five":4,"wall_stop_x":-17,"detour":"won","downburst":"lost"})"
      << '\n';
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  const std::string_view sample(argv[1]);
  if (sample == "crystal-yard")
    voxel_checks();
  else if (sample == "wind-delivery")
    planar_checks();
  else
    return 2;
}
