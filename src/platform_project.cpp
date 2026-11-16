#include "platform_project.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <algorithm>
#include <fstream>
#include <json.hpp>
#include <map>
#include <ranges>
#include <set>

namespace wds::platform {
namespace {

namespace fs = std::filesystem;
using Json = nlohmann::json;
constexpr auto manifest_name = "wds.project.json";
constexpr auto journal_name = ".wds-field-edit.json";
constexpr auto staging_name = ".wds-field-edit.tmp";
constexpr auto imports_name = ".wds-imports.json";
constexpr std::uint64_t max_revision = 9007199254740991ULL;
constexpr std::uintmax_t max_manifest_bytes = 16 * 1024 * 1024;
constexpr std::size_t max_edit_bytes = 1024 * 1024;
constexpr std::uintmax_t max_imports_bytes = 1024 * 1024;
constexpr std::size_t max_scene_entities = 1024;

ProjectResult failure(std::string code, std::string location) {
  return {std::nullopt, std::move(code), std::move(location)};
}

std::string portable(const fs::path& path) {
  const auto value = path.generic_u8string();
  return {value.begin(), value.end()};
}

bool lower(char ch) { return ch >= 'a' && ch <= 'z'; }
bool digit(char ch) { return ch >= '0' && ch <= '9'; }

bool valid_id(std::string_view id) {
  return !id.empty() && id.size() <= 64 && lower(id.front()) &&
         std::ranges::all_of(id, [](char ch) { return lower(ch) || digit(ch) || ch == '-'; });
}

bool valid_segment(std::string_view name) {
  if (name.empty() || name.size() > 64 || !(lower(name.front()) || digit(name.front())) ||
      name.ends_with('.') || !std::ranges::all_of(name, [](char ch) {
        return lower(ch) || digit(ch) || ch == '-' || ch == '.';
      }))
    return false;
  const auto stem = name.substr(0, name.find('.'));
  if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul") return false;
  return !(stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) &&
           digit(stem.back()));
}

bool valid_path(std::string_view path) {
  if (!path.starts_with("assets/") || path.ends_with('/')) return false;
  for (auto part : path | std::views::split('/')) {
    if (!valid_segment(std::string_view(part.begin(), part.end()))) return false;
  }
  return true;
}

bool reparse(const fs::path& path) {
#ifdef _WIN32
  const auto attr = GetFileAttributesW(path.c_str());
  return attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
  return fs::is_symlink(fs::symlink_status(path));
#endif
}

enum class PathState { ok, missing, unsafe };

PathState node(const fs::path& path, bool directory) {
  std::error_code error;
  const auto status = fs::symlink_status(path, error);
  if (status.type() == fs::file_type::not_found) return PathState::missing;
  if (error) throw fs::filesystem_error("status", path, error);
  if ((directory ? !fs::is_directory(status) : !fs::is_regular_file(status)) || reparse(path)) {
    return PathState::unsafe;
  }
  return PathState::ok;
}

std::string fold_ascii(std::string value) {
  for (auto& ch : value) {
    if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
  }
  return value;
}

PathState project_path(const fs::path& root, const fs::path& relative) {
  auto parent = root;
  const auto end = relative.end();
  for (auto it = relative.begin(); it != end; ++it) {
    const auto name = portable(*it);
    const auto folded = fold_ascii(name);
    int matches = 0;
    bool exact = false;
    for (const auto& child : fs::directory_iterator(parent)) {
      const auto candidate = portable(child.path().filename());
      if (fold_ascii(candidate) != folded) continue;
      ++matches;
      exact = exact || candidate == name;
    }
    if (matches == 0) return PathState::missing;
    if (matches != 1 || !exact) return PathState::unsafe;
    parent /= *it;
    auto next = it;
    const auto state = node(parent, ++next != end);
    if (state != PathState::ok) return state;
  }
  return PathState::ok;
}

std::optional<std::string> read_bytes(const fs::path& file, std::uintmax_t limit) {
  std::ifstream input(file, std::ios::binary | std::ios::ate);
  if (!input) throw fs::filesystem_error("open", file, std::make_error_code(std::errc::io_error));
  const auto length = static_cast<std::streamoff>(input.tellg());
  if (length < 0)
    throw fs::filesystem_error("size", file, std::make_error_code(std::errc::io_error));
  if (static_cast<std::uintmax_t>(length) > limit) return std::nullopt;
  input.seekg(0);
  if (!input) throw fs::filesystem_error("seek", file, std::make_error_code(std::errc::io_error));
  std::string bytes(static_cast<std::size_t>(length), '\0');
  if (!input.read(bytes.data(), length)) {
    throw fs::filesystem_error("read", file, std::make_error_code(std::errc::io_error));
  }
  return bytes;
}

Json parse_json(std::string_view bytes) {
  if (bytes.find('\0') != std::string_view::npos) return Json(Json::value_t::discarded);
  std::vector<std::set<std::string>> keys;
  bool duplicate = false;
  const auto json = Json::parse(
      bytes,
      [&](int depth, Json::parse_event_t event, Json& value) {
        using Event = Json::parse_event_t;
        if (depth > 16) throw std::length_error("project JSON is too deeply nested");
        if (event == Event::object_start) keys.emplace_back();
        if (event == Event::object_end) keys.pop_back();
        if (event == Event::key && !keys.back().insert(value.get<std::string>()).second) {
          duplicate = true;
        }
        return true;
      },
      false);
  return duplicate ? Json(Json::value_t::discarded) : json;
}

Json read_json(const fs::path& file) {
  const auto bytes = read_bytes(file, max_manifest_bytes);
  return bytes ? parse_json(*bytes) : Json(Json::value_t::discarded);
}

bool has_fields(const Json& json, std::initializer_list<std::string_view> fields) {
  return json.is_object() && json.size() == fields.size() &&
         std::ranges::all_of(fields, [&](auto field) { return json.contains(field); });
}

ProjectResult validate_manifest(const fs::path& root, const Json& json) {
  if (!json.is_object() || !json.contains("format_version") ||
      !json["format_version"].is_number_integer())
    return failure("invalid_manifest", "/format_version");
  if (json["format_version"] != 1) return failure("unsupported_version", "/format_version");
  if (!has_fields(json, {"format_version", "project_id", "revision", "entry_scene", "resources"})) {
    return failure("invalid_manifest", "/");
  }
  if (!json["project_id"].is_string() ||
      !valid_id(json["project_id"].get_ref<const std::string&>())) {
    return failure("invalid_manifest", "/project_id");
  }
  if (!json["revision"].is_number_unsigned() ||
      json["revision"].get<std::uint64_t>() > max_revision) {
    return failure("invalid_manifest", "/revision");
  }
  if (!json["entry_scene"].is_string() || !json["resources"].is_array()) {
    return failure("invalid_manifest",
                   !json["entry_scene"].is_string() ? "/entry_scene" : "/resources");
  }
  Project project{json["project_id"].get<std::string>(),
                  json["revision"].get<std::uint64_t>(),
                  json["entry_scene"].get<std::string>(),
                  {}};
  std::map<std::string, std::size_t> ids;
  std::set<std::string> paths;
  for (const auto& row : json["resources"]) {
    const auto where = "/resources/" + std::to_string(project.resources.size());
    if (!has_fields(row, {"id", "kind", "path", "refs"}) || !row["id"].is_string() ||
        !row["kind"].is_string() || !row["path"].is_string() || !row["refs"].is_array() ||
        !std::ranges::all_of(row["refs"], [](const auto& ref) { return ref.is_string(); }))
      return failure("invalid_manifest", where);
    Resource resource{row["id"].get<std::string>(), row["kind"].get<std::string>(),
                      row["path"].get<std::string>(), row["refs"].get<std::vector<std::string>>()};
    if (!valid_id(resource.id)) return failure("invalid_manifest", where + "/id");
    if (resource.kind != "scene" && resource.kind != "data")
      return failure("invalid_manifest", where + "/kind");
    if (!valid_path(resource.path)) return failure("invalid_path", where + "/path");
    if (!ids.emplace(resource.id, project.resources.size()).second)
      return failure("duplicate_id", where + "/id");
    if (!paths.insert(resource.path).second) return failure("duplicate_path", where + "/path");
    project.resources.push_back(std::move(resource));
  }
  const auto scene = ids.find(project.entry_scene);
  if (scene == ids.end() || project.resources[scene->second].kind != "scene") {
    return failure("invalid_entry_scene", "/entry_scene");
  }
  for (std::size_t i = 0; i < project.resources.size(); ++i) {
    const auto& resource = project.resources[i];
    for (std::size_t ref = 0; ref < resource.refs.size(); ++ref) {
      if (!ids.contains(resource.refs[ref])) {
        return failure("missing_reference",
                       "/resources/" + std::to_string(i) + "/refs/" + std::to_string(ref));
      }
    }
  }
  for (const auto& resource : project.resources) {
    const auto state = project_path(root, fs::path(resource.path));
    if (state != PathState::ok) {
      return failure(state == PathState::missing ? "missing_file" : "unsafe_path", resource.path);
    }
    if (!std::ifstream(root / resource.path, std::ios::binary))
      return failure("io_error", resource.path);
  }
  return {std::move(project), "ok", ""};
}

bool write_file(const fs::path& file, std::string_view content) {
  std::ofstream output(file, std::ios::binary);
  output << content;
  output.close();
  return output.good();
}

bool replace_file(const fs::path& root, const fs::path& target, std::string_view bytes) {
  const auto staging = root / staging_name;
  if (project_path(root, staging_name) != PathState::missing || !write_file(staging, bytes)) {
    return false;
  }
#ifdef _WIN32
  return MoveFileExW(staging.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
#else
  std::error_code error;
  fs::rename(staging, target, error);
  return !error;
#endif
}

bool remove_reserved(const fs::path& root, const char* name) {
  const auto state = project_path(root, name);
  if (state == PathState::missing) return true;
  std::error_code error;
  return state == PathState::ok && fs::remove(root / name, error) && !error;
}

bool pending_edit(const fs::path& root) {
  return project_path(root, journal_name) != PathState::missing ||
         project_path(root, staging_name) != PathState::missing;
}

bool valid_field(std::string_view field) {
  return !field.empty() && field.size() <= 64 && lower(field.front()) &&
         std::ranges::all_of(
             field, [](char ch) { return lower(ch) || digit(ch) || ch == '-' || ch == '_'; });
}

std::optional<std::int64_t> integer(const Json& value) {
  const auto limit = static_cast<std::int64_t>(max_revision);
  if (value.is_number_unsigned()) {
    const auto v = value.get<std::uint64_t>();
    return v <= max_revision ? std::optional(static_cast<std::int64_t>(v)) : std::nullopt;
  }
  if (!value.is_number_integer()) return std::nullopt;
  const auto v = value.get<std::int64_t>();
  return v >= -limit && v <= limit ? std::optional(v) : std::nullopt;
}

bool editable_data(const Json& value) {
  if (value.is_number()) return integer(value).has_value();
  if (value.is_structured()) return std::ranges::all_of(value, editable_data);
  return !value.is_discarded();
}

const Resource* find_data(const Project& project, std::string_view id) {
  const auto it = std::ranges::find(project.resources, id, &Resource::id);
  return it == project.resources.end() || it->kind != "data" ? nullptr : &*it;
}

const Resource& entry_resource(const Project& project) {
  return *std::ranges::find(project.resources, project.entry_scene, &Resource::id);
}

FieldResult parse_scene(const Json& json, std::vector<SceneEntity>& output) {
  // Older projects use an empty object for a new scene.
  if (json.is_object() && json.empty()) return {"ok"};
  if (!json.is_object() || !json.contains("format_version") ||
      !json["format_version"].is_number_integer())
    return {"invalid_scene", "/format_version"};
  if (json["format_version"] != 1) return {"unsupported_scene_version", "/format_version"};
  if (!has_fields(json, {"format_version", "entities"})) return {"invalid_scene", "/"};
  const auto& entities = json["entities"];
  if (!entities.is_array()) return {"invalid_scene", "/entities"};
  if (entities.size() > max_scene_entities) return {"scene_too_large", "/entities"};

  std::set<std::string> ids;
  for (const auto& row : entities) {
    const auto where = "/entities/" + std::to_string(output.size());
    if (!row.is_object() || !row.contains("id") || !row["id"].is_string() ||
        !valid_id(row["id"].get_ref<const std::string&>()))
      return {"invalid_scene", where + "/id"};
    SceneEntity entity{row["id"].get<std::string>()};
    if (!ids.insert(entity.id).second) return {"duplicate_entity_id", where + "/id"};
    for (auto it = row.begin(); it != row.end(); ++it) {
      if (it.key() == "id") continue;
      const auto field = std::ranges::find(scene_fields, it.key(), &SceneField::name);
      if (field == scene_fields.end()) return {"invalid_scene", where};
      const auto value = integer(*it);
      if (!value || *value < -scene_field_limit || *value > scene_field_limit)
        return {"invalid_scene", where + "/" + it.key()};
      entity.*(field->member) = *value;
    }
    output.push_back(std::move(entity));
  }
  return {"ok"};
}

FieldResult change_scene(Json& json, const SceneEdit& edit) {
  std::vector<SceneEntity> entities;
  auto result = parse_scene(json, entities);
  if (result.code != "ok") return result;
  const auto entity = std::ranges::find(entities, edit.entity, &SceneEntity::id);
  if (entity == entities.end()) return {"invalid_entity", "/entities"};
  const auto field = std::ranges::find(scene_fields, edit.field, &SceneField::name);
  const auto index = static_cast<std::size_t>(entity - entities.begin());
  const auto where = "/entities/" + std::to_string(index) + "/" + edit.field;
  if (field == scene_fields.end()) return {"invalid_field", where};
  const auto old = (*entity).*(field->member);
  if (old != edit.value) json["entities"][index][edit.field] = edit.value;
  return {"ok", where, edit.revision, old, edit.value, old != edit.value};
}

FieldResult change_entities(Json& scene, const EntityEdit& edit) {
  const bool add = edit.operation == "add";
  const bool copy = edit.operation == "copy";
  const bool move = edit.operation == "move";
  if ((!add && !copy && !move && edit.operation != "remove") || !valid_id(edit.entity) ||
      (copy ? !valid_id(edit.target) : !edit.target.empty()) || (!move && edit.index != 0))
    return {"invalid_argument", "/input"};

  std::vector<SceneEntity> entities;
  auto result = parse_scene(scene, entities);
  if (result.code != "ok") return result;
  const auto match = std::ranges::find(entities, edit.entity, &SceneEntity::id);
  const auto index = static_cast<std::size_t>(match - entities.begin());
  const auto where = "/entities/" + std::to_string(index);
  if (!add && match == entities.end()) return {"invalid_entity", "/entities"};

  if (add || copy) {
    const auto& id = copy ? edit.target : edit.entity;
    if (std::ranges::find(entities, id, &SceneEntity::id) != entities.end())
      return {"duplicate_entity_id", "/entities"};
    if (entities.size() == max_scene_entities) return {"scene_too_large", "/entities"};
    if (scene.empty()) scene = Json{{"format_version", 1}, {"entities", Json::array()}};
    auto& rows = scene["entities"];
    auto row = copy ? rows[index] : Json::object();
    row["id"] = id;
    rows.push_back(std::move(row));
    return {"ok", "/entities/" + std::to_string(entities.size()), edit.revision, {}, {}, true};
  }

  auto& rows = scene["entities"];
  if (move && edit.index >= rows.size()) return {"invalid_argument", "/index"};
  if (move && edit.index == index) return {"ok", where, edit.revision};
  auto from = rows.begin() + static_cast<Json::difference_type>(index);
  if (move) {
    auto row = std::move(*from);
    rows.erase(from);
    rows.insert(rows.begin() + static_cast<Json::difference_type>(edit.index), std::move(row));
  } else {
    rows.erase(from);
  }
  return {"ok", where, edit.revision, {}, {}, true};
}

std::string commit_edit(const fs::path& root, const Resource& resource, Json journal,
                        const std::string& before_manifest, const std::string& before_data,
                        Json& manifest, const std::string& next_data, std::uint64_t revision,
                        bool apply) {
  manifest["revision"] = revision + 1;
  const auto next_manifest = manifest.dump(2) + "\n";
  if (next_manifest.size() > max_edit_bytes || next_data.size() > max_edit_bytes)
    return "edit_too_large";
  journal["resource"] = resource.id;
  journal["before_manifest"] = before_manifest;
  journal["after_manifest"] = next_manifest;
  journal["before_data"] = before_data;
  journal["after_data"] = next_data;
  const auto encoded = journal.dump() + "\n";
  if (encoded.size() > max_manifest_bytes) return "edit_too_large";
  if (apply && (!replace_file(root, root / journal_name, encoded) ||
                !replace_file(root, root / resource.path, next_data) ||
                !replace_file(root, root / manifest_name, next_manifest) ||
                !remove_reserved(root, journal_name)))
    return "recovery_required";
  return "ok";
}

template <typename Change>
FieldResult edit_scene_file(const fs::path& root, std::uint64_t expected, const Json& journal,
                            bool apply, bool capture_change, Change change,
                            const SceneChange* restore = nullptr) {
  if (expected > max_revision) return {"invalid_argument", "/revision"};
  const auto opened = open_project(root);
  if (!opened.project) return {opened.code, opened.location};
  const auto revision = opened.project->revision;
  if (expected != revision) return {"revision_conflict", "/revision", revision};
  const auto& resource = entry_resource(*opened.project);
  try {
    const auto manifest_bytes = read_bytes(root / manifest_name, max_edit_bytes);
    const auto scene_bytes = read_bytes(root / resource.path, max_edit_bytes);
    if (!manifest_bytes || !scene_bytes) return {"edit_too_large", resource.path, revision};
    if (restore && (restore->project_id != opened.project->id || restore->scene_id != resource.id ||
                    restore->resource_path != resource.path || restore->before != *scene_bytes))
      return {"scene_content_conflict", resource.path, revision};
    auto manifest = parse_json(*manifest_bytes);
    auto scene = parse_json(*scene_bytes);
    auto result = change(scene);
    result.revision = revision;
    if (result.code != "ok" || !result.changed) return result;
    if (revision == max_revision) return {"revision_exhausted", "/revision", revision};
    const auto next = restore ? restore->after : scene.dump(2) + "\n";
    const auto code = commit_edit(root, resource, journal, *manifest_bytes, *scene_bytes, manifest,
                                  next, revision, apply);
    if (code != "ok")
      return {code, code == "recovery_required" ? journal_name : resource.path, revision};
    if (apply) {
      result.revision = revision + 1;
      if (capture_change)
        result.scene_change =
            SceneChange{opened.project->id, resource.id, resource.path, *scene_bytes, next};
    }
    return result;
  } catch (const fs::filesystem_error&) {
    return {apply ? "recovery_required" : "io_error", resource.path};
  } catch (const std::length_error&) {
    return {"invalid_scene", "/", revision};
  } catch (const Json::exception&) {
    return {"invalid_scene", "/", revision};
  }
}

fs::path from_utf8(std::string_view text) {
  return fs::path(std::u8string(text.begin(), text.end()));
}

// 非加密摘要，用于让来源记录能对照同一批字节；一致性判断仍逐字节比较。
std::string digest64(std::string_view bytes) {
  std::uint64_t hash = 14695981039346656037ULL;
  for (const auto ch : bytes) {
    hash ^= static_cast<unsigned char>(ch);
    hash *= 1099511628211ULL;
  }
  static constexpr char hex[] = "0123456789abcdef";
  std::string text(16, '0');
  for (int index = 15; index >= 0; --index) {
    text[static_cast<std::size_t>(index)] = hex[hash & 0xF];
    hash >>= 4;
  }
  return "fnv1a64:" + text;
}

// 导入内容校验：场景要能被试玩读取，数据要能进入首版字段编辑。
std::string imported_content_code(std::string_view kind, std::string_view bytes) {
  try {
    const auto json = parse_json(bytes);
    if (json.is_discarded()) return "invalid_content";
    std::vector<SceneEntity> entities;
    if (kind == "scene") return parse_scene(json, entities).code == "ok" ? "ok" : "invalid_content";
    if (!json.is_object() || !editable_data(json)) return "invalid_content";
    return "ok";
  } catch (const std::length_error&) {
    return "invalid_content";
  } catch (const Json::exception&) {
    return "invalid_content";
  }
}

bool valid_provenance(const Json& json) {
  if (!json.is_object() || !has_fields(json, {"version", "imports"}) ||
      !json["version"].is_number_integer() || json["version"] != 1 || !json["imports"].is_array())
    return false;
  for (const auto& row : json["imports"]) {
    if (!has_fields(row,
                    {"resource", "kind", "path", "source_name", "bytes", "digest", "revision"}) ||
        !row["resource"].is_string() || !row["kind"].is_string() || !row["path"].is_string() ||
        !row["source_name"].is_string() || !row["digest"].is_string() ||
        !row["bytes"].is_number_unsigned() || !row["revision"].is_number_unsigned())
      return false;
    if (!valid_id(row["resource"].get_ref<const std::string&>()) ||
        (row["kind"] != "scene" && row["kind"] != "data") ||
        !valid_path(row["path"].get_ref<const std::string&>()))
      return false;
  }
  return true;
}

struct Provenance {
  std::string code = "missing";
  std::string bytes;
  Json json = Json{{"version", 1}, {"imports", Json::array()}};
};

Provenance read_provenance(const fs::path& root) {
  Provenance result;
  const auto state = project_path(root, imports_name);
  if (state == PathState::missing) return result;
  if (state != PathState::ok) {
    result.code = "unsafe_path";
    return result;
  }
  const auto bytes = read_bytes(root / imports_name, max_imports_bytes);
  if (!bytes) {
    result.code = "io_error";
    return result;
  }
  result.bytes = *bytes;
  auto json = parse_json(*bytes);
  if (!valid_provenance(json)) {
    result.code = "invalid_provenance";
    return result;
  }
  result.code = "ok";
  result.json = std::move(json);
  return result;
}

std::string provenance_with(const Provenance& current, const ImportRecord& record) {
  Json json = current.json;
  auto& rows = json["imports"];
  const auto match = std::ranges::find(
      rows, record.resource, [](const Json& row) { return row["resource"].get<std::string>(); });
  Json row{{"resource", record.resource},       {"kind", record.kind},   {"path", record.path},
           {"source_name", record.source_name}, {"bytes", record.bytes}, {"digest", record.digest},
           {"revision", record.revision}};
  if (match == rows.end())
    rows.push_back(std::move(row));
  else
    *match = std::move(row);
  return json.dump(2) + "\n";
}

bool write_or_remove(const fs::path& root, const char* name, std::string_view bytes) {
  if (bytes.empty()) return remove_reserved(root, name);
  return replace_file(root, root / name, bytes);
}

}  // namespace

ProjectResult open_project(const fs::path& directory) {
  try {
    if (node(directory, true) != PathState::ok) return failure("unsafe_path", ".");
    if (pending_edit(directory)) return failure("recovery_required", journal_name);
    const auto state = project_path(directory, manifest_name);
    if (state != PathState::ok) {
      return failure(state == PathState::missing ? "missing_manifest" : "unsafe_path",
                     manifest_name);
    }
    return validate_manifest(directory, read_json(directory / manifest_name));
  } catch (const std::length_error&) {
    return failure("invalid_manifest", "/");
  } catch (const Json::exception&) {
    return failure("invalid_manifest", "/");
  } catch (const fs::filesystem_error&) {
    return failure("io_error", ".");
  }
}

bool SceneSession::advance(std::uint64_t ticks) {
  if (ticks > max_ticks - state_.ticks) return false;
  const auto steps = static_cast<std::int64_t>(ticks);
  for (auto& entity : state_.entities) {
    entity.x += entity.vx * steps;
    entity.y += entity.vy * steps;
  }
  state_.ticks += ticks;
  return true;
}

SceneResult start_scene(const fs::path& directory, std::uint64_t expected_revision) {
  if (expected_revision > max_revision) return {std::nullopt, "invalid_argument", "/revision"};
  const auto opened = open_project(directory);
  if (!opened.project) return {std::nullopt, opened.code, opened.location};
  const auto& project = *opened.project;
  if (project.revision != expected_revision) {
    return {std::nullopt, "revision_conflict", "/revision", {}, project.revision};
  }

  const auto& scene = entry_resource(project);
  SceneSession session;
  session.state_ = {project.revision, project.entry_scene, 0, {}};
  try {
    const auto bytes = read_bytes(directory / scene.path, max_edit_bytes);
    if (!bytes) return {std::nullopt, "scene_too_large", "/", scene.path, project.revision};
    const auto parsed = parse_scene(parse_json(*bytes), session.state_.entities);
    if (parsed.code != "ok")
      return {std::nullopt, parsed.code, parsed.location, scene.path, project.revision};
    return {std::move(session), "ok"};
  } catch (const std::filesystem::filesystem_error&) {
    return {std::nullopt, "io_error", "/", scene.path, project.revision};
  } catch (const std::length_error&) {
    return {std::nullopt, "invalid_scene", "/", scene.path, project.revision};
  } catch (const Json::exception&) {
    return {std::nullopt, "invalid_scene", "/", scene.path, project.revision};
  }
}

ProjectResult create_project(const fs::path& directory, std::string_view id) {
  if (!valid_id(id)) return failure("invalid_manifest", "/project_id");
  const auto parent = directory.has_parent_path() ? directory.parent_path() : fs::path(".");
  try {
    const auto status = fs::symlink_status(directory);
    if (fs::exists(status) || fs::is_symlink(status)) return failure("already_exists", ".");
    if (node(parent, true) != PathState::ok) {
      return failure("unsafe_path", ".");
    }
    if (!fs::create_directory(directory)) return failure("already_exists", ".");
  } catch (const fs::filesystem_error&) {
    return failure("io_error", ".");
  }
  const auto assets = directory / "assets";
  const auto scene = assets / "intro.json";
  const auto staging = directory / "wds.project.json.tmp";
  const Json json{{"format_version", 1},
                  {"project_id", id},
                  {"revision", 0},
                  {"entry_scene", "intro"},
                  {"resources", Json::array({{{"id", "intro"},
                                              {"kind", "scene"},
                                              {"path", "assets/intro.json"},
                                              {"refs", Json::array()}}})}};
  std::error_code error;
  if (fs::create_directory(assets, error)) {
    if (write_file(scene, "{\"format_version\":1,\"entities\":[]}\n") &&
        write_file(staging, json.dump(2) + "\n")) {
      fs::rename(staging, directory / manifest_name, error);
      if (!error) return open_project(directory);
    }
    for (const auto& path : {staging, scene, assets}) fs::remove(path, error);
  }
  fs::remove(directory, error);
  auto result = failure("io_error", ".");
  result.cleanup_incomplete = fs::exists(directory, error) || static_cast<bool>(error);
  return result;
}

FieldResult edit_field(const fs::path& root, const FieldEdit& edit, bool apply) {
  const auto project = open_project(root);
  if (!project.project) return {project.code, project.location};
  const auto revision = project.project->revision;
  const auto bound = static_cast<std::int64_t>(max_revision);
  if (!valid_id(edit.resource) || !valid_field(edit.field) || edit.revision > max_revision ||
      edit.value < -bound || edit.value > bound)
    return {"invalid_argument", edit.field, revision};
  if (edit.revision != revision) return {"revision_conflict", "/revision", revision};
  const auto* resource = find_data(*project.project, edit.resource);
  if (!resource) return {"invalid_resource", edit.resource, revision};
  try {
    const auto manifest_bytes = read_bytes(root / manifest_name, max_edit_bytes);
    const auto data_bytes = read_bytes(root / resource->path, max_edit_bytes);
    if (!manifest_bytes || !data_bytes) return {"edit_too_large", resource->path, revision};
    auto manifest = parse_json(*manifest_bytes);
    auto data = parse_json(*data_bytes);
    if (!data.is_object() || !editable_data(data)) {
      return {"invalid_resource", resource->path, revision};
    }
    const auto field = data.find(edit.field);
    const auto old = field == data.end() ? std::nullopt : integer(*field);
    if (!old) return {"invalid_field", edit.field, revision};
    FieldResult result{"ok", edit.field, revision, old, edit.value, *old != edit.value};
    if (!result.changed) return result;
    if (revision == max_revision) return {"revision_exhausted", "/revision", revision};
    *field = edit.value;
    const auto code =
        commit_edit(root, *resource, {{"version", 1}, {"field", edit.field}}, *manifest_bytes,
                    *data_bytes, manifest, data.dump(2) + "\n", revision, apply);
    if (code != "ok")
      return {code, code == "recovery_required" ? journal_name : resource->path, revision};
    if (!apply) return result;
    result.revision = revision + 1;
    return result;
  } catch (const fs::filesystem_error&) {
    return {apply ? "recovery_required" : "io_error", resource->path};
  } catch (const std::length_error&) {
    return {"invalid_resource", resource->path, revision};
  } catch (const Json::exception&) {
    return {"invalid_resource", resource->path, revision};
  }
}

FieldResult edit_scene(const fs::path& root, const SceneEdit& edit, bool apply,
                       bool capture_change) {
  if (!valid_id(edit.entity) || !valid_field(edit.field) || edit.revision > max_revision ||
      edit.value < -scene_field_limit || edit.value > scene_field_limit)
    return {"invalid_argument", "/input"};
  return edit_scene_file(root, edit.revision,
                         {{"version", 2}, {"field", edit.field}, {"entity", edit.entity}}, apply,
                         capture_change, [&](Json& scene) { return change_scene(scene, edit); });
}

FieldResult edit_entity(const fs::path& root, const EntityEdit& edit, bool apply,
                        bool capture_change) {
  const Json operation{{"operation", edit.operation},
                       {"entity", edit.entity},
                       {"target", edit.target},
                       {"index", edit.index}};
  return edit_scene_file(root, edit.revision, {{"version", 3}, {"edit", operation}}, apply,
                         capture_change, [&](Json& scene) { return change_entities(scene, edit); });
}

FieldResult restore_scene(const fs::path& root, std::uint64_t revision, std::string_view request) {
  const FieldResult invalid{"invalid_argument", "/input"};
  if (request.size() > max_scene_restore_request) return invalid;
  SceneChange change;
  try {
    const auto json = parse_json(request);
    const auto fields = {"project_id", "scene_id", "resource_path", "before", "after"};
    if (!has_fields(json, {"project_id", "scene_id", "resource_path", "before", "after"}) ||
        !std::ranges::all_of(fields, [&](const auto* key) { return json[key].is_string(); }))
      return invalid;
    change = {json["project_id"].get<std::string>(), json["scene_id"].get<std::string>(),
              json["resource_path"].get<std::string>(), json["before"].get<std::string>(),
              json["after"].get<std::string>()};
  } catch (const std::length_error&) {
    return invalid;
  }
  if (!valid_id(change.project_id) || !valid_id(change.scene_id) ||
      !valid_path(change.resource_path) || change.before.size() > max_edit_bytes ||
      change.after.size() > max_edit_bytes)
    return invalid;
  return edit_scene_file(
      root, revision, {{"version", 4}}, true, false,
      [&](Json& current) -> FieldResult {
        std::vector<SceneEntity> entities;
        if (auto result = parse_scene(current, entities); result.code != "ok") return result;
        entities.clear();
        if (auto result = parse_scene(parse_json(change.after), entities); result.code != "ok")
          return result;
        return {"ok", "/", revision, {}, {}, change.before != change.after};
      },
      &change);
}

ImportResult import_resource(const fs::path& root, const ImportRequest& request, bool apply) {
  if (request.revision > max_revision) return {"invalid_argument", "/revision"};
  const auto opened = open_project(root);
  if (!opened.project) return {opened.code, opened.location};
  const auto& project = *opened.project;
  const auto revision = project.revision;
  if (request.revision != revision) return {"revision_conflict", "/revision", revision};
  if (!valid_id(request.id)) return {"invalid_argument", "/id", revision};
  if (request.kind != "scene" && request.kind != "data")
    return {"invalid_argument", "/kind", revision};
  if (!valid_path(request.path)) return {"invalid_path", "/path", revision};

  try {
    const auto source = from_utf8(request.source);
    if (node(source, false) != PathState::ok) return {"invalid_source", "/source", revision};
    const auto imported = read_bytes(source, max_edit_bytes);
    if (!imported) return {"source_too_large", "/source", revision};
    const auto digest = digest64(*imported);
    if (!request.expected_digest.empty() && request.expected_digest != digest)
      return {"source_changed", "/source", revision};
    const auto content = imported_content_code(request.kind, *imported);
    if (content != "ok") return {content, "/source", revision};

    const auto target = project_path(root, fs::path(request.path));
    if (target == PathState::unsafe) return {"unsafe_path", request.path, revision};
    const auto existing = std::ranges::find(project.resources, request.id, &Resource::id);
    if (existing != project.resources.end()) {
      // 同 ID 只有在路径、类型与字节都一致时才算同一份资源，否则保留已有资源。
      const auto current = existing->path == request.path
                               ? read_bytes(root / existing->path, max_edit_bytes)
                               : std::nullopt;
      if (!current || *current != *imported || existing->kind != request.kind)
        return {"duplicate_id", request.id, revision};
      return {"ok",
              request.path,
              revision,
              request.id,
              request.kind,
              request.path,
              imported->size(),
              false,
              portable(source.filename()),
              digest};
    }
    if (std::ranges::any_of(project.resources, [&](const Resource& resource) {
          return resource.path == request.path;
        }))
      return {"duplicate_path", request.path, revision};
    if (target == PathState::ok) return {"target_exists", request.path, revision};
    if (revision == max_revision) return {"revision_exhausted", "/revision", revision};

    const auto manifest_bytes = read_bytes(root / manifest_name, max_edit_bytes);
    if (!manifest_bytes) return {"io_error", manifest_name, revision};
    const auto provenance = read_provenance(root);
    if (provenance.code != "ok" && provenance.code != "missing")
      return {provenance.code, imports_name, revision};

    const ImportRecord record{
        request.id,       request.kind, request.path, portable(source.filename()),
        imported->size(), digest,       revision + 1};
    auto manifest = parse_json(*manifest_bytes);
    manifest["resources"].push_back({{"id", request.id},
                                     {"kind", request.kind},
                                     {"path", request.path},
                                     {"refs", Json::array()}});
    manifest["revision"] = revision + 1;
    const auto next_manifest = manifest.dump(2) + "\n";
    const auto next_provenance = provenance_with(provenance, record);
    if (next_manifest.size() > max_edit_bytes || next_provenance.size() > max_imports_bytes)
      return {"import_too_large", "/path", revision};

    ImportResult result{
        "ok",         request.path,     revision, request.id,         request.kind,
        request.path, imported->size(), true,     record.source_name, record.digest};
    if (!apply) return result;
    const Json journal{{"version", 5},
                       {"resource", request.id},
                       {"kind", request.kind},
                       {"path", request.path},
                       {"before_manifest", *manifest_bytes},
                       {"after_manifest", next_manifest},
                       {"before_provenance", provenance.bytes},
                       {"after_provenance", next_provenance},
                       {"after_data", *imported}};
    const auto encoded = journal.dump() + "\n";
    if (encoded.size() > max_manifest_bytes) return {"import_too_large", "/path", revision};
    if (!replace_file(root, root / journal_name, encoded) ||
        !replace_file(root, root / request.path, *imported) ||
        !replace_file(root, root / imports_name, next_provenance) ||
        !replace_file(root, root / manifest_name, next_manifest) ||
        !remove_reserved(root, journal_name))
      return {"recovery_required", journal_name};
    result.revision = revision + 1;
    return result;
  } catch (const fs::filesystem_error&) {
    return {"io_error", "/source"};
  } catch (const std::length_error&) {
    return {"invalid_content", "/source", revision};
  } catch (const Json::exception&) {
    return {"invalid_content", "/source", revision};
  }
}

ImportListResult list_imports(const fs::path& root) {
  const auto opened = open_project(root);
  if (!opened.project) return {opened.code, opened.location, {}};
  try {
    const auto provenance = read_provenance(root);
    if (provenance.code == "missing") return {"ok", "", {}};
    if (provenance.code != "ok") return {provenance.code, imports_name, {}};
    ImportListResult result{"ok", "", {}};
    for (const auto& row : provenance.json["imports"]) {
      result.records.push_back(
          {row["resource"].get<std::string>(), row["kind"].get<std::string>(),
           row["path"].get<std::string>(), row["source_name"].get<std::string>(),
           row["bytes"].get<std::uintmax_t>(), row["digest"].get<std::string>(),
           row["revision"].get<std::uint64_t>()});
    }
    return result;
  } catch (const fs::filesystem_error&) {
    return {"io_error", imports_name, {}};
  } catch (const std::length_error&) {
    return {"invalid_provenance", imports_name, {}};
  } catch (const Json::exception&) {
    return {"invalid_provenance", imports_name, {}};
  }
}

namespace {

// 导入中断恢复：清单是提交点，目标是本次新建的文件与来源记录。
ProjectResult recover_import(const fs::path& root, const Json& journal) {
  const auto blocked = [] { return failure("recovery_required", journal_name); };
  if (!has_fields(journal,
                  {"version", "resource", "kind", "path", "before_manifest", "after_manifest",
                   "before_provenance", "after_provenance", "after_data"}))
    return blocked();
  for (const auto* key : {"resource", "kind", "path", "before_manifest", "after_manifest",
                          "before_provenance", "after_provenance", "after_data"}) {
    if (!journal[key].is_string()) return blocked();
  }
  const auto text = [&journal](const char* key) -> const std::string& {
    return journal[key].get_ref<const std::string&>();
  };
  const auto& id = text("resource");
  const auto& kind = text("kind");
  const auto& path = text("path");
  const auto& before_manifest = text("before_manifest");
  const auto& after_manifest = text("after_manifest");
  const auto& before_provenance = text("before_provenance");
  const auto& after_provenance = text("after_provenance");
  const auto& after_data = text("after_data");
  if (!valid_id(id) || (kind != "scene" && kind != "data") || !valid_path(path) ||
      before_manifest.size() > max_edit_bytes || after_manifest.size() > max_edit_bytes ||
      before_provenance.size() > max_imports_bytes || after_provenance.size() > max_imports_bytes ||
      after_data.size() > max_edit_bytes || after_provenance.empty())
    return blocked();
  if (imported_content_code(kind, after_data) != "ok") return blocked();

  try {
    auto previous = parse_json(before_manifest);
    const auto before_project = validate_manifest(root, previous);
    if (!before_project.project || before_project.project->revision == max_revision)
      return blocked();
    const auto& resources = before_project.project->resources;
    if (std::ranges::any_of(resources, [&](const Resource& resource) {
          return resource.id == id || resource.path == path;
        }))
      return blocked();
    auto expected = previous;
    expected["revision"] = before_project.project->revision + 1;
    expected["resources"].push_back(
        {{"id", id}, {"kind", kind}, {"path", path}, {"refs", Json::array()}});
    if (expected != parse_json(after_manifest)) return blocked();

    if (!valid_provenance(parse_json(after_provenance))) return blocked();
    if (!before_provenance.empty()) {
      const auto before = parse_json(before_provenance);
      if (!valid_provenance(before) || std::ranges::any_of(before["imports"], [&](const Json& row) {
            return row["resource"] == id;
          }))
        return blocked();
    }

    const auto data_state = project_path(root, path);
    if (data_state == PathState::unsafe) return blocked();
    const bool data_exists = data_state == PathState::ok;
    std::string actual_data;
    if (data_exists) {
      const auto bytes = read_bytes(root / path, max_edit_bytes);
      if (!bytes) return blocked();
      actual_data = *bytes;
    }
    const auto actual_manifest = read_bytes(root / manifest_name, max_edit_bytes);
    if (!actual_manifest) return blocked();
    const auto provenance = read_provenance(root);
    if (provenance.code != "ok" && provenance.code != "missing") return blocked();

    const bool committed = *actual_manifest == after_manifest;
    const bool uncommitted = *actual_manifest == before_manifest;
    if (!committed && !uncommitted) return blocked();
    // 提交后清单已发布，目标文件必须是本次导入的字节；提交前只允许本次写入的字节存在。
    if (committed && (!data_exists || actual_data != after_data)) return blocked();
    if (uncommitted && data_exists && actual_data != after_data) return blocked();
    if (!remove_reserved(root, staging_name)) return blocked();
    if (uncommitted && data_exists) {
      std::error_code error;
      if (!fs::remove(root / path, error) || error) return blocked();
    }
    if (!write_or_remove(
            root, imports_name,
            committed ? std::string_view(after_provenance) : std::string_view(before_provenance)))
      return blocked();
    if (!remove_reserved(root, journal_name)) return blocked();
    return open_project(root);
  } catch (const fs::filesystem_error&) {
    return blocked();
  } catch (const std::length_error&) {
    return blocked();
  } catch (const Json::exception&) {
    return blocked();
  }
}

}  // namespace

ProjectResult recover_project(const fs::path& root) {
  const auto blocked = [] { return failure("recovery_required", journal_name); };
  try {
    if (node(root, true) != PathState::ok || project_path(root, manifest_name) != PathState::ok)
      return blocked();
    const auto journal_state = project_path(root, journal_name);
    if (journal_state == PathState::missing) {
      // Before journal publication, no resource or manifest can have been replaced.
      const auto checked = validate_manifest(root, read_json(root / manifest_name));
      if (!checked.project || !remove_reserved(root, staging_name)) return blocked();
      return checked;
    }
    if (journal_state != PathState::ok) return blocked();
    const auto journal = read_json(root / journal_name);
    if (!journal.is_object() || !journal.contains("version") ||
        !journal["version"].is_number_integer())
      return blocked();
    if (journal["version"] == 5) return recover_import(root, journal);
    const bool scene_edit = journal["version"] == 2;
    const bool entity_edit = journal["version"] == 3;
    const bool scene_restore = journal["version"] == 4;
    if (scene_restore) {
      if (!has_fields(journal, {"version", "resource", "before_manifest", "after_manifest",
                                "before_data", "after_data"}))
        return blocked();
    } else if (entity_edit) {
      if (!has_fields(journal, {"version", "resource", "edit", "before_manifest", "after_manifest",
                                "before_data", "after_data"}))
        return blocked();
    } else if (scene_edit) {
      if (!has_fields(journal, {"version", "resource", "field", "entity", "before_manifest",
                                "after_manifest", "before_data", "after_data"}) ||
          !journal["entity"].is_string() ||
          !valid_id(journal["entity"].get_ref<const std::string&>()))
        return blocked();
    } else if (journal["version"] != 1 ||
               !has_fields(journal, {"version", "resource", "field", "before_manifest",
                                     "after_manifest", "before_data", "after_data"})) {
      return blocked();
    }
    for (const auto* key :
         {"resource", "before_manifest", "after_manifest", "before_data", "after_data"}) {
      if (!journal[key].is_string() ||
          journal[key].get_ref<const std::string&>().size() > max_edit_bytes)
        return blocked();
    }
    const auto& id = journal["resource"].get_ref<const std::string&>();
    const auto& before_manifest = journal["before_manifest"].get_ref<const std::string&>();
    const auto& after_manifest = journal["after_manifest"].get_ref<const std::string&>();
    const auto& before_data = journal["before_data"].get_ref<const std::string&>();
    const auto& after_data = journal["after_data"].get_ref<const std::string&>();
    if (!valid_id(id)) return blocked();
    std::string field;
    if (!entity_edit && !scene_restore) {
      if (!journal["field"].is_string()) return blocked();
      field = journal["field"].get<std::string>();
      if (!valid_field(field)) return blocked();
    }
    auto previous = parse_json(before_manifest);
    const auto next = parse_json(after_manifest);
    const auto old_project = validate_manifest(root, previous);
    const auto new_project = validate_manifest(root, next);
    if (!old_project.project || !new_project.project ||
        old_project.project->revision == max_revision)
      return blocked();
    previous["revision"] = old_project.project->revision + 1;
    if (previous != next) return blocked();
    const auto* resource = (scene_edit || entity_edit || scene_restore)
                               ? &entry_resource(*old_project.project)
                               : find_data(*old_project.project, id);
    if (!resource || resource->id != id) return blocked();
    auto old_data = parse_json(before_data);
    const auto new_data = parse_json(after_data);
    if (scene_restore) {
      std::vector<SceneEntity> entities;
      if (before_data == after_data || parse_scene(old_data, entities).code != "ok")
        return blocked();
      entities.clear();
      if (parse_scene(new_data, entities).code != "ok") return blocked();
    } else if (entity_edit) {
      const auto& operation = journal["edit"];
      if (!has_fields(operation, {"operation", "entity", "target", "index"}) ||
          !operation["operation"].is_string() || !operation["entity"].is_string() ||
          !operation["target"].is_string() || !operation["index"].is_number_unsigned() ||
          operation["index"].get<std::uint64_t>() >= max_scene_entities)
        return blocked();
      const auto change = change_entities(
          old_data,
          {operation["operation"].get<std::string>(), operation["entity"].get<std::string>(),
           old_project.project->revision, operation["target"].get<std::string>(),
           operation["index"].get<std::size_t>()});
      if (change.code != "ok" || !change.changed) return blocked();
    } else if (scene_edit) {
      std::vector<SceneEntity> entities;
      if (parse_scene(new_data, entities).code != "ok") return blocked();
      const auto& entity_id = journal["entity"].get_ref<const std::string&>();
      const auto entity = std::ranges::find(entities, entity_id, &SceneEntity::id);
      const auto property = std::ranges::find(scene_fields, field, &SceneField::name);
      if (entity == entities.end() || property == scene_fields.end()) return blocked();
      const auto change = change_scene(old_data, {entity_id, field, old_project.project->revision,
                                                  (*entity).*(property->member)});
      if (change.code != "ok" || !change.changed) return blocked();
    } else {
      if (!old_data.is_object() || !new_data.is_object() || !editable_data(old_data) ||
          !editable_data(new_data) || !old_data.contains(field) || !new_data.contains(field) ||
          !integer(old_data[field]) || !integer(new_data[field]) ||
          old_data[field] == new_data[field])
        return blocked();
      old_data[field] = new_data[field];
    }
    if (!scene_restore && old_data != new_data) return blocked();

    const auto actual_manifest = read_bytes(root / manifest_name, max_edit_bytes);
    const auto actual_data = read_bytes(root / resource->path, max_edit_bytes);
    const bool committed = actual_manifest == after_manifest && actual_data == after_data;
    const bool uncommitted = actual_manifest == before_manifest &&
                             (actual_data == before_data || actual_data == after_data);
    if (!committed && !uncommitted) return blocked();
    if (!remove_reserved(root, staging_name)) return blocked();
    if (uncommitted && actual_data == after_data &&
        !replace_file(root, root / resource->path, before_data))
      return blocked();
    if (!remove_reserved(root, journal_name)) return blocked();
    return open_project(root);
  } catch (const std::length_error&) {
    return blocked();
  } catch (const Json::exception&) {
    return blocked();
  } catch (const fs::filesystem_error&) {
    return blocked();
  }
}

std::string field_result_json(const FieldResult& result) {
  Json json{{"code", result.code}, {"location", result.location}};
  if (result.revision) json["revision"] = *result.revision;
  if (result.old_value) json["old_value"] = *result.old_value;
  if (result.new_value) json["new_value"] = *result.new_value;
  if (result.code == "ok") json["changed"] = result.changed;
  if (result.scene_change) {
    const auto& change = *result.scene_change;
    json["scene_change"] = {{"project_id", change.project_id},
                            {"scene_id", change.scene_id},
                            {"resource_path", change.resource_path},
                            {"before", change.before},
                            {"after", change.after}};
  }
  return json.dump();
}

std::string import_result_json(const ImportResult& result) {
  Json json{{"code", result.code}, {"location", result.location}};
  if (result.revision) json["revision"] = *result.revision;
  if (!result.resource_id.empty()) {
    json["resource_id"] = result.resource_id;
    json["kind"] = result.kind;
    json["path"] = result.path;
    json["source_name"] = result.source_name;
    json["digest"] = result.digest;
    if (result.bytes) json["bytes"] = *result.bytes;
  }
  if (result.code == "ok") json["changed"] = result.changed;
  return json.dump();
}

std::string import_list_json(const ImportListResult& result) {
  if (result.code != "ok") return Json{{"code", result.code}, {"location", result.location}}.dump();
  auto items = Json::array();
  for (const auto& record : result.records) {
    items.push_back({{"resource", record.resource},
                     {"kind", record.kind},
                     {"path", record.path},
                     {"source_name", record.source_name},
                     {"bytes", record.bytes},
                     {"digest", record.digest},
                     {"revision", record.revision}});
  }
  return Json{{"code", "ok"}, {"count", result.records.size()}, {"imports", std::move(items)}}
      .dump();
}

std::string project_result_json(const ProjectResult& result) {
  if (!result.project) {
    return Json{{"code", result.code},
                {"location", result.location},
                {"cleanup_incomplete", result.cleanup_incomplete}}
        .dump();
  }
  const auto& project = *result.project;
  return Json{{"code", "ok"},
              {"project_id", project.id},
              {"revision", project.revision},
              {"entry_scene", project.entry_scene},
              {"resource_count", project.resources.size()}}
      .dump();
}

std::string project_resources_json(const ProjectResult& result) {
  if (!result.project) return project_result_json(result);
  const auto& project = *result.project;
  auto items = Json::array();
  for (const auto& resource : project.resources) {
    items.push_back({{"id", resource.id},
                     {"kind", resource.kind},
                     {"path", resource.path},
                     {"refs", resource.refs}});
  }
  return Json{{"code", "ok"},
              {"project_id", project.id},
              {"revision", project.revision},
              {"resources", std::move(items)}}
      .dump();
}

std::string scene_result_json(const SceneResult& result, bool include_fields) {
  if (!result.session) {
    Json json{{"code", result.code}, {"location", result.location}};
    if (!result.resource_path.empty()) json["resource_path"] = result.resource_path;
    if (result.revision) json["revision"] = *result.revision;
    return json.dump();
  }
  const auto& scene = result.session->snapshot();
  auto entities = Json::array();
  for (const auto& entity : scene.entities) {
    entities.push_back({{"id", entity.id},
                        {"x", entity.x},
                        {"y", entity.y},
                        {"vx", entity.vx},
                        {"vy", entity.vy}});
  }
  Json output{{"code", result.code},
              {"revision", scene.revision},
              {"scene_id", scene.scene_id},
              {"ticks", scene.ticks},
              {"entities", std::move(entities)}};
  if (include_fields) {
    auto fields = Json::array();
    for (const auto& field : scene_fields) {
      fields.push_back({{"id", field.name},
                        {"type", "integer"},
                        {"default", 0},
                        {"minimum", -scene_field_limit},
                        {"maximum", scene_field_limit}});
    }
    output["fields"] = std::move(fields);
  }
  return output.dump();
}

std::string project_operations_json() {
  auto operations = Json::array();
  const auto add = [&operations](std::string_view id, std::string_view command, bool writes,
                                 Json input, Json errors) {
    operations.push_back({{"id", id},
                          {"version", 1},
                          {"command", command},
                          {"writes_project", writes},
                          {"input", std::move(input)},
                          {"errors", std::move(errors)}});
  };
  const Json path_input{{"directory", "path"}};
  const Json read_errors = {
      "unsafe_path",       "missing_manifest", "invalid_manifest", "unsupported_version",
      "duplicate_id",      "duplicate_path",   "invalid_path",     "invalid_entry_scene",
      "missing_reference", "missing_file",     "io_error",         "recovery_required"};
  add("project.create", "project create DIRECTORY ID", true,
      {{"directory", "new path"}, {"project_id", "project identifier"}},
      {"already_exists", "invalid_manifest", "unsafe_path", "io_error"});
  add("project.open", "project open DIRECTORY", false, path_input, read_errors);
  add("project.validate", "project validate DIRECTORY", false, path_input, read_errors);
  add("project.resources", "project resources DIRECTORY", false, path_input, read_errors);
  add("project.recover", "project recover DIRECTORY", true, path_input, {"recovery_required"});
  auto import_errors = read_errors;
  for (const auto* code :
       {"invalid_argument", "invalid_path", "invalid_source", "source_too_large", "invalid_content",
        "duplicate_id", "duplicate_path", "target_exists", "revision_conflict",
        "revision_exhausted", "import_too_large", "invalid_provenance"})
    import_errors.push_back(code);
  const Json import_input{{"directory", "path"},
                          {"source", "external file read once, bytes preserved"},
                          {"resource_id", "new resource identifier"},
                          {"kind", "scene | data"},
                          {"path", "assets/ relative target path"},
                          {"expected_revision", "integer"}};
  add("resource.import.preview", "project preview-import DIRECTORY SOURCE ID KIND PATH REVISION",
      false, import_input, import_errors);
  auto apply_import_input = import_input;
  apply_import_input["expected_digest"] = "optional digest from preview";
  import_errors.push_back("source_changed");
  add("resource.import.apply",
      "project import DIRECTORY SOURCE ID KIND PATH REVISION [--expect-digest DIGEST]", true,
      std::move(apply_import_input), import_errors);
  auto provenance_errors = read_errors;
  provenance_errors.push_back("invalid_provenance");
  add("project.imports", "project imports DIRECTORY", false, path_input, provenance_errors);
  auto scene_errors = read_errors;
  for (const auto* code : {"invalid_argument", "revision_conflict", "invalid_scene",
                           "unsupported_scene_version", "scene_too_large", "duplicate_entity_id"})
    scene_errors.push_back(code);
  add("project.play", "project play DIRECTORY REVISION TICKS", false,
      {{"directory", "path"}, {"expected_revision", "integer"}, {"ticks", "integer 0 through 600"}},
      scene_errors);
  const Json edit_input{{"directory", "path"},
                        {"resource_id", "data resource identifier"},
                        {"field", "top-level field"},
                        {"expected_revision", "integer"},
                        {"value", "integer"}};
  auto edit_errors = read_errors;
  for (const auto* code : {"invalid_argument", "revision_conflict", "invalid_resource",
                           "invalid_field", "edit_too_large", "revision_exhausted"})
    edit_errors.push_back(code);
  add("resource.field.preview", "project preview-field DIRECTORY RESOURCE FIELD REVISION VALUE",
      false, edit_input, edit_errors);
  add("resource.field.set", "project set-field DIRECTORY RESOURCE FIELD REVISION VALUE", true,
      edit_input, edit_errors);
  add("scene.inspect", "project scene DIRECTORY REVISION", false,
      {{"directory", "path"}, {"expected_revision", "integer"}}, scene_errors);
  for (const auto* code :
       {"invalid_entity", "invalid_field", "edit_too_large", "revision_exhausted"})
    scene_errors.push_back(code);
  const Json scene_input{{"directory", "path"},
                         {"entity_id", "entity identifier"},
                         {"field", "scene field"},
                         {"expected_revision", "integer"},
                         {"value", "integer"}};
  add("scene.field.preview", "project preview-scene-field DIRECTORY ENTITY FIELD REVISION VALUE",
      false, scene_input, scene_errors);
  add("scene.field.set", "project set-scene-field DIRECTORY ENTITY FIELD REVISION VALUE", true,
      scene_input, scene_errors);
  const Json entity_input{{"directory", "path"},
                          {"operation", "add | copy | remove | move"},
                          {"entity_id", "entity identifier"},
                          {"expected_revision", "integer"},
                          {"argument", "new ID for copy, zero-based final index for move"}};
  add("scene.entity.preview", "project preview-entity DIRECTORY OPERATION ENTITY REVISION [ARG]",
      false, entity_input, scene_errors);
  add("scene.entity.apply", "project edit-entity DIRECTORY OPERATION ENTITY REVISION [ARG]", true,
      entity_input, scene_errors);
  scene_errors.push_back("scene_content_conflict");
  add("scene.restore", "project restore-scene DIRECTORY REVISION", true,
      {{"directory", "path"},
       {"expected_revision", "integer"},
       {"stdin", "scene_change JSON with expected before bytes and replacement after bytes"}},
      scene_errors);
  return Json{{"code", "ok"}, {"operations", std::move(operations)}}.dump();
}

}  // namespace wds::platform
