#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace wds::platform {

struct Resource {
  std::string id;
  std::string kind;
  std::string path;
  std::vector<std::string> refs;
};

struct Project {
  std::string id;
  std::uint64_t revision;
  std::string entry_scene;
  std::vector<Resource> resources;
};

struct ProjectResult {
  std::optional<Project> project;
  std::string code;
  std::string location;
  bool cleanup_incomplete = false;
};

ProjectResult open_project(const std::filesystem::path& directory);
ProjectResult create_project(const std::filesystem::path& directory, std::string_view id);
std::string project_result_json(const ProjectResult& result);
std::string project_resources_json(const ProjectResult& result);
std::string project_operations_json();

}  // namespace wds::platform
