#include "workshop.hpp"

#include <algorithm>
#include <utility>

namespace wds::items {
namespace {

int count(const Container& container, std::string_view id) {
  int result = 0;
  for (const auto& slot : container)
    if (slot.id == id) result += slot.count;
  return result;
}

void remove(Container& container, std::string_view id, int amount) {
  for (auto& slot : container) {
    if (slot.id != id) continue;
    const auto used = std::min(slot.count, amount);
    slot.count -= used;
    amount -= used;
    if (!amount) break;
  }
  std::erase_if(container, [](const Stack& slot) { return slot.count == 0; });
}

bool same_lot(const Stack& left, const Stack& right) {
  return left.id == right.id && left.best_before == right.best_before;
}

int room(const Container& container, int slots, int limit, const Stack& stack) {
  int available = (slots - static_cast<int>(container.size())) * limit;
  for (const auto& slot : container)
    if (same_lot(slot, stack)) available += limit - slot.count;
  return available;
}

bool add(Container& container, int slots, int limit, const Stack& stack) {
  if (room(container, slots, limit, stack) < stack.count) return false;

  int amount = stack.count;
  for (auto& slot : container) {
    if (same_lot(slot, stack)) {
      const auto placed = std::min(amount, limit - slot.count);
      amount -= placed;
      slot.count += placed;
    }
  }
  while (amount) {
    const auto placed = std::min(amount, limit);
    container.push_back({stack.id, placed, stack.best_before});
    amount -= placed;
  }
  return true;
}

std::string_view transfer(Container& source, Container& destination, int slots, int limit,
                          std::string_view id, int amount, bool partial = false) {
  if (count(source, id) < amount) return "missing_item";
  auto taken = source;
  auto placed = destination;
  int remaining = amount;
  for (auto& slot : taken) {
    if (slot.id != id) continue;
    const auto available = room(placed, slots, limit, slot);
    const auto used = std::min({remaining, slot.count, available});
    if (used == 0) continue;
    add(placed, slots, limit, {slot.id, used, slot.best_before});
    slot.count -= used;
    remaining -= used;
    if (remaining == 0) break;
  }
  if (remaining == amount || (!partial && remaining > 0)) return "container_full";
  std::erase_if(taken, [](const Stack& slot) { return slot.count == 0; });
  source = std::move(taken);
  destination = std::move(placed);
  return "ok";
}

}  // namespace

Workshop::Workshop(Catalog catalog, State initial)
    : catalog_(std::move(catalog)), initial_(std::move(initial)), state_(initial_) {}

void Workshop::reset() {
  state_ = initial_;
  tick_ = 0;
}

Stack Workshop::fresh(Stack stack) const {
  const auto& spoilage = catalog_.items.at(stack.id).spoilage;
  if (spoilage) stack.best_before = tick_ + spoilage->ticks;
  return stack;
}

int Workshop::advance(unsigned tick) {
  tick_ = tick;
  int spoiled = 0;
  for (auto* container : {&state_.bag, &state_.chest, &state_.loot}) {
    for (auto& slot : *container) {
      if (slot.best_before == 0 || slot.best_before > tick) continue;
      slot.id = catalog_.items.at(slot.id).spoilage->result;
      slot.best_before = 0;
      spoiled += slot.count;
    }
  }
  return spoiled;
}

bool Workshop::uses_survival_rules() const {
  return std::ranges::any_of(catalog_.items,
                             [](const auto& entry) { return entry.second.spoilage.has_value(); }) ||
         std::ranges::any_of(catalog_.recipes,
                             [](const auto& entry) { return entry.second.priority.has_value(); });
}

bool Workshop::matches(const Ingredient& ingredient, std::string_view item) const {
  if (!ingredient.selector.starts_with('#')) return ingredient.selector == item;
  const auto found = catalog_.items.find(item);
  return found != catalog_.items.end() &&
         found->second.tags.contains(std::string_view(ingredient.selector).substr(1));
}

std::optional<Container> Workshop::materials(const Recipe& recipe,
                                             const std::vector<std::string>& grid,
                                             const Container& available) const {
  std::map<std::string, int, std::less<>> needed;
  if (!grid.empty()) {
    for (std::size_t i = 0; i < grid.size(); ++i)
      if (const auto& slot = recipe.pattern[i]) needed[grid[i]] += slot->count;
  } else {
    std::vector<const Ingredient*> demand;
    const auto append = [&](const Ingredient& part) {
      demand.insert(demand.end(), static_cast<std::size_t>(part.count), &part);
    };
    for (const auto& part : recipe.ingredients) append(part);
    for (const auto& part : recipe.pattern)
      if (part) append(*part);

    std::vector<const std::string*> supply;
    for (const auto& slot : available) {
      if (std::ranges::none_of(demand,
                               [&](const Ingredient* part) { return matches(*part, slot.id); }))
        continue;
      supply.insert(supply.end(), static_cast<std::size_t>(slot.count), &slot.id);
    }
    // Match individual units so overlapping tag requirements cannot consume an
    // exact ingredient that another tag-compatible stack could have supplied.
    std::vector<int> assignment(supply.size(), -1);
    std::vector<int> seen(supply.size(), -1);
    const auto claim = [&](auto&& self, int request, int hop) -> bool {
      for (std::size_t i = 0; i < supply.size(); ++i) {
        if (seen[i] == hop || !matches(*demand[static_cast<std::size_t>(request)], *supply[i]))
          continue;
        seen[i] = hop;
        if (assignment[i] < 0 || self(self, assignment[i], hop)) {
          assignment[i] = request;
          return true;
        }
      }
      return false;
    };
    for (int i = 0; i < static_cast<int>(demand.size()); ++i)
      if (!claim(claim, i, i)) return {};
    for (std::size_t i = 0; i < supply.size(); ++i)
      if (assignment[i] >= 0) ++needed[*supply[i]];
  }
  Container result;
  for (const auto& [item, amount] : needed) {
    if (count(available, item) < amount) return {};
    result.push_back({item, amount});
  }
  return result;
}

std::string_view Workshop::craft(std::string_view id, const std::vector<std::string>& grid) {
  const auto found = catalog_.recipes.find(id);
  if (found == catalog_.recipes.end()) return "unknown_recipe";
  const auto& recipe = found->second;
  if (!recipe.requires_station.empty() && !state_.unlocked.contains(recipe.requires_station))
    return "station_locked";
  if (!grid.empty()) {
    if (grid.size() != recipe.pattern.size()) return "invalid_pattern";
    for (std::size_t i = 0; i < grid.size(); ++i) {
      const auto& slot = recipe.pattern[i];
      if (slot ? !matches(*slot, grid[i]) : !grid[i].empty()) return "invalid_pattern";
    }
  }
  const auto used = materials(recipe, grid, state_.bag);
  if (!used) return "missing_ingredients";
  return complete(recipe, *used);
}

std::string_view Workshop::complete(const Recipe& recipe, const Container& used) {
  auto bag = state_.bag;
  for (const auto& slot : used) remove(bag, slot.id, slot.count);
  if (!add(bag, catalog_.bag_slots, catalog_.items.at(recipe.output.id).limit,
           fresh(recipe.output)))
    return "container_full";
  state_.bag = std::move(bag);
  if (!recipe.unlocks.empty()) state_.unlocked.insert(recipe.unlocks);
  return "ok";
}

std::string_view Workshop::cook(const std::vector<std::string>& supplied) {
  if (supplied.empty() || supplied.size() > 8) return "invalid_ingredients";
  Container batch;
  for (const auto& id : supplied) {
    const auto item = catalog_.items.find(id);
    if (item == catalog_.items.end()) return "unknown_item";
    if (!add(batch, 8, item->second.limit, {id, 1})) return "invalid_ingredients";
  }
  for (const auto& slot : batch)
    if (count(batch, slot.id) > count(state_.bag, slot.id)) return "missing_ingredients";

  const Recipe* choice = nullptr;
  Container used;
  bool station_locked = false;
  for (const auto& [id, recipe] : catalog_.recipes) {
    if (recipe.method != "cook" || !recipe.priority ||
        (choice && *choice->priority >= *recipe.priority))
      continue;
    const auto matched = materials(recipe, {}, batch);
    if (!matched) continue;
    int required = 0;
    for (const auto& stack : *matched) required += stack.count;
    if (static_cast<std::size_t>(required) != supplied.size()) continue;
    if (!recipe.requires_station.empty() && !state_.unlocked.contains(recipe.requires_station)) {
      station_locked = true;
      continue;
    }
    choice = &recipe;
    used = *matched;
  }
  if (!choice) return station_locked ? "station_locked" : "no_matching_recipe";
  return complete(*choice, used);
}

std::string_view Workshop::gather(std::string_view id) {
  const auto found = catalog_.sources.find(id);
  if (found == catalog_.sources.end() || found->second.uses <= 0) return "unknown_source";
  const auto previous = state_.gathered.find(id);
  if (previous != state_.gathered.end() && previous->second == found->second.uses)
    return "source_empty";
  if (!drop(found->second.yield)) return "container_full";
  ++state_.gathered[found->first];
  return "ok";
}

bool Workshop::can_drop(const Stack& stack) const {
  const auto item = catalog_.items.find(stack.id);
  return stack.count > 0 && item != catalog_.items.end() &&
         room(state_.loot, loot_slots, item->second.limit, fresh(stack)) >= stack.count;
}

bool Workshop::drop(const Stack& stack) {
  const auto item = catalog_.items.find(stack.id);
  return stack.count > 0 && item != catalog_.items.end() &&
         add(state_.loot, loot_slots, item->second.limit, fresh(stack));
}

bool Workshop::has_item(std::string_view id, int amount) const {
  return amount > 0 && count(state_.bag, id) >= amount;
}

bool Workshop::consume(std::string_view id, int amount) {
  if (!has_item(id, amount)) return false;
  remove(state_.bag, id, amount);
  return true;
}

std::vector<Action> Workshop::actions(bool include_gather) const {
  std::vector<Action> result;
  if (include_gather)
    for (const auto& [id, source] : catalog_.sources)
      if (source.uses > 0) result.push_back({"gather", id});
  const auto list_items = [&](std::string_view type, const Container& inventory) {
    Tags present;
    for (const auto& slot : inventory)
      if (present.insert(slot.id).second) result.push_back({std::string(type), slot.id});
  };
  list_items("pickup", state_.loot);
  for (const auto& [id, recipe] : catalog_.recipes) result.push_back({"recipe", id});
  list_items("store", state_.bag);
  list_items("take", state_.chest);
  return result;
}

std::string_view Workshop::apply(const Action& action, const std::vector<std::string>& grid) {
  if (action.type == "recipe") return craft(action.id, grid);
  if (!grid.empty()) return "invalid_pattern";
  if (action.type == "gather") return gather(action.id);
  const auto item = catalog_.items.find(action.id);
  if (item == catalog_.items.end()) return "unknown_item";
  const auto limit = item->second.limit;
  if (action.type == "pickup") {
    const auto loose = count(state_.loot, action.id);
    if (!loose) return "missing_item";
    return transfer(state_.loot, state_.bag, catalog_.bag_slots, limit, action.id, loose, true);
  }
  if (action.type == "store")
    return transfer(state_.bag, state_.chest, catalog_.chest_slots, limit, action.id, 1);
  if (action.type == "take")
    return transfer(state_.chest, state_.bag, catalog_.bag_slots, limit, action.id, 1);
  return "unknown_action";
}

}  // namespace wds::items
