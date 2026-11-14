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

struct FieldEdit {
  std::string resource;
  std::string field;
  std::uint64_t revision;
  std::int64_t value;
  bool operator==(const FieldEdit&) const = default;
};

struct FieldResult {
  std::string code;
  std::string location{};
  std::optional<std::uint64_t> revision{};
  std::optional<std::int64_t> old_value{};
  std::optional<std::int64_t> new_value{};
  bool changed = false;
};

ProjectResult open_project(const std::filesystem::path& directory);
ProjectResult create_project(const std::filesystem::path& directory, std::string_view id);
FieldResult edit_field(const std::filesystem::path& directory, const FieldEdit& edit, bool apply);
ProjectResult recover_project(const std::filesystem::path& directory);
std::string field_result_json(const FieldResult& result);
std::string project_result_json(const ProjectResult& result);
std::string project_resources_json(const ProjectResult& result);
std::string project_operations_json();

}  // namespace wds::platform
