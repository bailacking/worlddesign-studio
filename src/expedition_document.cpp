#include <algorithm>
#include <array>
#include <fstream>
#include <random>
#include <set>
#include <stdexcept>
#include <utility>

#include "asset_import.hpp"
#include "expedition.hpp"
#include "platform_project.hpp"
#include "project_io.hpp"
#include "workshop_document.hpp"

namespace wds::expedition {
namespace {

using Json = nlohmann::json;
namespace fs = std::filesystem;
namespace io = platform::detail;

struct Invalid {
  std::string location;
};

constexpr std::array habitats{"surface", "ember", "sky", "cavern", "ruins", "shore", "ocean"};
constexpr std::array kinds{"source", "portal", "enemy", "boss",  "raid", "trade",  "anchor",
                           "dock",   "repair", "tame",  "breed", "rest", "chapter"};

bool id(std::string_view word) {
  return !word.empty() && word.size() <= 48 && std::ranges::all_of(word, [](unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
  });
}

std::string token(const Json& row, const char* field, const std::string& location,
                  bool optional = false) {
  if (optional && !row.contains(field)) return {};
  if (!row.contains(field) || !row[field].is_string() ||
      !id(row[field].get_ref<const std::string&>()))
    throw Invalid{location + "/" + field};
  return row[field].get<std::string>();
}

std::int64_t number(const Json& value, std::int64_t low, std::int64_t high,
                    const std::string& location) {
  if (!value.is_number_integer() ||
      (value.is_number_unsigned() && value.get<std::uint64_t>() > static_cast<std::uint64_t>(high)))
    throw Invalid{location};
  const auto read = value.get<std::int64_t>();
  if (read < low || read > high) throw Invalid{location};
  return read;
}

int integer(const Json& row, const char* key, int low, int high, const std::string& location) {
  if (!row.contains(key)) throw Invalid{location + "/" + key};
  return static_cast<int>(number(row[key], low, high, location + "/" + key));
}

TilePoint point(const Json& value, int width, int height, const std::string& location) {
  if (!value.is_array() || value.size() != 2) throw Invalid{location};
  return {static_cast<int>(number(value[0], 0, width - 1, location + "/0")),
          static_cast<int>(number(value[1], 0, height - 1, location + "/1"))};
}

const Json& array(const Json& row, const char* key, std::size_t low, std::size_t high,
                  std::string_view location) {
  if (!row.contains(key) || !row[key].is_array() || row[key].size() < low || row[key].size() > high)
    throw Invalid{std::string(location) + "/" + key};
  return row[key];
}

Region region(const Json& row, const std::string& location, std::set<std::string>& site_ids) {
  const auto width = integer(row, "width", 4, 40, location);
  const auto height = integer(row, "height", 4, 30, location);
  if (!row.contains("spawn")) throw Invalid{location + "/spawn"};
  Region result{token(row, "id", location),
                token(row, "habitat", location),
                width,
                height,
                point(row["spawn"], width, height, location + "/spawn"),
                {},
                {}};
  if (row.contains("palette")) {
    const auto& shades = array(row, "palette", 4, 4, location);
    std::array<std::uint32_t, 4> colors{};
    for (std::size_t i = 0; i < colors.size(); ++i)
      colors[i] = static_cast<std::uint32_t>(
          number(shades[i], 0, 0xffffff, location + "/palette/" + std::to_string(i)));
    result.palette = colors;
  }
  if (std::ranges::find(habitats, result.habitat) == habitats.end())
    throw Invalid{location + "/habitat"};
  const auto& entries = array(row, "sites", 0, 96, location);
  std::set<TilePoint> occupied;
  for (std::size_t i = 0; i < entries.size(); ++i) {
    const auto& entry = entries[i];
    const auto path = location + "/sites/" + std::to_string(i);
    const auto key = token(entry, "id", path);
    const auto kind = token(entry, "kind", path);
    if (!site_ids.insert(key).second || std::ranges::find(kinds, kind) == kinds.end() ||
        !entry.contains("at"))
      throw Invalid{path};
    const auto where = point(entry["at"], width, height, path + "/at");
    if (!occupied.insert(where).second) throw Invalid{path + "/at"};
    const auto power = entry.contains("power") ? integer(entry, "power", 1, 20, path) : 1;
    result.sites.push_back({key, kind, where, token(entry, "target", path, true),
                            token(entry, "item", path, true), token(entry, "reward", path, true),
                            power});
  }
  return result;
}

void validate_references(const Rules& rules, const items::Catalog& catalog,
                         const std::set<std::string>& regions) {
  std::set<std::string> chapters{"departure"};
  for (const auto& area : rules.regions)
    for (const auto& site : area.sites)
      if (site.kind == "chapter") chapters.insert(site.id);
  for (const auto& area : rules.regions) {
    for (const auto& site : area.sites) {
      const auto path = "/regions/" + area.id + "/sites/" + site.id;
      if (!site.item.empty() && !catalog.items.contains(site.item)) throw Invalid{path + "/item"};
      if (!site.reward.empty() && !catalog.items.contains(site.reward))
        throw Invalid{path + "/reward"};
      if (site.kind == "portal" && (!regions.contains(site.target) || site.target == area.id))
        throw Invalid{path + "/target"};
      if (site.kind == "chapter" && !site.target.empty() &&
          (!chapters.contains(site.target) || site.target == site.id))
        throw Invalid{path + "/target"};
      if (site.kind == "source") {
        const auto found = catalog.sources.find(site.id);
        if (found == catalog.sources.end() || found->second.uses <= 0 ||
            found->second.yield.id != site.reward)
          throw Invalid{path + "/reward"};
      }
      if ((site.kind == "anchor" || site.kind == "repair" || site.kind == "trade" ||
           site.kind == "breed" || site.kind == "tame") &&
          site.item.empty())
        throw Invalid{path + "/item"};
      if (site.kind == "trade" && site.reward.empty()) throw Invalid{path + "/reward"};
    }
  }
}

void validate_icons(const Json& document) {
  if (!document.contains("icons")) return;
  const auto& icons = document["icons"];
  if (!icons.is_object() || icons.size() > kinds.size()) throw Invalid{"/icons"};
  for (const auto& [kind, asset] : icons.items()) {
    if (std::ranges::find(kinds, kind) == kinds.end() || !asset.is_string() ||
        !id(asset.get_ref<const std::string&>()))
      throw Invalid{"/icons/" + kind};
  }
}

Icons load_icons(const Json& data, const platform::Project& project,
                 const platform::Resource& document, const fs::path& root) {
  Icons result;
  if (!data.contains("icons")) return result;
  for (const auto& [kind, id] : data["icons"].items()) {
    const auto& key = id.get_ref<const std::string&>();
    const auto asset = std::ranges::find(project.resources, key, &platform::Resource::id);
    const bool linked = std::ranges::find(document.refs, key) != document.refs.end();
    if (asset == project.resources.end() || !linked || asset->kind != "image")
      throw Invalid{"/icons/" + kind};
    const auto bytes = io::read_bytes(root / asset->path, 64 * 1024);
    if (!bytes) throw Invalid{"/icons/" + kind};
    const auto info = assets::inspect_png(*bytes);
    if (info.code != "ok" || info.width > 64 || info.height > 64) throw Invalid{"/icons/" + kind};
    auto image = assets::decode_png(*bytes);
    if (image.info.code != "ok") throw Invalid{"/icons/" + kind};
    result.emplace(kind, Icon{static_cast<int>(info.width), static_cast<int>(info.height),
                              std::move(image.rgba)});
  }
  return result;
}

Expedition decode(const Json& data, std::string identity) {
  if (!data.is_object() || data.value("version", 0) != 1 || !data.contains("permanent") ||
      !data["permanent"].is_boolean() || !data.contains("seed"))
    throw Invalid{"/"};
  const auto view = token(data, "view", "");
  if (view != "voxel" && view != "planar") throw Invalid{"/view"};
  Rules rules{view == "voxel",
              data["permanent"].get<bool>(),
              static_cast<std::uint32_t>(number(data["seed"], 0, UINT32_MAX, "/seed")),
              integer(data, "season_ticks", 4, 300, ""),
              integer(data, "initial_hunger", 1, 20, ""),
              integer(data, "hunger_interval", 1, 300, ""),
              {}};
  const auto& regions = array(data, "regions", 1, 16, "");
  std::set<std::string> region_ids, site_ids;
  for (std::size_t i = 0; i < regions.size(); ++i) {
    const auto path = "/regions/" + std::to_string(i);
    auto parsed = region(regions[i], path, site_ids);
    if (!region_ids.insert(parsed.id).second) throw Invalid{path + "/id"};
    rules.regions.push_back(std::move(parsed));
  }
  if (rules.regions[0].habitat == "ocean") throw Invalid{"/regions/0/habitat"};
  if (!data.contains("workshop")) throw Invalid{"/workshop"};
  auto items = items::read_workshop(data["workshop"], false);
  if (!items.workshop) throw Invalid{items.location};
  validate_references(rules, items.workshop->catalog(), region_ids);
  validate_icons(data);
  return Expedition(std::move(rules), std::move(*items.workshop), std::move(identity));
}

std::string identity(std::string_view project, std::uint64_t revision, std::string_view bytes) {
  std::uint64_t hash = 14695981039346656037ULL;
  for (unsigned char value : bytes) hash = (hash ^ value) * 1099511628211ULL;
  return std::string(project) + ":" + std::to_string(revision) + ":" + std::to_string(hash);
}

Action decode_action(const Json& value) {
  if (!value.is_object() || value.size() != 1U + value.count("target")) throw Invalid{"/actions"};
  const auto parsed = command(token(value, "command", "/actions"));
  if (!parsed) throw Invalid{"/actions/command"};
  return {*parsed, token(value, "target", "/actions", true)};
}

Json encode_action(const Action& action) {
  Json value{{"command", command_name(action.command)}};
  if (!action.target.empty()) value["target"] = action.target;
  return value;
}

Json state(const Expedition& expedition) {
  const auto& current = expedition.state();
  auto receipts = Json::array();
  for (const auto& receipt : current.receipts)
    receipts.push_back({{"cause", receipt.cause},
                        {"health", receipt.health},
                        {"hunger", receipt.hunger},
                        {"morale", receipt.morale},
                        {"wetness", receipt.wetness},
                        {"heat", receipt.heat}});
  auto positions = Json::object();
  for (std::size_t i = 0; i < current.positions.size(); ++i) {
    const auto point = current.positions[i];
    positions[expedition.rules().regions[i].id] = {point.x, point.y};
  }
  auto actors = Json::object();
  for (const auto& [id, point] : current.actors) actors[id] = {point.x, point.y};
  return {{"region", expedition.region().id},
          {"positions", positions},
          {"tick", current.tick},
          {"health", current.health},
          {"hunger", current.hunger},
          {"morale", current.morale},
          {"wetness", current.wetness},
          {"heat", current.heat},
          {"season", expedition.season()},
          {"tide", expedition.tide()},
          {"boat", current.boat},
          {"chapter", current.chapter},
          {"dead", current.dead},
          {"ending", current.ending},
          {"anchor_charge", current.anchor_charge},
          {"anchor", expedition.rules().regions[current.anchor].id},
          {"population", current.population},
          {"companions", current.companions},
          {"encounters", current.encounters},
          {"actors", actors},
          {"workshop", items::state_json(expedition.workshop().state())},
          {"receipts", receipts}};
}

}  // namespace

LoadResult load(const fs::path& root, std::uint64_t revision) {
  const auto open = platform::open_project(root);
  if (!open.project) return {{}, open.code, open.location};
  const auto& project = *open.project;
  if (project.revision != revision) return {{}, "revision_conflict", "/revision"};
  const auto entry =
      std::ranges::find(project.resources, project.entry_scene, &platform::Resource::id);
  const auto resource = std::ranges::find(project.resources, "expedition", &platform::Resource::id);
  if (resource == project.resources.end() ||
      std::ranges::find(entry->refs, resource->id) == entry->refs.end())
    return {{}, "no_expedition"};
  if (resource->kind != "data") return {{}, "invalid_expedition", resource->path};
  try {
    const auto bytes = io::read_bytes(root / resource->path, 1024 * 1024);
    if (!bytes) return {{}, "invalid_expedition", resource->path};
    const auto data = io::parse_json(*bytes);
    auto expedition = decode(data, identity(project.id, revision, *bytes));
    auto icons = load_icons(data, project, *resource, root);
    const auto current = platform::open_project(root);
    if (!current.project) return {{}, current.code, current.location};
    if (current.project->revision != revision) return {{}, "revision_conflict", "/revision"};
    return {std::move(expedition), "ok", {}, std::move(icons)};
  } catch (const Invalid& error) {
    return {{}, "invalid_expedition", resource->path + error.location};
  } catch (const Json::exception&) {
    return {{}, "invalid_expedition", resource->path};
  } catch (const fs::filesystem_error&) {
    return {{}, "io_error", resource->path};
  }
}

std::optional<std::string> document_error(std::string_view bytes) {
  try {
    decode(io::parse_json(bytes), {});
    return {};
  } catch (const Invalid& error) {
    return error.location;
  } catch (const Json::exception&) {
    return "/";
  }
}

std::string snapshot(const Expedition& expedition) { return state(expedition).dump(); }

std::string replay(Expedition expedition, std::string_view actions) {
  try {
    const auto input = io::parse_json(actions);
    if (!input.is_array() || input.size() > Expedition::max_actions)
      return Json{{"code", "invalid_action"}, {"location", "/input/actions"}}.dump();
    auto steps = Json::array();
    for (std::size_t i = 0; i < input.size(); ++i) {
      const auto result = expedition.apply(decode_action(input[i]));
      auto current = state(expedition);
      current["code"] = result;
      steps.push_back(std::move(current));
    }
    return Json{{"code", "ok"}, {"state", state(expedition)}, {"steps", steps}}.dump();
  } catch (const Invalid& error) {
    return Json{{"code", "invalid_action"}, {"location", "/input" + error.location}}.dump();
  } catch (const Json::exception&) {
    return Json{{"code", "invalid_action"}, {"location", "/input/actions"}}.dump();
  }
}

bool restore(Expedition& expedition, const fs::path& file) {
  if (!expedition.history().empty()) return false;
  try {
    if (io::node(file, false) != io::PathState::ok) return false;
    const auto bytes = io::read_bytes(file, 1024 * 1024);
    if (!bytes) return false;
    const auto document = io::parse_json(*bytes);
    if (!document.is_object() || document.value("version", 0) != 1 ||
        document.value("identity", "") != expedition.identity())
      return false;
    auto candidate = expedition;
    for (const auto& value : array(document, "actions", 0, Expedition::max_actions, ""))
      if (candidate.apply(decode_action(value)) != "ok") return false;
    expedition = std::move(candidate);
    return true;
  } catch (const Invalid&) {
    return false;
  } catch (const Json::exception&) {
    return false;
  } catch (const fs::filesystem_error&) {
    return false;
  }
}

bool save(const Expedition& expedition, const fs::path& file,
          const std::function<void()>& before_publish) {
  std::error_code error;
  fs::path staging;
  const auto cleanup = [&]() {
    if (staging.empty()) return;
    fs::remove(staging / "save.json", error);
    fs::remove(staging, error);
  };
  try {
    const auto target = fs::absolute(file);
    if (io::node(target, false) == io::PathState::unsafe) return false;
    std::random_device random;
    for (int attempt = 0; attempt < 8; ++attempt) {
      const auto directory = target.parent_path() / (".wds-save-" + std::to_string(random()));
      if (fs::create_directory(directory)) {
        staging = directory;
        break;
      }
    }
    if (staging.empty()) return false;
    auto actions = Json::array();
    for (const auto& action : expedition.history()) actions.push_back(encode_action(action));
    const auto staged = staging / "save.json";
    {
      std::ofstream output(staged, std::ios::binary);
      output
          << Json{{"version", 1}, {"identity", expedition.identity()}, {"actions", actions}}.dump();
      output.close();
      if (!output) {
        cleanup();
        return false;
      }
    }
    if (before_publish) before_publish();
    fs::rename(staged, target);
    cleanup();
    return true;
  } catch (const std::exception&) {
    cleanup();
    return false;
  }
}

}  // namespace wds::expedition
