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

  World yard;
  yard.use_seed(53207, TerrainProfile::workyard);
  std::istringstream tracks("wds-voxel-workyard-v1 53207\n13 2 17 7\n14 2 17 7\n");
  check(yard.read(tracks), "workyard reads rail states");
  Player rider;
  rider.step(yard, 0, 0, false, 0.01);
  check(rider.toggle_cart(yard) && rider.cart == Cell{13, 2, 17},
        "a grounded player can board from the next cell");
  check(rider.move_cart(yard, 1, 0) && rider.feet.x == 14.5, "cart moves the rider along the rail");
  check(!rider.move_cart(yard, 1, 0) && rider.cart == Cell{14, 2, 17},
        "rider remains at the end of the rail");
  const auto rail = yard.cast(rider.eye(), {0, -1, 0}, 4);
  check(rail.block == Block::rail && std::abs(rail.distance - 1.44) < 0.001 &&
            !yard.collides(rider.feet),
        "rail renders as a slab and allows the rider to stand on its support");
  check(rider.toggle_cart(yard) && !rider.cart, "rider can leave the cart safely");

  std::istringstream incompatible("wds-voxel-workyard-v1 53207\n15 8 10 13\n");
  check(!yard.read(incompatible) && yard.block(13, 2, 17) == Block::rail,
        "the original profile rejects new block IDs without changing its existing state");
  World thermal;
  thermal.use_seed(53207, TerrainProfile::thermal_workyard);
  std::istringstream vents("wds-voxel-workyard-v2 53207\n15 8 10 13\n16 7 10 8\n");
  check(thermal.read(vents), "thermal workyard reads both sources");
  const auto falling = thermal.step_dynamics(15, 10, 2, 1, 1);
  check(falling.lava_updates == 1 && falling.water_updates == 1 && falling.solidified == 0 &&
            thermal.block(15, 7, 10) == Block::lava_high,
        "lava and water fall through empty cells on the first tick");
  const auto cooled = thermal.step_dynamics(15, 10, 2, 1, 1);
  check(cooled.solidified == 1 && thermal.block(15, 7, 10) == Block::basalt &&
            thermal.block(16, 7, 10) == Block::spring && solid(Block::basalt),
        "adjacent water cools lava into solid basalt across the chunk boundary");
  std::stringstream heat_save;
  thermal.write(heat_save);
  World reloaded_thermal;
  reloaded_thermal.use_seed(53207, TerrainProfile::thermal_workyard);
  check(reloaded_thermal.read(heat_save) && reloaded_thermal.block(15, 7, 10) == Block::basalt,
        "thermal workyard round trips cooled material");
}
