#pragma once

#include <array>
#include <map>
#include <string_view>

#include "platform_project.hpp"
#include "workshop_document.hpp"

namespace wds::lab {

using Json = nlohmann::json;
inline constexpr std::array<std::string_view, 2> world_names{"surface", "cavern"};
inline constexpr std::uint64_t max_revision = 9007199254740991ULL;
inline constexpr std::size_t max_players = 8;

struct Position {
  int x = 12;
  int z = 17;
};

struct Explorer {
  std::string id;
  std::size_t world = 0;
  std::array<Position, 2> positions{};
  items::Workshop inventory;
  std::uint64_t sequence = 0;
  Json last_command = Json::object();
};

struct Campaign {
  std::array<voxel::World, 2> worlds;
  std::map<std::string, Explorer> players;
  std::uint64_t revision = 0;
};

std::uint64_t checked_number(const Json& value, std::uint64_t maximum = max_revision);
std::size_t world_number(const Json& name);
Json save_campaign(const Campaign& campaign, const Json& identity);
Campaign load_campaign(const Json& saved, Campaign initial, const Json& identity,
                       const Json& catalog);
Json snapshot(const Campaign& campaign, const Explorer& explorer);
std::string_view apply(Campaign& campaign, Explorer& explorer, const Json& command);

}  // namespace wds::lab
