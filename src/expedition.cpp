#include "expedition.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <utility>

#include "seeded_grid.hpp"

namespace wds::expedition {
namespace {

constexpr std::array commands{"north",  "south", "west", "east",   "wait", "use",  "attack",
                              "gather", "craft", "eat",  "revive", "cook", "treat"};

int distance(TilePoint a, TilePoint b) { return std::abs(a.x - b.x) + std::abs(a.y - b.y); }

}  // namespace

std::optional<Command> command(std::string_view name) {
  const auto found = std::ranges::find(commands, name);
  if (found == commands.end()) return {};
  return static_cast<Command>(found - commands.begin());
}

std::string_view command_name(Command value) {
  return commands.at(static_cast<std::size_t>(value));
}

Expedition::Expedition(Rules rules, items::Workshop workshop, std::string identity)
    : rules_(std::move(rules)), workshop_(std::move(workshop)), identity_(std::move(identity)) {
  state_.hunger = rules_.initial_hunger;
  for (const auto& region : rules_.regions) {
    state_.positions.push_back(region.spawn);
    for (const auto& site : region.sites)
      if (site.kind == "enemy" || site.kind == "boss" || site.kind == "raid")
        state_.encounters.emplace(site.id, site.power);
    for (const auto& site : region.sites)
      if (site.kind == "enemy" || site.kind == "raid") state_.actors.emplace(site.id, site.point);
    for (const auto& site : region.sites)
      if (site.kind == "plot") state_.crops.emplace(site.id, -1);
  }
}

TilePoint Expedition::location(const Site& site) const {
  const auto current = state_.actors.find(site.id);
  return current == state_.actors.end() ? site.point : current->second;
}

bool Expedition::uses_survival_rules() const {
  return workshop_.uses_survival_rules() || !state_.crops.empty() ||
         std::ranges::any_of(rules_.regions, [](const auto& area) {
           return std::ranges::any_of(area.sites,
                                      [](const auto& site) { return site.kind == "kitchen"; });
         });
}

bool Expedition::uses_exposure_rules() const {
  return std::ranges::any_of(rules_.regions, [](const auto& area) {
    return area.climate ||
           std::ranges::any_of(area.sites, [](const auto& site) { return site.venom > 0; });
  });
}

const Site* Expedition::nearby(std::string_view target) const {
  if (target.empty()) {
    const auto found = std::ranges::min_element(
        region().sites, {}, [&](const auto& site) { return distance(location(site), position()); });
    return found != region().sites.end() && distance(location(*found), position()) <= 1 ? &*found
                                                                                        : nullptr;
  }
  const auto found = std::ranges::find(region().sites, target, &Site::id);
  return found != region().sites.end() && distance(location(*found), position()) <= 1 ? &*found
                                                                                      : nullptr;
}

int Expedition::terrain(TilePoint point) const {
  const auto& here = region();
  const auto region_number = static_cast<std::uint32_t>(state_.region);
  const auto hash = coordinate_hash(rules_.seed ^ (region_number * 0x9e3779b9U),
                                    floor_div(point.x, 2), floor_div(point.y, 2));
  if (here.habitat == "ocean") return hash % 7U < 2 ? 2 : 1;
  if (distance(point, here.spawn) <= 1 || point.x == 1 || point.y == 1) return 0;
  if (std::ranges::any_of(here.sites,
                          [&](const Site& site) { return distance(point, site.point) <= 1; }))
    return 0;
  return hash % 11U == 0 ? 3 : static_cast<int>(hash % 3U);
}

void Expedition::change(std::string_view cause, int health, int hunger, int morale) {
  state_.health = std::clamp(state_.health + health, 0, 20);
  state_.hunger = std::clamp(state_.hunger + hunger, 0, 20);
  state_.morale = std::clamp(state_.morale + morale, 0, 20);
  state_.dead = state_.health == 0;
  state_.receipts.push_back({std::string(cause), state_.health, state_.hunger, state_.morale,
                             state_.wetness, state_.heat});
}

void Expedition::advance() {
  ++state_.tick;
  if (workshop_.advance(state_.tick) > 0) change("spoilage", 0);
  if (state_.tick % static_cast<unsigned>(rules_.hunger_interval) == 0) change("metabolism", 0, -1);
  if (state_.hunger == 0) change("starvation", -1);
  if (region().habitat == "cavern" && state_.tick % 2 == 0 && !workshop_.has_item("lantern"))
    change("darkness", -1, 0, -2);
  if (region().habitat == "ember" && state_.tick % 3 == 0) change("embers", -1);
  advance_environment();
  advance_crops();
  for (const auto& site : region().sites) {
    const auto actor = state_.actors.find(site.id);
    if (actor == state_.actors.end() || state_.encounters.at(site.id) == 0) continue;
    auto& point = actor->second;
    const auto range = distance(point, position());
    if (state_.tick % 3 == 0 && range > 1 && range <= 4) {
      auto next = point;
      if (point.x != position().x)
        next.x += point.x < position().x ? 1 : -1;
      else
        next.y += point.y < position().y ? 1 : -1;
      if (terrain(next) != 3) point = next;
    }
    if (site.kind == "raid" && state_.tick % 6 == 0 && distance(point, position()) <= 2)
      change("raid", -2);
  }
}

void Expedition::advance_environment() {
  const auto phase = season();
  const auto& climate = region().climate;
  if (!rules_.voxel || climate) {
    const int rain =
        climate ? climate->wetness[phase] : (phase == 1 || region().habitat == "ocean" ? 2 : -1);
    const int warmth = climate ? climate->heat[phase] : (phase == 2 ? 2 : -1);
    state_.wetness = std::clamp(state_.wetness + rain, 0, 10);
    state_.heat = std::clamp(state_.heat + warmth, 0, 10);
    if (state_.wetness >= 8) change("drenched", 0, 0, -1);
    if (state_.heat >= 8) change("overheat", -2);
    if (phase == 3 && state_.wetness > 4) change("cold", -1);
    if (state_.morale == 0) change("despair", -1);
  }
  if (climate && climate->eruption && phase == climate->eruption->season) {
    const auto& hazard = *climate->eruption;
    const auto offset = (rules_.seed + static_cast<unsigned>(state_.region)) % hazard.interval;
    const auto progress = (state_.tick + offset) % hazard.interval;
    if (progress == hazard.interval - hazard.warning_ticks) change("eruption_warning", 0);
    if (progress == 0) change("eruption", -hazard.damage);
  }
  if (state_.poison > 0) {
    --state_.poison;
    change("poison", -1);
  }
}

void Expedition::advance_crops() {
  if (state_.crops.empty() || season() == 3) return;
  for (const auto& area : rules_.regions) {
    if (season() == 2 && area.habitat != "shore") continue;
    for (const auto& site : area.sites) {
      if (site.kind != "plot") continue;
      auto& remaining = state_.crops.at(site.id);
      if (remaining > 0 && --remaining == 0) change("crop_ready", 0);
    }
  }
}

bool Expedition::award(const std::string& reward, const std::string& cost) {
  auto next = workshop_;
  if (!cost.empty() && !next.consume(cost, 1)) return false;
  if (!reward.empty() && (!next.drop({reward, 1}) || next.apply({"pickup", reward}) != "ok"))
    return false;
  workshop_ = std::move(next);
  return true;
}

void Expedition::go_to(std::size_t index) {
  state_.region = index;
  change("arrival", 0);
}

bool Expedition::enter(const Site& portal) {
  const auto found = std::ranges::find(rules_.regions, portal.target, &Region::id);
  if (found == rules_.regions.end()) return false;
  if (!portal.item.empty() && !workshop_.has_item(portal.item)) return false;
  if (found->habitat == "ocean" && state_.boat == 0) return false;
  go_to(static_cast<std::size_t>(found - rules_.regions.begin()));
  return true;
}

std::string_view Expedition::move(int x, int y) {
  auto point = position();
  point.x += x;
  point.y += y;
  if (point.x < 0 || point.y < 0 || point.x >= region().width || point.y >= region().height ||
      terrain(point) == 3)
    return "blocked";
  if (region().habitat == "ocean") {
    if (!state_.boat) return "boat_required";
    if (terrain(point) == 2 || tide() == 2) {
      --state_.boat;
      change("wave", -1);
    }
  }
  state_.positions[state_.region] = point;
  return "ok";
}

std::string_view Expedition::interact(const Site& site, bool attack) {
  if (attack) {
    const auto found = state_.encounters.find(site.id);
    if (found == state_.encounters.end() || found->second == 0) return "no_target";
    const int dealt = workshop_.has_item("spear") ? 3 : 1;
    if (dealt >= found->second) {
      if (!award(site.reward)) return "capacity";
      found->second = 0;
      change("victory", 0, 0, 1);
    } else {
      const auto before = found->second;
      found->second -= dealt;
      const bool phase_change =
          site.kind == "boss" && before > site.power / 2 && found->second <= site.power / 2;
      if (phase_change) change("boss_enraged", 0);
      const bool enraged = site.kind == "boss" && found->second <= site.power / 2;
      change("retaliation", enraged ? -3 : -2, 0, -1);
      if (!state_.dead && site.venom > state_.poison) {
        state_.poison = site.venom;
        change("poisoned", 0);
      }
      if (region().habitat == "ocean") state_.boat = std::max(0, state_.boat - 1);
    }
    return "ok";
  }
  if (site.kind == "portal") return enter(site) ? "ok" : "route_locked";
  if (site.kind == "trade") return award(site.reward, site.item) ? "ok" : "trade_unavailable";
  if (site.kind == "plot") {
    auto& remaining = state_.crops.at(site.id);
    if (remaining > 0) return "crop_growing";
    if (remaining == 0) {
      if (!award(site.reward)) return "capacity";
      remaining = -1;
      change("harvest", 0);
    } else {
      if (!workshop_.consume(site.item)) return "missing_item";
      remaining = site.power;
      change("plant", 0);
    }
    return "ok";
  }
  if (site.kind == "anchor" && state_.anchor_site == site.id &&
      state_.anchor_charge >= site.anchor_capacity)
    return "anchor_full";
  if (!site.item.empty() && !workshop_.has_item(site.item)) return "missing_item";
  if (site.kind == "anchor") {
    if (!workshop_.consume(site.item, 1)) return "missing_item";
    state_.anchor_charge = state_.anchor_site == site.id ? state_.anchor_charge + 1 : 1;
    state_.anchor = state_.region;
    state_.anchor_site = site.id;
    change("anchor_charged", 0);
  } else if (site.kind == "dock") {
    if (state_.boat != 0) return "already_launched";
    state_.boat = 8;
    change("launch", 0);
  } else if (site.kind == "repair") {
    if (!award({}, site.item)) return "missing_item";
    state_.boat = std::min(8, state_.boat + site.power);
    change("repair", 0);
  } else if (site.kind == "tame" || site.kind == "breed") {
    auto& count = site.kind == "tame" ? state_.companions : state_.population;
    if (count >= 6) return "population_limit";
    if (!award({}, site.item)) return "missing_item";
    ++count;
    change(site.kind, 0);
  } else if (site.kind == "rest") {
    change("shelter", site.power, 0, site.power);
  } else if (site.kind == "chapter") {
    if (!site.target.empty() && site.target != state_.chapter) return "chapter_locked";
    if (!award(site.reward)) return "capacity";
    state_.chapter = site.id;
    state_.ending = site.id == "return";
    change(state_.ending ? "ending" : "chapter", 0, 0, 2);
  } else {
    return "no_action";
  }
  return "ok";
}

std::string_view Expedition::apply(Action action) {
  if (history_.size() == max_actions) return "action_limit";
  const auto cmd = action.command;
  if (state_.dead && cmd != Command::revive) return "dead";
  const bool needs_target = cmd == Command::gather || cmd == Command::craft ||
                            cmd == Command::eat || cmd == Command::cook || cmd == Command::treat;
  const bool uses_target = needs_target || cmd == Command::use || cmd == Command::attack;
  if ((needs_target && action.target.empty()) || (!uses_target && !action.target.empty()) ||
      (cmd == Command::cook) != !action.ingredients.empty())
    return "invalid_action";
  const auto execute = [&]() -> std::string_view {
    switch (cmd) {
      case Command::north:
        return move(0, -1);
      case Command::south:
        return move(0, 1);
      case Command::west:
        return move(-1, 0);
      case Command::east:
        return move(1, 0);
      case Command::wait:
        return "ok";
      case Command::use:
      case Command::attack: {
        const auto* site = nearby(action.target);
        return site ? interact(*site, cmd == Command::attack) : "out_of_reach";
      }
      case Command::gather: {
        const auto* site = nearby(action.target);
        if (!site || site->kind != "source") return "out_of_reach";
        auto next = workshop_;
        if (const auto result = next.apply({"gather", site->id}); result != "ok") return result;
        if (const auto result = next.apply({"pickup", site->reward}); result != "ok") return result;
        workshop_ = std::move(next);
        return "ok";
      }
      case Command::craft:
        return workshop_.apply({"recipe", action.target});
      case Command::cook: {
        const auto* site = nearby(action.target);
        if (!site || site->kind != "kitchen") return "out_of_reach";
        const auto result = workshop_.cook(action.ingredients);
        if (result == "ok") change("cooked", 0);
        return result;
      }
      case Command::eat:
        if (const auto item = workshop_.catalog().items.find(action.target);
            item == workshop_.catalog().items.end() || !item->second.tags.contains("food") ||
            !workshop_.consume(action.target, 1))
          return "missing_food";
        change("meal", 1, 6);
        return "ok";
      case Command::treat:
        if (state_.poison == 0) return "no_poison";
        if (const auto item = workshop_.catalog().items.find(action.target);
            item == workshop_.catalog().items.end() || !item->second.tags.contains("antidote") ||
            !workshop_.consume(action.target))
          return "missing_antidote";
        state_.poison = 0;
        change("treated", 0);
        return "ok";
      case Command::revive:
        if (!state_.dead) return "still_alive";
        if (!state_.anchor_charge && rules_.permanent && !workshop_.consume("lifeline", 1))
          return "permanent_death";
        if (state_.anchor_charge) --state_.anchor_charge;
        state_.dead = false;
        state_.poison = 0;
        state_.hunger = std::max(state_.hunger, 8);
        go_to(state_.anchor);
        state_.positions[state_.region] = region().spawn;
        change("revival", 12, 0, 6);
        return "ok";
    }
    return "invalid_action";
  };
  auto previous = std::move(state_.receipts);
  state_.receipts.clear();
  const auto result = execute();
  if (result != "ok") {
    state_.receipts = std::move(previous);
    return result;
  }
  if (!state_.dead) advance();
  history_.push_back(std::move(action));
  return "ok";
}

}  // namespace wds::expedition
