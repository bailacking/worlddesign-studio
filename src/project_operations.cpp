#include "project_operations.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <stdexcept>

#include "behavior_document.hpp"
#include "expedition.hpp"
#include "platform_project.hpp"
#include "project_diff.hpp"
#include "project_export.hpp"
#include "project_io.hpp"
#include "signal_circuit.hpp"

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

std::optional<int> actuator_tick(const Json& trace, std::string_view trigger, std::string_view gate,
                                 bool powered, bool rising) {
  auto event = trace.begin();
  while (event != trace.end()) {
    const auto tick = (*event)["tick"].get<int>();
    bool triggered = false;
    do {
      const auto& phase = (*event)["phase"];
      if (phase == "update" || phase == "input") {
        const auto& id = (*event)["id"].get_ref<const std::string&>();
        const bool high = (*event)["to"] != 0;
        if (id == gate) powered = high;
        if (id == trigger && high == rising && ((*event)["from"] == 0) == rising) triggered = true;
      }
      ++event;
    } while (event != trace.end() && (*event)["tick"] == tick);
    if (triggered && powered) return tick;
  }
  return {};
}

Json actuate(const fs::path& directory, std::uint64_t revision, bool apply, const Json& input) {
  const auto trigger = text(input, "trigger");
  const auto x = number<int>(input, "x"), y = number<int>(input, "y"), z = number<int>(input, "z");
  const auto dx = number<int>(input, "dx"), dz = number<int>(input, "dz");
  const auto capacity = input.contains("capacity") ? number<int>(input, "capacity") : 1;
  if (capacity < 1 || capacity > voxel::World::push_capacity)
    return fail("invalid_argument", "/input/capacity");
  const auto motion = input.contains("motion") ? text(input, "motion") : "push";
  if (motion != "push" && motion != "sticky-pull") return fail("invalid_argument", "/input/motion");
  const bool pull = motion == "sticky-pull";
  if (pull && capacity != 1) return fail("invalid_argument", "/input/capacity");
  if (pull && !((dx == 0 && (dz == -1 || dz == 1)) || (dz == 0 && (dx == -1 || dx == 1))))
    return fail("invalid_direction", "/input");
  if (!input.contains("circuit")) return fail("invalid_argument", "/input/circuit");
  const auto& circuit = input["circuit"];
  const auto replay = voxel::replay_circuit(circuit);
  if (replay["code"] != "ok")
    return fail("invalid_circuit",
                "/input/circuit" + replay["location"].get<std::string>().substr(6));
  if (!replay["levels"].contains(trigger)) return fail("invalid_argument", "/input/trigger");
  std::string gate;
  bool powered = true;
  if (input.contains("requires_high")) {
    gate = text(input, "requires_high");
    if (!replay["levels"].contains(gate)) return fail("invalid_argument", "/input/requires_high");
    powered = circuit.contains("resume") && circuit["resume"]["nodes"][gate]["level"] != 0;
  }
  auto loaded = start_scene(directory, revision, true);
  if (!loaded.session && !loaded.voxel_scene) return Json::parse(scene_result_json(loaded));
  if (!loaded.voxel_scene) return fail("unsupported_scene_kind", "/kind");
  const auto pulse = actuator_tick(replay["trace"], trigger, gate, powered, !pull);
  if (!pulse) return {{"code", "ok"}, {"revision", revision}, {"changed", false}};
  auto& scene = *loaded.voxel_scene;
  const int move_x = pull ? -dx : dx, move_z = pull ? -dz : dz;
  const auto result = scene.world.shift_blocks(x, y, z, move_x, move_z, capacity);
  if (result.code != "ok") return fail(result.code, "/input");
  auto saved = apply ? save_voxel_scene(directory, scene) : prepare_voxel_save(scene);
  saved.scene_change.reset();
  auto reply = Json::parse(field_result_json(saved));
  if (saved.code == "ok") {
    reply["tick"] = *pulse;
    reply["moved"] = result.moved;
    reply["from"] = {x, y, z};
    reply["to"] = {x + move_x, y, z + move_z};
  }
  return reply;
}

