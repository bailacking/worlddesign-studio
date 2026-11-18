#include "project_operations.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <stdexcept>

#include "behavior_document.hpp"
#include "platform_project.hpp"
#include "project_export.hpp"
#include "project_io.hpp"

namespace wds::platform {
namespace {

using Json = nlohmann::json;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

struct InvalidInput {
  std::string location;
};

Json fail(std::string_view code, std::string_view location) {
  return {{"code", code}, {"location", location}};
}

std::string text(const Json& input, const char* key) {
  const auto value = input.find(key);
  if (value == input.end() || !value->is_string()) throw InvalidInput{std::string("/input/") + key};
  auto result = value->get<std::string>();
  if (result.find('\0') != std::string::npos) throw InvalidInput{std::string("/input/") + key};
  return result;
}

template <typename Int>
Int number(const Json& object, const char* key, std::string_view prefix = "/input/") {
  const auto item = object.find(key);
  if (item != object.end() && item->is_number_integer()) {
    const auto literal = item->dump();
    Int value{};
    const auto parsed = std::from_chars(literal.data(), literal.data() + literal.size(), value);
    if (parsed.ec == std::errc{} && parsed.ptr == literal.data() + literal.size()) return value;
  }
  throw InvalidInput{std::string(prefix) + key};
}

bool flag(const Json& input, const char* key) {
  if (!input.contains(key)) return false;
  if (!input[key].is_boolean()) throw InvalidInput{std::string("/input/") + key};
  return input[key].get<bool>();
}

fs::path path(const Json& input, const char* key) {
  const auto utf8 = text(input, key);
  return fs::path(std::u8string(utf8.begin(), utf8.end()));
}

const Json* find_operation(std::string_view id) {
  const auto& operations = detail::operation_specs();
  const auto found = std::ranges::find_if(operations, [id](const Json& spec) {
    return spec["id"].get_ref<const std::string&>() == id;
  });
  return found == operations.end() ? nullptr : &*found;
}

Json project_index(const fs::path& directory, std::optional<std::uint64_t> expected) {
  const auto opened = open_project(directory);
  if (!opened.project) return Json::parse(project_result_json(opened));
  const auto revision = opened.project->revision;
  if (expected && *expected != revision)
    return {{"code", "revision_conflict"}, {"location", "/revision"}, {"revision", revision}};

  auto result = Json::parse(project_resources_json(opened));
  result["entry_scene"] = opened.project->entry_scene;
  result["scene"] = Json::parse(scene_result_json(start_scene(directory, revision, true), true));
  result["imports"] = Json::parse(import_list_json(list_imports(directory)));
  const auto current = open_project(directory);
  if (!current.project) return Json::parse(project_result_json(current));
  if (current.project->revision != revision)
    return {{"code", "revision_conflict"},
            {"location", "/revision"},
            {"revision", current.project->revision}};
  return result;
}

Json dispatch(const fs::path& directory, std::string_view id, std::string_view mode,
              std::optional<std::uint64_t> expected, const Json& input) {
  const auto revision = expected.value_or(0);
  const bool apply = mode == "apply";
  if (id == "project.index") return project_index(directory, expected);
  if (id == "behavior.convert") {
    const auto document = input.find("document");
    if (document == input.end()) throw InvalidInput{"/input/document"};
    return behavior::convert_document(text(input, "from"), text(input, "to"), *document);
  }
  if (id == "project.open" || id == "project.validate")
    return Json::parse(project_result_json(open_project(directory)));
  if (id == "project.resources")
    return Json::parse(project_resources_json(open_project(directory)));
  if (id == "project.imports") return Json::parse(import_list_json(list_imports(directory)));
  if (id == "project.recover") return Json::parse(project_result_json(recover_project(directory)));
  if (id == "project.create")
    return Json::parse(project_result_json(create_project(directory, text(input, "project_id"))));
  if (id == "project.export")
    return Json::parse(export_result_json(
        export_project(directory, revision, text(input, "target"), path(input, "destination"))));
  if (id == "scene.inspect" || id == "project.play") {
    const bool inspect = id == "scene.inspect";
    const auto ticks = inspect ? 0 : number<std::uint64_t>(input, "ticks");
    if (ticks > SceneSession::max_ticks) return fail("invalid_argument", "/ticks");
    auto loaded = start_scene(directory, revision, inspect);
    if (loaded.session && ticks) loaded.session->advance(ticks);
    return Json::parse(scene_result_json(loaded, inspect));
  }
  if (id.starts_with("resource.field.")) {
    const FieldEdit edit{text(input, "resource_id"), text(input, "field"), revision,
                         number<std::int64_t>(input, "value")};
    return Json::parse(field_result_json(edit_field(directory, edit, apply)));
  }
  if (id.starts_with("scene.field.")) {
    const SceneEdit edit{text(input, "entity_id"), text(input, "field"), revision,
                         number<std::int64_t>(input, "value")};
    return Json::parse(
        field_result_json(edit_scene(directory, edit, apply, flag(input, "capture_change"))));
  }
  if (id.starts_with("scene.entity.")) {
    EntityEdit edit{text(input, "operation"), text(input, "entity_id"), revision};
    if (edit.operation == "copy")
      edit.target = text(input, "argument");
    else if (edit.operation == "move")
      edit.index = number<std::size_t>(input, "argument");
    else if (input.contains("argument"))
      throw InvalidInput{"/input/argument"};
    return Json::parse(
        field_result_json(edit_entity(directory, edit, apply, flag(input, "capture_change"))));
  }
  if (id.starts_with("resource.import.")) {
    ImportRequest request{text(input, "source"), text(input, "resource_id"), text(input, "kind"),
                          text(input, "path"), revision};
    if (input.contains("expected_digest")) {
      request.expected_digest = text(input, "expected_digest");
      if (request.expected_digest.empty()) return fail("invalid_argument", "/expected_digest");
    }
    return Json::parse(import_result_json(import_resource(directory, request, apply)));
  }
  if (id == "scene.restore") {
    const auto change = input.find("change");
    if (change == input.end() || !change->is_object()) throw InvalidInput{"/input/change"};
    return Json::parse(field_result_json(restore_scene(directory, revision, change->dump())));
  }
  return fail("unsupported_operation", "/operation");
}

}  // namespace

Json run_operation(const fs::path& directory, std::string_view operation, std::string_view mode,
                   std::optional<std::uint64_t> revision, const Json& input,
                   OperationAccess access) {
  const auto* spec = find_operation(operation);
  if (!spec) return fail("unsupported_operation", "/operation");
  const auto& modes = (*spec)["modes"];
  if (std::ranges::find(modes, Json(mode)) == modes.end()) return fail("invalid_mode", "/mode");
  if (!input.is_object()) return fail("invalid_argument", "/input");
  const auto& arguments = (*spec)["input"];
  if (operation != "project.index" &&
      arguments.contains("expected_revision") != revision.has_value())
    return fail("invalid_argument", "/revision");
  for (const auto& [key, value] : input.items()) {
    (void)value;
    if (key == "directory" || key == "expected_revision" || !arguments.contains(key))
      return fail("invalid_argument", "/input/" + key);
  }
  if (mode == "cancel") return fail("cancelled", "/mode");
  if (mode == "apply" && (((*spec)["writes_project"] == true && !access.project_write) ||
                          (spec->value("writes_destination", false) && !access.destination_write)))
    return fail("permission_denied", "/operation");
  try {
    return dispatch(directory, operation, mode, revision, input);
  } catch (const InvalidInput& error) {
    return fail("invalid_argument", error.location);
  } catch (const fs::filesystem_error&) {
    return fail("io_error", "/");
  } catch (const Json::exception&) {
    return fail("invalid_argument", "/input");
  }
}

Json request_operation(const fs::path& directory, std::string_view bytes, OperationAccess access) {
  const auto start = Clock::now();
  Json result = fail("invalid_argument", "/");
  std::string id;
  const auto handle = [&]() -> Json {
    if (bytes.size() > max_project_request) return fail("invalid_argument", "/");
    auto request = detail::parse_json(bytes);
    if (!request.is_object()) return fail("invalid_argument", "/");
    for (const auto& [key, value] : request.items()) {
      (void)value;
      if (key != "operation" && key != "version" && key != "mode" && key != "expected_revision" &&
          key != "input" && key != "request_id")
        return fail("invalid_argument", "/" + key);
    }
    if (number<std::uint64_t>(request, "version", "/") != 1)
      return fail("unsupported_operation_version", "/version");
    if (!request.contains("operation") || !request["operation"].is_string())
      return fail("invalid_argument", "/operation");
    if (!request.contains("mode") || !request["mode"].is_string())
      return fail("invalid_argument", "/mode");
    if (request.contains("request_id")) {
      if (!request["request_id"].is_string()) return fail("invalid_argument", "/request_id");
      const auto& value = request["request_id"].get_ref<const std::string&>();
      if (value.empty() || value.size() > 64 || !std::ranges::all_of(value, [](unsigned char c) {
            return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                   c == '-' || c == '_';
          }))
        return fail("invalid_argument", "/request_id");
      id = value;
    }
    std::optional<std::uint64_t> revision;
    if (request.contains("expected_revision"))
      revision = number<std::uint64_t>(request, "expected_revision", "/");
    return run_operation(directory, request["operation"].get_ref<const std::string&>(),
                         request["mode"].get_ref<const std::string&>(), revision,
                         request.value("input", Json::object()), access);
  };
  try {
    result = handle();
  } catch (const InvalidInput& error) {
    result = fail("invalid_argument", error.location);
  } catch (const std::length_error&) {
    result = fail("invalid_argument", "/");
  } catch (const Json::exception&) {
    result = fail("invalid_argument", "/");
  }
  if (!id.empty()) result["request_id"] = id;
  result["usage"] = {
      {"requests", 1},
      {"elapsed_ms", std::chrono::duration<double, std::milli>(Clock::now() - start).count()}};
  return result;
}

}  // namespace wds::platform
