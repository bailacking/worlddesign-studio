#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace wds::items {

struct Stack {
  std::string id;
  int count;
  unsigned best_before = 0;
  unsigned uses_remaining = 0;
  bool operator==(const Stack&) const = default;
};

using Container = std::vector<Stack>;
using Tags = std::set<std::string, std::less<>>;

struct Spoilage {
  unsigned ticks;
  std::string result;
};

struct Item {
  int limit;
  Tags tags;
  std::optional<Spoilage> spoilage{};
  unsigned durability = 0;
};

struct Ingredient {
  std::string selector;
  int count;
};

struct Recipe {
  std::string method;
  std::string requires_station;
  std::string unlocks;
  std::vector<Ingredient> ingredients;
  std::vector<std::optional<Ingredient>> pattern;
  Stack output;
  std::optional<int> priority{};
};

struct Source {
  Stack yield;
  int uses;
};

struct Catalog {
  std::map<std::string, Item, std::less<>> items;
  std::map<std::string, Recipe, std::less<>> recipes;
  std::map<std::string, Source, std::less<>> sources;
  int bag_slots = 0;
  int chest_slots = 0;
};

struct State {
  Container bag;
  Container chest;
  Container loot;
  std::map<std::string, int, std::less<>> gathered;
  Tags unlocked;
  bool operator==(const State&) const = default;
};

struct Action {
  std::string type;
  std::string id;
  bool operator==(const Action&) const = default;
};

class Workshop {
 public:
  static constexpr int loot_slots = 16;

  explicit Workshop(Catalog catalog, State initial = {});
  const Catalog& catalog() const { return catalog_; }
  const State& state() const { return state_; }
  void reset();
  bool can_drop(const Stack& stack) const;
  bool drop(const Stack& stack);
  bool has_item(std::string_view id, int amount = 1) const;
  bool consume(std::string_view id, int amount = 1);
  std::string_view cook(const std::vector<std::string>& supplied);
  int advance(unsigned tick);
  bool uses_survival_rules() const;
  bool uses_processing_rules() const;
  bool wear(std::string_view id);
  std::vector<Action> actions(bool gather) const;
  std::string_view apply(const Action& action, const std::vector<std::string>& grid = {});

 private:
  bool matches(const Ingredient& ingredient, std::string_view item) const;
  std::optional<Container> materials(const Recipe& recipe, const std::vector<std::string>& grid,
                                     const Container& available) const;
  std::string_view complete(const Recipe& recipe, const Container& used);
  std::string_view craft(std::string_view id, const std::vector<std::string>& grid);
  std::string_view gather(std::string_view id);
  Stack fresh(Stack stack) const;

  Catalog catalog_;
  State initial_;
  State state_;
  unsigned tick_ = 0;
};

}  // namespace wds::items
