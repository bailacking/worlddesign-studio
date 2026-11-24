#pragma once

#include <cstdint>
#include <filesystem>
#include <json.hpp>
#include <map>
#include <string>
#include <vector>

namespace wds::platform {

enum class MergeSide { current, incoming };
using MergeChoices = std::map<std::string, MergeSide, std::less<>>;

struct SceneMerge {
  nlohmann::json document;
  std::vector<std::string> conflicts;
  nlohmann::json versions;
};

SceneMerge merge_scene_documents(const nlohmann::json& base, const nlohmann::json& current,
                                 const nlohmann::json& incoming, MergeChoices* choices = nullptr);

nlohmann::json compare_projects(const std::filesystem::path& before, std::uint64_t before_revision,
                                const std::filesystem::path& after, std::uint64_t after_revision);

}  // namespace wds::platform
