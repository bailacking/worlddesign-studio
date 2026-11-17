#pragma once

#include <array>
#include <bitset>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace wds::platform {

enum class SceneRole { none, player, wall, goal, hazard };
enum class SceneOutcome { playing, won, lost, timed_out };
enum class SceneKey { a = 0, left = 26, right, up, down, count };

std::optional<SceneKey> scene_key(std::string_view name);
std::string_view outcome_name(SceneOutcome outcome);

using SceneInput = std::bitset<static_cast<std::size_t>(SceneKey::count)>;

struct SceneEntity {
  std::string id;
  std::int64_t x = 0;
  std::int64_t y = 0;
  std::int64_t vx = 0;
  std::int64_t vy = 0;
  std::int64_t half_width = 0;
  std::int64_t half_height = 0;
  std::string sprite{};
  SceneRole role = SceneRole::none;
};

struct SceneControls {
  std::size_t player;
  std::int64_t speed;
  std::array<SceneInput, 4> keys;  // Left, right, up, down.
};

struct SceneSprite {
  std::string id;
  unsigned width;
  unsigned height;
  std::vector<unsigned char> rgba;
};

struct SceneSnapshot {
  std::uint64_t revision = 0;
  std::string scene_id;
  std::uint64_t ticks = 0;
  std::vector<SceneEntity> entities;
  bool interactive = false;
  SceneOutcome outcome = SceneOutcome::playing;
};

struct SceneBounds {
  std::int64_t left;
  std::int64_t bottom;
  std::int64_t right;
  std::int64_t top;
};

SceneBounds scene_play_bounds(const SceneSnapshot& scene);

class SceneClock {
 public:
  static constexpr std::uint64_t ticks_per_second = 30;

  std::uint64_t consume(std::chrono::nanoseconds elapsed);

 private:
  // Accumulate elapsed nanoseconds * tick rate so 1/30 s never needs rounding.
  std::uint64_t remainder_ = 0;
};

struct SceneResult;

class SceneSession {
 public:
  static constexpr std::uint64_t max_ticks = 600;

  const SceneSnapshot& snapshot() const { return state_; }
  const std::vector<SceneSprite>& sprites() const { return sprites_; }
  bool advance(std::uint64_t ticks, const SceneInput& input = {});

 private:
  friend SceneResult start_scene(const std::filesystem::path&, std::uint64_t);

  SceneSession() = default;
  void step(const SceneInput& input);

  SceneSnapshot state_;
  std::optional<SceneControls> controls_;
  std::vector<SceneSprite> sprites_;
};

}  // namespace wds::platform
