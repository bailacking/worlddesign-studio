#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "tile_world.hpp"
#include "workshop.hpp"

namespace wds::expedition {

using platform::TilePoint;

struct Site {
  std::string id;
  std::string kind;
  TilePoint point;
  std::string target;
  std::string item;
  std::string reward;
  int power;
  int anchor_capacity = 3;
  std::string icon{};
  int venom = 0;
  std::vector<std::string> after_any{};
  std::string unlock_character{};
  int interaction_priority = 0;
  std::string inspection{};
  bool activate_character = false;
};

struct Eruption {
  unsigned interval;
  unsigned warning_ticks;
  unsigned season;
  int damage;
};

struct Climate {
  std::array<int, 4> wetness;
  std::array<int, 4> heat;
  std::optional<Eruption> eruption{};
};

struct Region {
  std::string id;
  std::string habitat;
  int width;
  int height;
  TilePoint spawn;
  std::vector<Site> sites;
  std::optional<std::array<std::uint32_t, 4>> palette;
  std::optional<Climate> climate{};
};

enum class Command {
  north,
  south,
  west,
  east,
  wait,
  use,
  attack,
  gather,
  craft,
  eat,
  revive,
  cook,
  treat,
  supply,
  survey_region
};

enum class PlayMode { journey, build, trial, survey };

using KeyBindings = std::map<std::string, std::vector<std::string>, std::less<>>;

const KeyBindings& default_key_bindings();
bool valid_input_key(std::string_view name);

struct RulePack {
  std::string resource;
  PlayMode mode;
  std::vector<Command> denied;
  std::optional<int> season_ticks{};
  std::optional<int> hunger_interval{};
};

struct CharacterTraits {
  int heat_guard = 0;
  int venom_guard = 0;
};

struct Crew {
  std::string initial;
  std::map<std::string, CharacterTraits, std::less<>> characters;
};

struct Rules {
  bool voxel;
  bool permanent;
  std::uint32_t seed;
  int season_ticks;
  int initial_hunger;
  int hunger_interval;
  std::vector<Region> regions;
  std::optional<RulePack> pack{};
  KeyBindings keys{};
  std::optional<Crew> crew{};
};

struct Action {
  Command command;
  std::string target{};
  std::vector<std::string> ingredients{};
};

std::optional<Command> command(std::string_view name);
std::string_view command_name(Command command);
std::optional<PlayMode> play_mode(std::string_view name);
std::string_view play_mode_name(PlayMode mode);

struct Receipt {
  std::string cause;
  int health;
  int hunger;
  int morale;
  int wetness;
  int heat;
};

struct State {
  unsigned tick = 0;
  std::size_t region = 0;
  std::vector<TilePoint> positions;
  int health = 20;
  int hunger = 20;
  int morale = 20;
  int wetness = 0;
  int heat = 0;
  int poison = 0;
  int boat = 0;
  int anchor_charge = 0;
  int population = 1;
  int companions = 0;
  bool dead = false;
  bool ending = false;
  std::size_t anchor = 0;
  std::string anchor_site;
  std::string chapter = "departure";
  std::map<std::string, int, std::less<>> encounters;
  std::map<std::string, TilePoint, std::less<>> actors;
  std::map<std::string, int, std::less<>> crops;
  items::Tags completed_chapters;
  items::Tags unlocked_characters;
  std::string active_character{};
  std::vector<Receipt> receipts;
};

class Expedition {
 public:
  static constexpr std::size_t max_actions = 4096;

  Expedition(Rules rules, items::Workshop workshop, std::string identity);
  const Rules& rules() const { return rules_; }
  const State& state() const { return state_; }
  const Region& region() const { return rules_.regions[state_.region]; }
  TilePoint position() const { return state_.positions[state_.region]; }
  const items::Workshop& workshop() const { return workshop_; }
  const std::string& identity() const { return identity_; }
  const std::vector<Action>& history() const { return history_; }
  unsigned season() const { return (state_.tick / rules_.season_ticks) % 4; }
  unsigned tide() const { return (state_.tick / 4) % 4; }
  std::string_view apply(Action action);
  PlayMode mode() const { return rules_.pack ? rules_.pack->mode : PlayMode::journey; }
  bool allows(Command command) const;
  std::vector<std::string_view> permitted_commands() const;
  const KeyBindings& input_keys() const;
  const Site* nearby(std::string_view target) const;
  std::vector<const Site*> nearby_sites() const;
  int terrain(TilePoint point) const;
  TilePoint location(const Site& site) const;
  bool uses_survival_rules() const;
  bool uses_exposure_rules() const;
  bool uses_story_rules() const { return !state_.completed_chapters.empty(); }
  bool uses_interaction_rules() const;
  bool uses_character_rules() const;
  CharacterTraits active_traits() const;

 private:
  std::string_view interact(const Site& site, bool attack);
  std::string_view move(int x, int y);
  void advance();
  void advance_crops();
  void advance_environment();
  void change(std::string_view cause, int health, int hunger = 0, int morale = 0);
  bool award(const std::string& reward, const std::string& cost = {});
  bool enter(const Site& portal);
  void go_to(std::size_t region);

  Rules rules_;
  items::Workshop workshop_;
  std::string identity_;
  State state_;
  std::vector<Action> history_;
};

struct Icon {
  int width;
  int height;
  std::vector<unsigned char> rgba;
};

using Icons = std::map<std::string, Icon, std::less<>>;

struct LoadResult {
  std::optional<Expedition> expedition;
  std::string code;
  std::string location{};
  Icons icons{};
};

LoadResult load(const std::filesystem::path& root, std::uint64_t revision);
std::optional<std::string> document_error(std::string_view bytes);
std::string snapshot(const Expedition& expedition);
std::string replay(Expedition expedition, std::string_view actions);
bool restore(Expedition& expedition, const std::filesystem::path& file);
bool save(const Expedition& expedition, const std::filesystem::path& file,
          const std::function<void()>& before_publish = {});

}  // namespace wds::expedition
