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
constexpr std::uint64_t max_revision = 9007199254740991ULL;
constexpr std::uintmax_t max_manifest_bytes = 16 * 1024 * 1024;
constexpr std::size_t max_edit_bytes = 1024 * 1024;

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
    if (write_file(scene, "{}\n") && write_file(staging, json.dump(2) + "\n")) {
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
    manifest["revision"] = revision + 1;
    *field = edit.value;
    const auto next_manifest = manifest.dump(2) + "\n";
    const auto next_data = data.dump(2) + "\n";
    if (next_manifest.size() > max_edit_bytes || next_data.size() > max_edit_bytes) {
      return {"edit_too_large", resource->path, revision};
    }
    const Json journal{{"version", 1},
                       {"resource", edit.resource},
                       {"field", edit.field},
                       {"before_manifest", *manifest_bytes},
                       {"after_manifest", next_manifest},
                       {"before_data", *data_bytes},
                       {"after_data", next_data}};
    const auto encoded = journal.dump() + "\n";
    if (encoded.size() > max_manifest_bytes) return {"edit_too_large", resource->path, revision};
    if (!apply) return result;
    if (!replace_file(root, root / journal_name, encoded) ||
        !replace_file(root, root / resource->path, next_data) ||
        !replace_file(root, root / manifest_name, next_manifest) ||
        !remove_reserved(root, journal_name)) {
      return {"recovery_required", journal_name};
    }
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
    if (!has_fields(journal, {"version", "resource", "field", "before_manifest", "after_manifest",
                              "before_data", "after_data"}) ||
        !journal["version"].is_number_integer() || journal["version"] != 1)
      return blocked();
    for (const auto* key :
         {"resource", "field", "before_manifest", "after_manifest", "before_data", "after_data"}) {
      if (!journal[key].is_string() ||
          journal[key].get_ref<const std::string&>().size() > max_edit_bytes)
        return blocked();
    }
    const auto& id = journal["resource"].get_ref<const std::string&>();
    const auto& field = journal["field"].get_ref<const std::string&>();
    const auto& before_manifest = journal["before_manifest"].get_ref<const std::string&>();
    const auto& after_manifest = journal["after_manifest"].get_ref<const std::string&>();
    const auto& before_data = journal["before_data"].get_ref<const std::string&>();
    const auto& after_data = journal["after_data"].get_ref<const std::string&>();
    if (!valid_id(id) || !valid_field(field)) return blocked();
    auto previous = parse_json(before_manifest);
    const auto next = parse_json(after_manifest);
    const auto old_project = validate_manifest(root, previous);
    const auto new_project = validate_manifest(root, next);
    if (!old_project.project || !new_project.project ||
        old_project.project->revision == max_revision)
      return blocked();
    previous["revision"] = old_project.project->revision + 1;
    if (previous != next) return blocked();
    const auto* resource = find_data(*old_project.project, id);
    if (!resource) return blocked();
    auto old_data = parse_json(before_data);
    const auto new_data = parse_json(after_data);
    if (!old_data.is_object() || !new_data.is_object() || !editable_data(old_data) ||
        !editable_data(new_data) || !old_data.contains(field) || !new_data.contains(field) ||
        !integer(old_data[field]) || !integer(new_data[field]) ||
        old_data[field] == new_data[field])
      return blocked();
    old_data[field] = new_data[field];
    if (old_data != new_data) return blocked();

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
  return json.dump();
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
  return Json{{"code", "ok"}, {"operations", std::move(operations)}}.dump();
}

}  // namespace wds::platform
