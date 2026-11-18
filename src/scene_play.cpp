#include "scene_play.hpp"

#include <algorithm>
#include <limits>

namespace wds::platform {
namespace {

struct Interval {
  std::int64_t min;
  std::int64_t max;
};

Interval bounds(std::int64_t center, std::int64_t half_extent) {
  return {center - half_extent, center + half_extent};
}

bool overlaps(Interval first, Interval second) {
  return first.min < second.max && second.min < first.max;
}

std::int64_t move_axis(const SceneSnapshot& state, SceneEntity& player, std::int64_t delta,
                       bool horizontal, const TileWorld* terrain) {
  if (delta == 0) return 0;
  auto& position = horizontal ? player.x : player.y;
  const auto primary = bounds(position, horizontal ? player.half_width : player.half_height);
  const auto cross =
      horizontal ? bounds(player.y, player.half_height) : bounds(player.x, player.half_width);
  const auto avoid = [&](Interval obstacle) {
    if (delta > 0 && primary.max <= obstacle.min)
      delta = std::min(delta, obstacle.min - primary.max);
    else if (delta < 0 && primary.min >= obstacle.max)
      delta = std::max(delta, obstacle.max - primary.min);
  };
  if (terrain) {
    const auto low = primary.min + std::min<std::int64_t>(delta, 0);
    const auto high = primary.max - 1 + std::max<std::int64_t>(delta, 0);
    const auto first =
        horizontal ? TileWorld::cell(low, cross.min) : TileWorld::cell(cross.min, low);
    const auto last =
        horizontal ? TileWorld::cell(high, cross.max - 1) : TileWorld::cell(cross.max - 1, high);
    for (int y = first.y; y <= last.y; ++y) {
      for (int x = first.x; x <= last.x; ++x) {
        if (!terrain->solid({x, y})) continue;
        const auto start = (horizontal ? x : y) * TileWorld::tile_size;
        avoid({start, start + TileWorld::tile_size});
      }
    }
  }
  for (const auto& wall : state.entities) {
    if (wall.role != SceneRole::wall ||
        !overlaps(cross,
                  horizontal ? bounds(wall.y, wall.half_height) : bounds(wall.x, wall.half_width)))
      continue;
    avoid(horizontal ? bounds(wall.x, wall.half_width) : bounds(wall.y, wall.half_height));
  }
  position += delta;
  return delta;
}

SceneOutcome check_triggers(const SceneSnapshot& state, const SceneEntity& player, std::int64_t dx,
                            std::int64_t dy) {
  // Check each swept segment so the untouched corner of the enclosing box
  // cannot trigger a result during diagonal movement.
  const Interval across_x{player.x - std::max(dx, std::int64_t{0}) - player.half_width,
                          player.x - std::min(dx, std::int64_t{0}) + player.half_width};
  const Interval across_y{player.y - std::max(dy, std::int64_t{0}) - player.half_height,
                          player.y - std::min(dy, std::int64_t{0}) + player.half_height};
  const auto final_x = bounds(player.x, player.half_width);
  const auto initial_y = bounds(player.y - dy, player.half_height);
  auto outcome = SceneOutcome::playing;
  for (const auto& entity : state.entities) {
    if (entity.role != SceneRole::goal && entity.role != SceneRole::hazard) continue;
    const auto x = bounds(entity.x, entity.half_width);
    const auto y = bounds(entity.y, entity.half_height);
    if (!(overlaps(across_x, x) && overlaps(initial_y, y)) &&
        !(overlaps(final_x, x) && overlaps(across_y, y)))
      continue;
    if (entity.role == SceneRole::hazard) return SceneOutcome::lost;
    outcome = SceneOutcome::won;
  }
  return outcome;
}

}  // namespace

std::optional<SceneKey> scene_key(std::string_view name) {
  if (name.size() == 1 && 'A' <= name[0] && name[0] <= 'Z')
    return static_cast<SceneKey>(name[0] - 'A');
  if (name == "Left") return SceneKey::left;
  if (name == "Right") return SceneKey::right;
  if (name == "Up") return SceneKey::up;
  if (name == "Down") return SceneKey::down;
  return std::nullopt;
}

std::string_view outcome_name(SceneOutcome outcome) {
  switch (outcome) {
    case SceneOutcome::won:
      return "won";
    case SceneOutcome::lost:
      return "lost";
    case SceneOutcome::timed_out:
      return "timed_out";
    default:
      return "playing";
  }
}

SceneBounds scene_play_bounds(const SceneSnapshot& scene) {
  if (!scene.interactive) return {-320, -180, 320, 180};
  constexpr auto high = std::numeric_limits<std::int64_t>::max();
  SceneBounds result{high, high, -high, -high};
  for (const auto& entity : scene.entities) {
    if (entity.role == SceneRole::none) continue;
    result.left = std::min(result.left, entity.x - entity.half_width);
    result.bottom = std::min(result.bottom, entity.y - entity.half_height);
    result.right = std::max(result.right, entity.x + entity.half_width);
    result.top = std::max(result.top, entity.y + entity.half_height);
  }
  return result;
}

std::uint64_t SceneClock::consume(std::chrono::nanoseconds elapsed) {
  constexpr auto max_frame =
      std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::milliseconds(250));
  constexpr auto units_per_second = 1'000'000'000ULL;
  const auto clamped = std::clamp(elapsed, std::chrono::nanoseconds::zero(), max_frame);
  remainder_ += static_cast<std::uint64_t>(clamped.count()) * ticks_per_second;
  const auto ticks = remainder_ / units_per_second;
  remainder_ %= units_per_second;
  return ticks;
}

