#pragma once

#include <array>
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

struct SceneEntity {
  std::string id;
  std::int64_t x = 0;
  std::int64_t y = 0;
  std::int64_t vx = 0;
  std::int64_t vy = 0;
};

struct SceneField {
  std::string_view name;
  std::int64_t SceneEntity::* member;
};

inline constexpr std::int64_t scene_field_limit = 1'000'000;
inline constexpr std::array scene_fields{
    SceneField{"x", &SceneEntity::x}, SceneField{"y", &SceneEntity::y},
    SceneField{"vx", &SceneEntity::vx}, SceneField{"vy", &SceneEntity::vy}};

struct SceneEdit {
  std::string entity;
  std::string field;
  std::uint64_t revision;
  std::int64_t value;
};

struct SceneSnapshot {
  std::uint64_t revision = 0;
  std::string scene_id;
  std::uint64_t ticks = 0;
  std::vector<SceneEntity> entities;
};

struct SceneResult;

class SceneSession {
 public:
  static constexpr std::uint64_t max_ticks = 600;

  const SceneSnapshot& snapshot() const { return state_; }
  bool advance(std::uint64_t ticks);

 private:
  friend SceneResult start_scene(const std::filesystem::path&, std::uint64_t);

  SceneSession() = default;
  SceneSnapshot state_;
};

struct SceneResult {
  std::optional<SceneSession> session;
  std::string code;
  std::string location{};
  std::string resource_path{};
  std::optional<std::uint64_t> revision{};
};

ProjectResult open_project(const std::filesystem::path& directory);
ProjectResult create_project(const std::filesystem::path& directory, std::string_view id);
FieldResult edit_field(const std::filesystem::path& directory, const FieldEdit& edit, bool apply);
ProjectResult recover_project(const std::filesystem::path& directory);
SceneResult start_scene(const std::filesystem::path& directory, std::uint64_t expected_revision);
FieldResult edit_scene(const std::filesystem::path& directory, const SceneEdit& edit, bool apply);
std::string scene_result_json(const SceneResult& result, bool include_fields = false);
std::string field_result_json(const FieldResult& result);
std::string project_result_json(const ProjectResult& result);
std::string project_resources_json(const ProjectResult& result);
std::string project_operations_json();

}  // namespace wds::platform
