#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "scene_play.hpp"
#include "voxel_world.hpp"

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

struct SceneChange {
  std::string project_id;
  std::string scene_id;
  std::string resource_path;
  std::string before;
  std::string after;
};

struct FieldResult {
  std::string code;
  std::string location{};
  std::optional<std::uint64_t> revision{};
  std::optional<std::int64_t> old_value{};
  std::optional<std::int64_t> new_value{};
  bool changed = false;
  std::optional<SceneChange> scene_change{};
};

struct SceneField {
  std::string_view name;
  std::int64_t SceneEntity::*member;
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

struct EntityEdit {
  std::string operation;
  std::string entity;
  std::uint64_t revision = 0;
  std::string target{};
  std::size_t index = 0;
};

struct VoxelScene {
  voxel::World world;
  SceneChange source;
  std::uint64_t revision;
};

struct SceneResult {
  std::optional<SceneSession> session;
  std::string code;
  std::string location{};
  std::string resource_path{};
  std::optional<std::uint64_t> revision{};
  std::optional<VoxelScene> voxel_scene{};
};

struct ImportRequest {
  std::string source;
  std::string id;
  std::string kind;
  std::string path;
  std::uint64_t revision = 0;
  std::string expected_digest{};
};

struct ImportResult {
  std::string code;
  std::string location{};
  std::optional<std::uint64_t> revision{};
  std::string resource_id{};
  std::string kind{};
  std::string path{};
  std::optional<std::uintmax_t> bytes{};
  bool changed = false;
  std::string source_name{};
  std::string digest{};
  unsigned width = 0;
  unsigned height = 0;
  std::optional<std::size_t> line_count{};
  std::string text_preview{};
};

struct AssetSource {
  std::string code;
  std::string location{};
  std::string bytes{};
};

struct ImportRecord {
  std::string resource;
  std::string kind;
  std::string path;
  std::string source_name;
  std::uintmax_t bytes = 0;
  std::string digest;
  std::uint64_t revision = 0;
};

struct ImportListResult {
  std::string code;
  std::string location{};
  std::vector<ImportRecord> records;
};

ProjectResult open_project(const std::filesystem::path& directory);
ProjectResult create_project(const std::filesystem::path& directory, std::string_view id);
ImportResult import_resource(const std::filesystem::path& directory, const ImportRequest& request,
                             bool apply);
ImportListResult list_imports(const std::filesystem::path& directory);
AssetSource read_asset_source(const std::filesystem::path& directory, std::string_view id);
FieldResult edit_field(const std::filesystem::path& directory, const FieldEdit& edit, bool apply);
ProjectResult recover_project(const std::filesystem::path& directory);
SceneResult start_scene(const std::filesystem::path& directory, std::uint64_t expected_revision,
                        bool allow_voxel = false);
FieldResult save_voxel_scene(const std::filesystem::path& directory, const VoxelScene& scene);
FieldResult prepare_voxel_save(const VoxelScene& scene);
FieldResult edit_scene(const std::filesystem::path& directory, const SceneEdit& edit, bool apply,
                       bool capture_change = false);
FieldResult edit_entity(const std::filesystem::path& directory, const EntityEdit& edit, bool apply,
                        bool capture_change = false);
FieldResult restore_scene(const std::filesystem::path& directory, std::uint64_t revision,
                          std::string_view request);
inline constexpr std::size_t max_scene_restore_request = 16 * 1024 * 1024;
std::string scene_result_json(const SceneResult& result, bool include_fields = false);
std::string field_result_json(const FieldResult& result);
std::string import_result_json(const ImportResult& result);
std::string import_list_json(const ImportListResult& result);
std::string project_result_json(const ProjectResult& result);
std::string project_resources_json(const ProjectResult& result);
std::string project_operations_json();

}  // namespace wds::platform
