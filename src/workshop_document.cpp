#include "workshop_document.hpp"

#include <algorithm>

namespace wds::items {
namespace {

using Json = nlohmann::json;
struct Invalid {
  std::string location;
};

bool symbol(std::string_view id) {
  const auto letter = [](char c) { return c >= 'a' && c <= 'z'; };
  return !id.empty() && id.size() <= 64 && letter(id.front()) &&
         std::ranges::all_of(
             id, [&](char c) { return letter(c) || c == '-' || (c >= '0' && c <= '9'); });
}

bool shape(const Json& object, std::initializer_list<std::string_view> required,
           std::initializer_list<std::string_view> optional = {}) {
  if (!object.is_object() ||
      !std::ranges::all_of(required, [&](auto key) { return object.contains(key); }))
    return false;
  for (const auto& [key, value] : object.items()) {
    (void)value;
    if (std::ranges::find(required, key) == required.end() &&
        std::ranges::find(optional, key) == optional.end())
      return false;
  }
  return true;
}

int number(const Json& value, int minimum, int maximum, std::string location) {
  if (value.is_number_unsigned()) {
    const auto count = value.get<std::uint64_t>();
    if (count <= static_cast<std::uint64_t>(maximum) &&
        count >= static_cast<std::uint64_t>(minimum))
      return static_cast<int>(count);
  } else if (value.is_number_integer()) {
    const auto count = value.get<std::int64_t>();
    if (count >= minimum && count <= maximum) return static_cast<int>(count);
  }
  throw Invalid{std::move(location)};
}

std::string text(const Json& value, const std::string& location) {
  if (!value.is_string()) throw Invalid{location};
  return value.get<std::string>();
}

Tags tags(const Json& array, const std::string& location) {
  Tags result;
  if (!array.is_array() || array.size() > 64) throw Invalid{location};
  for (const auto& value : array) {
    auto id = text(value, location);
    if (!symbol(id) || !result.insert(std::move(id)).second) throw Invalid{location};
  }
  return result;
}

Stack stack(const Catalog& catalog, const Json& object, const std::string& location) {
  if (!shape(object, {"item", "count"})) throw Invalid{location};
  auto id = text(object["item"], location + "/item");
  if (!catalog.items.contains(id)) throw Invalid{location + "/item"};
  return {std::move(id), number(object["count"], 1, 64, location + "/count")};
}

Ingredient ingredient(const Catalog& catalog, const Json& object, const std::string& location) {
  if (!shape(object, {"item", "count"})) throw Invalid{location};
  auto id = text(object["item"], location + "/item");
  const bool known =
      id.starts_with('#')
          ? std::ranges::any_of(catalog.items,
                                [&](const auto& entry) {
                                  return entry.second.tags.contains(std::string_view(id).substr(1));
                                })
          : catalog.items.contains(id);
  if (!known) throw Invalid{location + "/item"};
  return {std::move(id), number(object["count"], 1, 64, location + "/count")};
}

Recipe recipe(const Catalog& catalog, const Json& object, const std::string& location) {
  if (!shape(object, {"method", "output"}, {"station", "unlocks", "ingredients", "pattern"}) ||
      object.contains("ingredients") == object.contains("pattern"))
    throw Invalid{location};
  Recipe result{};
  result.method = text(object["method"], location + "/method");
  if (result.method != "craft" && result.method != "smelt" && result.method != "cook")
    throw Invalid{location + "/method"};
  if (object.contains("station")) {
    result.requires_station = text(object["station"], location + "/station");
    if (!symbol(result.requires_station)) throw Invalid{location + "/station"};
  } else if (result.method != "craft") {
    throw Invalid{location + "/station"};
  }
  if (object.contains("unlocks")) {
    result.unlocks = text(object["unlocks"], location + "/unlocks");
    if (!symbol(result.unlocks)) throw Invalid{location + "/unlocks"};
  }
  result.output = stack(catalog, object["output"], location + "/output");
  int units = 0;
  if (object.contains("ingredients")) {
    const auto& array = object["ingredients"];
    if (!array.is_array() || array.empty() || array.size() > 64)
      throw Invalid{location + "/ingredients"};
    for (std::size_t i = 0; i < array.size(); ++i) {
      auto part = ingredient(catalog, array[i], location + "/ingredients/" + std::to_string(i));
      units += part.count;
      result.ingredients.push_back(std::move(part));
    }
  } else {
    const auto& array = object["pattern"];
    if (!array.is_array() || array.size() != 9 || result.method != "craft")
      throw Invalid{location + "/pattern"};
    for (std::size_t i = 0; i < array.size(); ++i) {
      if (array[i].is_null()) {
        result.pattern.push_back(std::nullopt);
      } else {
        auto part = ingredient(catalog, array[i], location + "/pattern/" + std::to_string(i));
        units += part.count;
        result.pattern.push_back(std::move(part));
      }
    }
  }
  if (!units || units > 64) throw Invalid{location};
  return result;
}

Container container(const Catalog& catalog, const Json& array, int slots,
                    const std::string& location) {
  Container result;
  if (!array.is_array() || array.size() > static_cast<std::size_t>(slots)) throw Invalid{location};
  for (std::size_t index = 0; index < array.size(); ++index) {
    const auto where = location + "/" + std::to_string(index);
    auto slot = stack(catalog, array[index], where);
    if (slot.count > catalog.items.at(slot.id).limit) throw Invalid{where + "/count"};
    result.push_back(std::move(slot));
  }
  return result;
}

State progress(const Catalog& catalog, const Json& object, const Tags& stations) {
  constexpr std::string_view path = "/workshop/progress";
  if (!shape(object, {"bag", "chest", "loot", "unlocked"})) throw Invalid{std::string(path)};
  State result;
  result.bag = container(catalog, object["bag"], catalog.bag_slots, "/workshop/progress/bag");
  result.chest =
      container(catalog, object["chest"], catalog.chest_slots, "/workshop/progress/chest");
  result.loot = container(catalog, object["loot"], Workshop::loot_slots, "/workshop/progress/loot");
  result.unlocked = tags(object["unlocked"], "/workshop/progress/unlocked");
  if (!std::ranges::all_of(result.unlocked, [&](const auto& id) { return stations.contains(id); }))
    throw Invalid{"/workshop/progress/unlocked"};
  return result;
}

}  // namespace

LoadResult read_workshop(const Json& document, bool voxel) {
  try {
    if (!shape(document, {"version", "bag_slots", "chest_slots", "items", "recipes", "sources"},
               {"progress"}) ||
        (!voxel && document.contains("progress")))
      throw Invalid{"/workshop"};
    number(document["version"], 1, 1, "/workshop/version");
    Catalog catalog;
    catalog.bag_slots = number(document["bag_slots"], 1, 32, "/workshop/bag_slots");
    catalog.chest_slots = number(document["chest_slots"], 1, 32, "/workshop/chest_slots");
    const auto& items = document["items"];
    if (!items.is_object() || items.empty() || items.size() > 128) throw Invalid{"/workshop/items"};
    for (const auto& [id, item] : items.items()) {
      const auto where = "/workshop/items/" + id;
      if (!symbol(id) || !shape(item, {"limit", "tags"})) throw Invalid{where};
      catalog.items.emplace(id, Item{number(item["limit"], 1, 64, where + "/limit"),
                                     tags(item["tags"], where + "/tags")});
    }
    const auto& recipes = document["recipes"];
    if (!recipes.is_object() || recipes.size() > 128) throw Invalid{"/workshop/recipes"};
    Tags stations;
    for (const auto& [id, value] : recipes.items()) {
      const auto where = "/workshop/recipes/" + id;
      if (!symbol(id)) throw Invalid{where};
      auto entry = recipe(catalog, value, where);
      if (!entry.unlocks.empty()) stations.insert(entry.unlocks);
      catalog.recipes.emplace(id, std::move(entry));
    }
    if (stations.size() > 64) throw Invalid{"/workshop/recipes"};
    for (const auto& [id, value] : catalog.recipes)
      if (!value.requires_station.empty() && !stations.contains(value.requires_station))
        throw Invalid{"/workshop/recipes/" + id + "/station"};

    const auto& sources = document["sources"];
    if (!sources.is_object() || sources.size() > 128) throw Invalid{"/workshop/sources"};
    for (const auto& [id, value] : sources.items()) {
      const auto where = "/workshop/sources/" + id;
      if (!symbol(id) || !shape(value, {"yield", "uses"})) throw Invalid{where};
      if (voxel && id != "grass" && id != "dirt" && id != "stone" && id != "copper" &&
          id != "quartz" && id != "timber")
        throw Invalid{where};
      catalog.sources.emplace(
          id, Source{stack(catalog, value["yield"], where + "/yield"),
                     number(value["uses"], voxel ? 0 : 1, voxel ? 0 : 64, where + "/uses")});
    }
    auto initial =
        document.contains("progress") ? progress(catalog, document["progress"], stations) : State{};
    return {Workshop(std::move(catalog), std::move(initial)), {}};
  } catch (const Invalid& error) {
    return {{}, error.location};
  }
}

Json state_json(const State& state) {
  const auto slots = [](const Container& container) {
    auto result = Json::array();
    for (const auto& stack : container)
      result.push_back({{"item", stack.id}, {"count", stack.count}});
    return result;
  };
  return {{"bag", slots(state.bag)},
          {"chest", slots(state.chest)},
          {"loot", slots(state.loot)},
          {"unlocked", state.unlocked}};
}

}  // namespace wds::items
