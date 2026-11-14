#include "voxel_world.hpp"

#include <cmath>
#include <sstream>
#include <stdexcept>

namespace {

void check(bool ok, const char* failure) {
  if (!ok) throw std::runtime_error(failure);
}

}  // namespace

int main() {
  using namespace wds::voxel;
  const World world;
  Player player;
  check(!world.collides(player.feet), "spawn must be free");
  const auto ground = world.cast(player.eye(), {0, -1, 0}, 5);
  check(ground.block == Block::grass && std::abs(ground.distance - 1.55) < 0.001,
        "downward ray must hit the ground");
  const auto wall = world.cast(player.eye(), player.view(), 20);
  check(wall.block == Block::stone && std::abs(wall.distance - 5.5) < 0.001,
        "forward ray must hit the wall");
  check(world.cast(player.eye(), {0, 1, 0}, 20).block == Block::air, "upward ray must miss");
  check(pixel(world, player.eye(), player.view()) != pixel(world, player.eye(), {0, 1, 0}),
        "wall and sky must have different colors");
  for (int step = 0; step < 160; ++step) player.step(world, 1, 0, false, 0.01);
  check(player.feet.z < 12.4 && player.feet.z >= 12.27 - 0.001,
        "forward motion must stop at the obstacle");
  check(!world.collides(player.feet) && player.grounded, "player must rest on the ground");
  player.step(world, 0, 0, true, 0.01);
  check(player.feet.y > 2 && !player.grounded, "jump must leave the ground");
  for (int step = 0; step < 180; ++step) player.step(world, 0, 0, false, 0.01);
  check(std::abs(player.feet.y - 2) < 0.001 && player.grounded, "jump must land");
  const double x = player.feet.x;
  player.step(world, 1, 1, false, 0.02);
  check(player.feet.x > x, "player can slide sideways along a wall");

  World edited;
  const Player start;
  check(edited.edit(start.feet, start.eye(), start.view(), true), "place against the wall");
  check(edited.block(12, 3, 12) == Block::stone, "place must use the visible face");
  check(edited.edit(start.feet, start.eye(), start.view(), false), "break placed block");
  check(edited.block(12, 3, 12) == Block::air, "break must restore empty space");
  check(edited.edit(start.feet, start.eye(), start.view(), false), "break original wall");
  check(!edited.edit(start.feet, start.eye(), {0, -1, 0}, true),
        "placement must not intersect the player");

  std::stringstream save;
  edited.write(save);
  World reloaded;
  check(reloaded.read(save) && reloaded.block(12, 3, 11) == Block::air,
        "reopening must preserve the wall edit");
  std::istringstream corrupt("wds-voxel-v1 24 12 24\n00");
  check(!reloaded.read(corrupt) && reloaded.block(12, 3, 11) == Block::air,
        "invalid save must preserve the current world");

  const auto mesa = template_content("mesa");
  check(mesa.has_value(), "mesa preset exists");
  std::stringstream content_file;
  write_content(content_file, *mesa);
  const auto decoded = read_content(content_file);
  check(decoded && decoded->sky == 0xe9af8b && decoded->placement == Block::dirt &&
            !decoded->style(Block::stone).breakable,
        "content settings survive a project file round trip");
  World themed(*decoded);
  check(pixel(themed, start.eye(), {0, 1, 0}) != pixel(world, start.eye(), {0, 1, 0}),
        "project sky color drives rendering");
  check(!themed.edit(start.feet, start.eye(), start.view(), false),
        "locked stone cannot be broken");
  check(themed.edit(start.feet, start.eye(), start.view(), true) &&
            themed.block(12, 3, 12) == Block::dirt,
        "placed block type comes from project data");
  std::stringstream themed_save;
  themed.write(themed_save);
  World themed_reload(*decoded);
  check(themed_reload.read(themed_save) && themed_reload.block(12, 3, 12) == Block::dirt &&
            !themed_reload.content().style(Block::stone).breakable,
        "loading world data preserves the project's content rules");
  std::istringstream invalid_content(
      "wds-sandbox-content-v1\nsky ffaa00\nplace air\ngrass 123456 breakable\n");
  check(!read_content(invalid_content), "invalid and incomplete content must be rejected");

  Player standard_jump, higher_jump;
  standard_jump.step(world, 0, 0, false, 0.01);
  higher_jump.step(world, 0, 0, false, 0.01);
  standard_jump.step(world, 0, 0, true, 0.01, 6);
  higher_jump.step(world, 0, 0, true, 0.01, 8);
  check(higher_jump.feet.y > standard_jump.feet.y, "rule must change actual jump motion");
}
