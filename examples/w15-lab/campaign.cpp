#include "campaign.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace wds::lab {
namespace {

constexpr int world_limit = voxel::SeededTerrain::limit;

bool keys(const Json& object, std::initializer_list<std::string_view> names) {
  return object.is_object() && object.size() == names.size() &&
         std::ranges::all_of(names, [&](auto name) { return object.contains(name); });
}

void require(bool valid) {
  if (!valid) throw std::runtime_error("Invalid W15 campaign data");
}

std::string world_data(const voxel::World& world) {
  std::ostringstream encoded;
  world.write(encoded);
  return std::move(encoded).str();
}

voxel::Vec3 feet(Position position) { return {position.x + .5, 2, position.z + .5}; }

Json position_json(Position position) { return Json::array({position.x, position.z}); }

Position read_position(const Json& value) {
  require(value.is_array() && value.size() == 2);
  const auto coordinate = [](const Json& number) {
    if (number.is_number_unsigned())
      return static_cast<int>(checked_number(number, world_limit - 1));
    require(number.is_number_integer());
    const auto coordinate = number.get<std::int64_t>();
    require(coordinate >= -world_limit && coordinate < world_limit);
    return static_cast<int>(coordinate);
  };
  return {coordinate(value[0]), coordinate(value[1])};
}

bool free_space(const voxel::World& world, Position position) {
  if (position.x < -world_limit || position.x >= world_limit || position.z < -world_limit ||
      position.z >= world_limit)
    return false;
  world.stream(feet(position));
  return !world.collides(feet(position));
}

std::optional<Position> direction(const Json& value) {
  if (value == "north") return Position{0, -1};
  if (value == "south") return Position{0, 1};
  if (value == "east") return Position{1, 0};
  if (value == "west") return Position{-1, 0};
  return {};
}

Json positions_json(const Explorer& player) {
  return {{"surface", position_json(player.positions[0])},
          {"cavern", position_json(player.positions[1])}};
}

}  // namespace

std::uint64_t checked_number(const Json& value, std::uint64_t maximum) {
  require(value.is_number_unsigned() ||
          (value.is_number_integer() && value.get<std::int64_t>() >= 0));
  const auto result = value.get<std::uint64_t>();
  require(result <= maximum);
  return result;
}

std::size_t world_number(const Json& name) {
  require(name.is_string());
  const auto found = std::ranges::find(world_names, name.get_ref<const std::string&>());
  if (found == world_names.end()) throw std::runtime_error("Unknown W15 world");
  return static_cast<std::size_t>(found - world_names.begin());
}

Json save_campaign(const Campaign& campaign, const Json& identity) {
  auto worlds = Json::object();
  for (std::size_t index = 0; index < world_names.size(); ++index)
    worlds[world_names[index]] = world_data(campaign.worlds[index]);
  auto players = Json::object();
  for (const auto& [ticket, player] : campaign.players)
    players[ticket] = {{"id", player.id},
                       {"world", world_names[player.world]},
                       {"positions", positions_json(player)},
                       {"sequence", player.sequence},
                       {"last_command", player.last_command},
                       {"inventory", items::state_json(player.inventory.state())}};
  return {{"version", 1},
          {"identity", identity},
          {"revision", campaign.revision},
          {"worlds", std::move(worlds)},
          {"players", std::move(players)}};
}