Json workyard(const fs::path& directory, std::uint64_t revision, std::string_view id, bool apply,
              const Json& input) {
  auto loaded = start_scene(directory, revision, true);
  if (!loaded.session && !loaded.voxel_scene) return Json::parse(scene_result_json(loaded));
  if (!loaded.voxel_scene) return fail("unsupported_scene_kind", "/kind");
  auto& scene = *loaded.voxel_scene;
  const auto x = number<int>(input, "x"), z = number<int>(input, "z");
  if (id == "scene.voxel.cart") {
    const voxel::Cell start{x, number<int>(input, "y"), z};
    voxel::CartRoute route;
    const auto moves = input.find("directions");
    if (moves != input.end()) {
      if (input.contains("dx") || input.contains("dz") || input.contains("steps") ||
          !moves->is_array() || moves->empty() || moves->size() > voxel::World::cart_max_steps)
        return fail("invalid_argument", "/input/directions");
      constexpr std::array<std::string_view, 4> names{"east", "west", "south", "north"};
      constexpr std::array offsets{std::array{1, 0}, std::array{-1, 0}, std::array{0, 1},
                                   std::array{0, -1}};
      std::vector<std::array<int, 2>> directions;
      for (const auto& move : *moves) {
        if (!move.is_string()) return fail("invalid_argument", "/input/directions");
        const auto found = std::ranges::find(names, move.get_ref<const std::string&>());
        if (found == names.end()) return fail("invalid_argument", "/input/directions");
        directions.push_back(offsets[static_cast<std::size_t>(found - names.begin())]);
      }
      route = scene.world.cart_route(start, directions);
    } else {
      route = scene.world.cart_route(start, number<int>(input, "dx"), number<int>(input, "dz"),
                                     number<int>(input, "steps"));
    }
    if (route.code != "ok") return fail(route.code, "/input");
    return {{"code", "ok"},
            {"revision", revision},
            {"positions", route.positions},
            {"blocked", route.blocked}};
  }
  std::optional<std::uint32_t> weather_tick;
  if (input.contains("weather_tick")) weather_tick = number<std::uint32_t>(input, "weather_tick");
  const auto change =
      scene.world.step_dynamics(x, z, number<int>(input, "width"), number<int>(input, "depth"),
                                number<int>(input, "steps"), weather_tick);
  if (change.code != "ok") return fail(change.code, "/input");
  auto saved = apply ? save_voxel_scene(directory, scene) : prepare_voxel_save(scene);
  saved.scene_change.reset();
  auto reply = Json::parse(field_result_json(saved));
  if (saved.code == "ok") {
    reply["water_updates"] = change.water_updates;
    reply["ignited"] = change.ignited;
    reply["extinguished"] = change.extinguished;
    if (weather_tick) reply["weather_extinguished"] = change.weather_extinguished;
    if (scene.world.terrain()->thermal_workyard()) {
      reply["lava_updates"] = change.lava_updates;
      reply["solidified"] = change.solidified;
    }
  }
  return reply;
}

