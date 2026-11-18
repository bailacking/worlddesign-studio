#pragma once

#include <json.hpp>

#include "workshop.hpp"

namespace wds::items {

struct LoadResult {
  std::optional<Workshop> workshop;
  std::string location;
};

LoadResult read_workshop(const nlohmann::json& document, bool voxel);
nlohmann::json state_json(const State& state);

}  // namespace wds::items
