#include <cstdlib>
#include <fstream>
#include <iostream>

#include "platform_project.hpp"

namespace {

void require(bool condition, const char* detail) {
  if (condition) return;
  std::cerr << detail << '\n';
  std::exit(1);
}

}  // namespace

int main() {
  using namespace wds::platform;
  using wds::voxel::Block;
  require(start_scene(".", 0).code == "unsupported_scene_kind", "The 2D player must reject 3D");
  auto loaded = start_scene(".", 0, true);
  require(loaded.voxel_scene.has_value(), "Load a voxel entry through the project manifest");
  auto& session = *loaded.voxel_scene;
  auto& world = session.world;
  require(world.content().sky == 0xa5c6ef && world.content().style(Block::stone).color == 0x818db0,
          "Load the declared material resource");
  const auto clean = save_voxel_scene(".", session);
  require(clean.code == "ok" && !clean.changed && clean.revision == 0,
          "An unchanged world must keep its revision");

  wds::voxel::Player player;
  auto camera = player.camera();
  require(world.edit(player.feet, camera.eye, camera.ray(0, 0), true) &&
              world.block(12, 3, 12) == Block::stone,
          "Place in the adjacent cell of the picked face");
  player.feet.x += 1;
  camera = player.camera();
  require(world.edit(player.feet, camera.eye, camera.forward, false) &&
              world.block(13, 3, 11) == Block::air,
          "Break a different picked block");
  auto untouched = start_scene(".", 0, true);
  require(untouched.voxel_scene && untouched.voxel_scene->world.block(12, 3, 12) == Block::air &&
              untouched.voxel_scene->world.block(13, 3, 11) == Block::stone,
          "Draft edits must remain isolated from the project");

  const auto saved = save_voxel_scene(".", session);
  require(saved.code == "ok" && saved.changed && saved.revision == 1,
          "Save a new project revision");
  auto reopened = start_scene(".", 1, true);
  require(reopened.voxel_scene && reopened.voxel_scene->world.block(12, 3, 12) == Block::stone &&
              reopened.voxel_scene->world.block(13, 3, 11) == Block::air,
          "Reopen with both block edits intact");
  require(save_voxel_scene(".", session).code == "revision_conflict",
          "Do not save a draft from an older revision");
  {
    std::ofstream external("assets/quarry.json", std::ios::app);
    external << '\n';
  }
  require(save_voxel_scene(".", *reopened.voxel_scene).code == "scene_content_conflict",
          "Do not overwrite changed scene bytes at the same revision");
}