bool SceneSession::advance(std::uint64_t ticks, const SceneInput& input) {
  if (ticks > max_ticks - state_.ticks) return false;
  if (controls_) {
    for (std::uint64_t index = 0; index < ticks && state_.outcome == SceneOutcome::playing;
         ++index) {
      dispatch_behavior(behavior::Event::tick);
      step(input);
    }
    if (state_.ticks == max_ticks && state_.outcome == SceneOutcome::playing)
      state_.outcome = SceneOutcome::timed_out;
    return true;
  }
  if (behavior_) {
    for (std::uint64_t index = 0; index < ticks; ++index) {
      dispatch_behavior(behavior::Event::tick);
      for (auto& entity : state_.entities) {
        entity.x += entity.vx;
        entity.y += entity.vy;
      }
    }
    state_.ticks += ticks;
    return true;
  }
  const auto steps = static_cast<std::int64_t>(ticks);
  for (auto& entity : state_.entities) {
    entity.x += entity.vx * steps;
    entity.y += entity.vy * steps;
  }
  state_.ticks += ticks;
  return true;
}

void SceneSession::restart() {
  state_.entities = initial_entities_;
  state_.ticks = 0;
  state_.outcome = SceneOutcome::playing;
  if (terrain_) {
    const auto& player = state_.entities[controls_->player];
    terrain_->stream(TileWorld::cell(player.x, player.y));
  }
  if (behavior_) {
    state_.behavior_state = behavior_->initial_state;
    dispatch_behavior(behavior::Event::start);
  }
}

void SceneSession::dispatch_behavior(behavior::Event event) {
  if (!behavior_) return;
  auto& values = *state_.behavior_state;
  behavior_->dispatch(event, values);
  if (!velocity_binding_) return;
  const auto& [entity, state] = *velocity_binding_;
  state_.entities[entity].vx =
      std::clamp<std::int64_t>(values.at(state), -scene_field_limit, scene_field_limit);
}

void SceneSession::step(const SceneInput& input) {
  const auto& controls = *controls_;
  const auto direction = [&](std::size_t positive, std::size_t negative) {
    return static_cast<int>((input & controls.keys[positive]).any()) -
           static_cast<int>((input & controls.keys[negative]).any());
  };
  auto& player = state_.entities[controls.player];
  const auto* terrain = terrain_ ? &*terrain_ : nullptr;
  const auto x = move_axis(state_, player, controls.speed * direction(1, 0), true, terrain);
  const auto y = move_axis(state_, player, controls.speed * direction(2, 3), false, terrain);
  if (terrain_) terrain_->stream(TileWorld::cell(player.x, player.y));
  for (auto& entity : state_.entities) {
    if (entity.role != SceneRole::none) continue;
    entity.x += entity.vx;
    entity.y += entity.vy;
  }
  state_.outcome = check_triggers(state_, player, x, y);
  ++state_.ticks;
}

}  // namespace wds::platform
