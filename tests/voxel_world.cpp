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

  Player standard_jump, higher_jump;
  standard_jump.step(world, 0, 0, false, 0.01);
  higher_jump.step(world, 0, 0, false, 0.01);
  standard_jump.step(world, 0, 0, true, 0.01, 6);
  higher_jump.step(world, 0, 0, true, 0.01, 8);
  check(higher_jump.feet.y > standard_jump.feet.y, "rule must change actual jump motion");
}