Json dispatch(const fs::path& directory, std::string_view id, std::string_view mode,
              std::optional<std::uint64_t> expected, const Json& input) {
  const auto revision = expected.value_or(0);
  const bool apply = mode == "apply";
  if (id == "project.index") return project_index(directory, expected);
  if (id == "project.diff")
    return compare_projects(directory, revision, path(input, "other_directory"),
                            number<std::uint64_t>(input, "other_revision"));
  if (id == "scene.merge")
    return Json::parse(field_result_json(merge_project_scene(
        {directory, revision},
        {path(input, "base_directory"), number<std::uint64_t>(input, "base_revision")},
        {path(input, "other_directory"), number<std::uint64_t>(input, "other_revision")}, apply)));
  if (id == "project.merge") {
    const auto resources = input.find("resources");
    if (resources == input.end() || !resources->is_array() || resources->size() > 8 ||
        !std::ranges::all_of(*resources, &Json::is_string))
      return fail("invalid_argument", "/input/resources");
    const auto resolutions = input.find("resolutions");
    const auto token = input.contains("merge_token") ? text(input, "merge_token") : "";
    return Json::parse(merge_project_resources(
        {directory, revision},
        {path(input, "base_directory"), number<std::uint64_t>(input, "base_revision")},
        {path(input, "other_directory"), number<std::uint64_t>(input, "other_revision")},
        resources->get<std::vector<std::string>>(), apply,
        resolutions == input.end() ? "[]" : resolutions->dump(), token));
  }
  if (id == "project.restore") {
    const auto change = input.find("change");
    if (change == input.end() || !change->is_object())
      return fail("invalid_argument", "/input/change");
    return Json::parse(restore_project_resources({directory, revision}, change->dump(), apply));
  }
  if (id == "scene.voxel.actuate") return actuate(directory, revision, apply, input);
  if (id == "scene.voxel.cart" || id == "scene.voxel.dynamics")
    return workyard(directory, revision, id, apply, input);
  if (id == "expedition.replay" || id == "expedition.recipes") {
    auto loaded = expedition::load(directory, revision);
    if (!loaded.expedition) return fail(loaded.code, loaded.location);
    if (id == "expedition.recipes")
      return Json::parse(expedition::recipe_graph(*loaded.expedition));
    const auto actions = input.find("actions");
    if (actions == input.end()) return fail("invalid_argument", "/input/actions");
    const auto resume = input.find("resume");
    if (resume != input.end() && !resume->is_object())
      return fail("invalid_checkpoint", "/input/resume");
    return Json::parse(expedition::replay(std::move(*loaded.expedition), actions->dump(),
                                          resume == input.end() ? std::string{} : resume->dump(),
                                          flag(input, "checkpoint")));
  }
  if (id == "circuit.replay") return voxel::replay_circuit(input);
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
  if (id == "project.create") {
    const auto kind = input.contains("kind") ? text(input, "kind") : "scene";
    return Json::parse(
        project_result_json(create_project(directory, text(input, "project_id"), kind)));
  }
  if (id == "scene.palette.set")
    return Json::parse(
        field_result_json(edit_palette(directory, revision, text(input, "provider"), apply)));
  if (id == "project.export")
    return Json::parse(export_result_json(
        export_project(directory, revision, text(input, "target"), path(input, "destination"))));
  if (id == "scene.navigate") {
    const TilePoint from{number<int>(input, "from_x"), number<int>(input, "from_y")};
    const TilePoint to{number<int>(input, "to_x"), number<int>(input, "to_y")};
    if (!TileWorld::inside(from) || !TileWorld::inside(to))
      return fail("invalid_argument", "/input");
    auto loaded = start_scene(directory, revision);
    if (!loaded.session) return Json::parse(scene_result_json(loaded));
    const auto& terrain = loaded.session->terrain();
    if (!terrain) return fail("invalid_terrain", "/terrain");
    auto route = Json::array();
    for (const auto tile : terrain->route(from, to)) route.push_back({tile.x, tile.y});
    return {{"code", "ok"},
            {"revision", revision},
            {"visible", terrain->visible(from, to)},
            {"route", std::move(route)}};
  }
  if (id == "scene.landscape.sample") {
    const auto x = number<int>(input, "x");
    const auto z = number<int>(input, "z");
    constexpr auto limit = voxel::SeededTerrain::limit;
    if (x < -limit || x >= limit || z < -limit || z >= limit)
      return fail("invalid_argument", "/input");
    auto loaded = start_scene(directory, revision, true);
    if (!loaded.voxel_scene && !loaded.session) return Json::parse(scene_result_json(loaded));
    if (!loaded.voxel_scene) return fail("unsupported_scene_kind", "/kind");
    auto& world = loaded.voxel_scene->world;
    const auto& terrain = world.terrain();
    if (!terrain || !terrain->frontier()) return fail("invalid_voxel_world", "/terrain/profile");
    world.stream({static_cast<double>(x), 0, static_cast<double>(z)});
    auto blocks = Json::array();
    for (int y = 0; y < voxel::World::height; ++y)
      blocks.push_back(voxel::block_name(world.block(x, y, z)));
    return {{"code", "ok"},
            {"revision", revision},
            {"biome", terrain->biome(x, z)},
            {"landmark", terrain->landmark(x, z)},
            {"blocks", std::move(blocks)}};
  }
  if (id == "scene.landscape.environment") {
    const auto x = number<int>(input, "x"), y = number<int>(input, "y"),
               z = number<int>(input, "z");
    const auto tick = number<std::uint32_t>(input, "tick");
    const auto loaded = start_scene(directory, revision, true);
    if (!loaded.session && !loaded.voxel_scene) return Json::parse(scene_result_json(loaded));
    if (!loaded.voxel_scene || !loaded.voxel_scene->world.terrain())
      return fail("unsupported_scene_kind", "/terrain");
    const auto sample = loaded.voxel_scene->world.environment(x, y, z, tick);
    if (!sample) return fail("invalid_argument", "/input");
    return {{"code", "ok"},
            {"revision", revision},
            {"weather", sample->weather},
            {"open_sky", sample->open_sky},
            {"vertical_light", sample->vertical_light},
            {"light", sample->light}};
  }
  if (id == "scene.character.set") {
    const auto optional = [&](const char* key) -> std::optional<int> {
      return input.contains(key) ? std::optional(number<int>(input, key)) : std::nullopt;
    };
    return Json::parse(
        field_result_json(edit_character(directory, revision, text(input, "id"), optional("speed"),
                                         optional("sight"), apply, flag(input, "capture_change"))));
  }
  if (id == "scene.tile.set") {
    const TileEdit edit{
        {number<int>(input, "x"), number<int>(input, "y")}, text(input, "kind"), revision};
    return Json::parse(
        field_result_json(edit_tile(directory, edit, apply, flag(input, "capture_change"))));
  }
  if (id == "scene.inspect" || id == "project.play") {
    const bool inspect = id == "scene.inspect";
    const auto ticks = inspect ? 0 : number<std::uint64_t>(input, "ticks");
    if (ticks > SceneSession::max_ticks) return fail("invalid_argument", "/ticks");
    auto loaded = start_scene(directory, revision, true);
    SceneInput keys;
    if (const auto given = input.find("keys"); given != input.end()) {
      if (!given->is_array() || given->size() > keys.size()) throw InvalidInput{"/input/keys"};
      for (const auto& item : *given) {
        const auto key =
            item.is_string() ? scene_key(item.get_ref<const std::string&>()) : std::nullopt;
        if (!key || keys.test(static_cast<std::size_t>(*key))) throw InvalidInput{"/input/keys"};
        keys.set(static_cast<std::size_t>(*key));
      }
    }
    if (loaded.session && ticks) loaded.session->advance(ticks, keys);
    auto result = Json::parse(scene_result_json(loaded, inspect));
    if (!inspect && loaded.voxel_scene) {
      voxel::Player player;
      const auto direction = [&](std::string_view positive, std::string_view negative) {
        return static_cast<int>(keys.test(static_cast<std::size_t>(*scene_key(positive)))) -
               static_cast<int>(keys.test(static_cast<std::size_t>(*scene_key(negative))));
      };
      for (std::uint64_t i = 0; i < 2 * ticks; ++i)
        player.step(loaded.voxel_scene->world, direction("W", "S"), direction("D", "A"), false,
                    1.0 / 60.0);
      result["ticks"] = ticks;
      result["position"] = {player.feet.x, player.feet.y, player.feet.z};
    }
    return result;
  }
  if (id.starts_with("resource.field.")) {
    FieldEdit edit{text(input, "resource_id"), text(input, "field"), revision,
                   number<std::int64_t>(input, "value")};
    if (input.contains("expected_value"))
      edit.expected_value = number<std::int64_t>(input, "expected_value");
    return Json::parse(field_result_json(edit_field(directory, edit, apply)));
  }
  if (id.starts_with("scene.field.")) {
    SceneEdit edit{text(input, "entity_id"), text(input, "field"), revision,
                   number<std::int64_t>(input, "value")};
    if (input.contains("expected_value"))
      edit.expected_value = number<std::int64_t>(input, "expected_value");
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