Campaign load_campaign(const Json& saved, Campaign initial, const Json& identity,
                       const Json& catalog) {
  require(keys(saved, {"version", "identity", "revision", "worlds", "players"}) &&
          checked_number(saved["version"], 1) == 1 && saved["identity"] == identity);
  initial.revision = checked_number(saved["revision"]);
  const auto& worlds = saved["worlds"];
  require(keys(worlds, {"surface", "cavern"}));
  for (std::size_t i = 0; i < world_names.size(); ++i) {
    require(worlds[world_names[i]].is_string());
    std::istringstream input(worlds[world_names[i]].get<std::string>());
    require(initial.worlds[i].read(input));
  }
  const auto& players = saved["players"];
  require(players.is_object() && players.size() <= max_players);
  std::set<std::string> ids;
  for (const auto& [ticket, data] : players.items()) {
    require(ticket.size() == 36 && std::ranges::all_of(ticket, [](char c) {
              return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || c == '-';
            }));
    require(keys(data, {"id", "world", "positions", "sequence", "last_command", "inventory"}) &&
            data["id"].is_string() && data["last_command"].is_object());
    auto id = data["id"].get<std::string>();
    require(id.size() == 8 && id.starts_with("player-") && id.back() >= '1' &&
            id.back() <= '0' + static_cast<int>(players.size()) && ids.insert(id).second);
    const auto& positions = data["positions"];
    require(keys(positions, {"surface", "cavern"}));
    std::array places{read_position(positions["surface"]), read_position(positions["cavern"])};
    for (std::size_t index = 0; index < places.size(); ++index)
      require(free_space(initial.worlds[index], places[index]));
    auto document = catalog;
    document["progress"] = data["inventory"];
    auto inventory = items::read_workshop(document, true);
    require(inventory.workshop.has_value());
    const auto seq = checked_number(data["sequence"], initial.revision);
    require((seq == 0) == data["last_command"].empty());
    initial.players.emplace(ticket,
                            Explorer{std::move(id), world_number(data["world"]), places,
                                     std::move(*inventory.workshop), seq, data["last_command"]});
  }
  return initial;
}

Json snapshot(const Campaign& campaign, const Explorer& explorer) {
  auto players = Json::array();
  for (const auto& entry : campaign.players) {
    const auto& player = entry.second;
    players.push_back({{"id", player.id},
                       {"world", world_names[player.world]},
                       {"position", position_json(player.positions[player.world])}});
  }
  return {{"v", 1},
          {"type", "snapshot"},
          {"revision", campaign.revision},
          {"you", explorer.id},
          {"world", world_names[explorer.world]},
          {"positions", positions_json(explorer)},
          {"last_sequence", explorer.sequence},
          {"players", std::move(players)},
          {"terrain", world_data(campaign.worlds[explorer.world])},
          {"inventory", items::state_json(explorer.inventory.state())}};
}

std::string_view apply(Campaign& campaign, Explorer& explorer, const Json& command) {
  if (!command.is_object() || !command.contains("op") || !command["op"].is_string())
    return "invalid_command";
  const auto& op = command["op"].get_ref<const std::string&>();
  auto& position = explorer.positions[explorer.world];
  auto& world = campaign.worlds[explorer.world];
  if (op == "travel" && keys(command, {"op", "world"})) {
    const auto target = world_number(command["world"]);
    if (!free_space(campaign.worlds[target], explorer.positions[target])) return "blocked";
    if (target == explorer.world) return "already_there";
    explorer.world = target;
    return "ok";
  }
  if ((op == "move" || op == "mine") && keys(command, {"op", "direction"})) {
    const auto delta = direction(command["direction"]);
    if (!delta) return "invalid_command";
    if (op == "move") {
      const Position next{position.x + delta->x, position.z + delta->z};
      if (!free_space(world, next)) return "blocked";
      position = next;
      return "ok";
    }
    auto base = feet(position);
    world.stream(base);
    const voxel::Vec3 eye{base.x, base.y + 1.55, base.z};
    const voxel::Vec3 ray{static_cast<double>(delta->x), 0, static_cast<double>(delta->z)};
    return platform::edit_voxel(world, &explorer.inventory, base, eye, ray, false) ? "ok"
                                                                                   : "blocked";
  }
  if ((op == "pickup" || op == "store" || op == "take" || op == "recipe") &&
      keys(command, {"op", "id"}) && command["id"].is_string()) {
    const auto& id = command["id"].get_ref<const std::string&>();
    if (!id.empty() && id.size() <= 64) return explorer.inventory.apply({op, id});
  }
  return "invalid_command";
}

}  // namespace wds::lab
