#include <cstdlib>
#include <filesystem>
#include <iostream>

#include "platform_project.hpp"

namespace {

using namespace wds;
using namespace wds::platform;

void require(bool condition, const char* detail) {
  if (condition) return;
  std::cerr << detail << '\n';
  std::exit(1);
}

int quantity(const items::Container& slots, std::string_view id) {
  int result = 0;
  for (const auto& slot : slots)
    if (slot.id == id) result += slot.count;
  return result;
}

void voxel_chain(const std::filesystem::path& project) {
  auto loaded = start_scene(project, 0, true);
  require(loaded.voxel_scene && loaded.voxel_scene->workshop, "Load the ore-furnace fixture");
  auto& scene = *loaded.voxel_scene;
  auto& shop = *scene.workshop;
  require(shop.state().bag.empty() && shop.state().loot.empty(), "Start with empty hands");
  const voxel::Player player;
  const auto edit = [&](bool place) {
    return scene.edit(player.feet, player.eye(), player.view(), place);
  };
  require(!edit(true), "Cannot place with empty hands");
  for (int i = 0; i < 4; ++i) require(edit(false), "Break the next targeted material block");
  require(shop.state().bag.empty() && quantity(shop.state().loot, "ore") == 3 &&
              quantity(shop.state().loot, "peat") == 1,
          "Mining creates drops before pickup");
  require(shop.apply({"pickup", "ore"}) == "ok" && shop.apply({"pickup", "peat"}) == "ok",
          "Pick up ore and peat");
  require(shop.apply({"recipe", "smelt-ingot"}) == "station_locked", "Smelt requires a furnace");
  std::vector<std::string> grid(9);
  grid[0] = grid[4] = "ore";
  const auto before = shop.state();
  require(
      shop.apply({"recipe", "build-furnace"}, grid) == "invalid_pattern" && shop.state() == before,
      "Invalid order must keep all materials");
  grid[4].clear();
  grid[1] = "ore";
  require(shop.apply({"recipe", "build-furnace"}, grid) == "ok" &&
              shop.state().unlocked.contains("furnace") && quantity(shop.state().bag, "ore") == 1,
          "Two aligned blocks build the first workstation");
  require(shop.apply({"recipe", "smelt-ingot"}) == "ok" &&
              quantity(shop.state().bag, "ingot") == 1 && !shop.has_item("ore") &&
              !shop.has_item("peat"),
          "Smelt consumes ore and fuel");
  require(shop.apply({"store", "ingot"}) == "ok" &&
              shop.apply({"store", "furnace"}) == "container_full" && shop.has_item("furnace"),
          "Full chest cannot take a second item type");
  require(shop.apply({"take", "ingot"}) == "ok" && shop.apply({"store", "ingot"}) == "ok",
          "Chest transfer works in both directions");
  const auto saved = save_voxel_scene(project, scene);
  require(saved.code == "ok" && saved.revision == 1, "Save blocks and materials together");
  require(save_voxel_scene(project, scene).code == "revision_conflict",
          "Reject stale workshop save");
  auto reopened = start_scene(project, 1, true);
  require(reopened.voxel_scene && reopened.voxel_scene->workshop &&
              reopened.voxel_scene->workshop->state() == shop.state() &&
              reopened.voxel_scene->world.block(12, 3, 15) == voxel::Block::air,
          "Reopen the mined blocks, furnace unlock and chest");
  std::cout
      << "Voxel: empty bag -> mining -> drops -> shaped furnace -> ingot -> chest -> revision 1.\n";
}

void survival_chain(const std::filesystem::path& project) {
  auto loaded = start_scene(project, 0);
  require(loaded.session && loaded.session->workshop(), "Load wild-camp fixture");
  auto& session = *loaded.session;
  const auto& workshop = *session.workshop();
  require(workshop.state().bag.empty(), "Camp starts empty");
  require(session.work({"gather", "berry-bush"}) == "source_out_of_reach" &&
              workshop.state().gathered.empty(),
          "Faraway gather must not consume a source");
  require(session.work({"gather", "twigs"}) == "ok" && session.work({"pickup", "twig"}) == "ok",
          "Gather the near twigs");
  SceneInput input;
  input.set(static_cast<std::size_t>(SceneKey::right));
  require(session.advance(8, input), "Move to marks beside stones and berries");
  require(session.work({"gather", "stones"}) == "ok" &&
              session.work({"pickup", "pebble"}) == "ok" &&
              session.work({"gather", "berry-bush"}) == "ok" &&
              session.work({"pickup", "berry"}) == "ok",
          "Collect rocks and berries at the new position");
  require(session.work({"gather", "stones"}) == "source_empty", "Depleted source cannot duplicate");
  const auto before = workshop.state();
  require(
      session.work({"recipe", "cook-berries"}) == "station_locked" && workshop.state() == before,
      "Cooking with no campfire leaves bag untouched");
  require(session.work({"recipe", "build-campfire"}) == "ok" &&
              workshop.state().unlocked.contains("hearth") && workshop.has_item("campfire"),
          "Build and unlock the first campsite");
  require(session.work({"recipe", "make-lantern"}) == "station_locked" &&
              session.work({"recipe", "cook-berries"}) == "ok" &&
              workshop.state().unlocked.contains("meal-lesson") && workshop.has_item("stew"),
          "Cooking spends food and fuel and unlocks the next recipe");
  session.restart();
  require(workshop.state().bag.empty() && workshop.state().gathered.empty() &&
              workshop.state().unlocked.empty() && session.snapshot().entities.front().x == -72,
          "A new session starts with the original scene and inventory");
  std::cout << "Planar: gather at range -> campfire -> cook -> research unlock -> clean restart.\n";
}

void reuse(const std::filesystem::path& project) {
  auto loaded = start_scene(project, 0);
  require(loaded.session && loaded.session->workshop(), "Load original beacon project");
  auto& session = *loaded.session;
  require(session.work({"gather", "stems"}) == "ok" && session.work({"pickup", "fiber"}) == "ok" &&
              session.work({"gather", "sap"}) == "ok" &&
              session.work({"pickup", "resin"}) == "ok" &&
              session.work({"recipe", "build-beacon"}) == "ok" &&
              session.workshop()->has_item("beacon"),
          "An exact item and overlapping tag select different available units");
  auto catalog = session.workshop()->catalog();
  catalog.bag_slots = 1;
  catalog.recipes["overflow"] = {
      "craft", "", "", {items::Ingredient{"fiber", 1}}, {}, items::Stack{"beacon", 1}};
  items::Workshop small(std::move(catalog));
  require(small.drop({"fiber", 11}) && small.apply({"pickup", "fiber"}) == "ok" &&
              quantity(small.state().bag, "fiber") == 8 &&
              quantity(small.state().loot, "fiber") == 3,
          "Pickup of excess loot keeps remainder in drops");
  const auto before = small.state();
  require(small.apply({"recipe", "overflow"}) == "container_full" && small.state() == before,
          "Full output rolls back all input and state changes");
  std::cout
      << "Original: shared tag matching -> beacon; partial pickup and full-output rollback.\n";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 4) return 2;
  voxel_chain(argv[1]);
  survival_chain(argv[2]);
  reuse(argv[3]);
}
