#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string_view>
#include <vector>

namespace w4 {

constexpr int width = 960;
constexpr int height = 540;
constexpr int tile_size = 32;
constexpr int sprite_size = 48;
constexpr int frame_count = 240;
constexpr int warmup_frames = 30;

struct Color {
  unsigned char r, g, b;
};

inline constexpr Color sea{19, 30, 40};
inline constexpr Color rail{34, 57, 64};
inline constexpr Color light{251, 212, 119};
inline constexpr Color exit_open{111, 229, 181};

struct Point {
  int x, y;
};

struct Movement {
  bool left{}, right{}, up{}, down{};
};

enum class Asset { tile, body, equipment };

inline constexpr std::array asset_names{"tile.png", "body.png", "equipment.png"};

struct Scene {
  Point actor{72, height / 2};
  std::array<bool, 3> collected{};
  bool won = false;
  int variant = 0;

  static constexpr std::array beacons{Point{240, height / 2}, Point{480, height / 2},
                                      Point{720, height / 2}};

  int score() const { return static_cast<int>(std::ranges::count(collected, true)); }

  bool advance(Movement movement) {
    if (won) return false;
    actor.x = std::clamp(actor.x + 4 * (movement.right - movement.left), 24, width - 24);
    actor.y = std::clamp(actor.y + 4 * (movement.down - movement.up), 100, height - 56);
    bool picked_up = false;
    for (std::size_t index = 0; index < beacons.size(); ++index) {
      const auto delta_x = actor.x - beacons[index].x;
      const auto delta_y = actor.y - beacons[index].y;
      if (!collected[index] && delta_x * delta_x + delta_y * delta_y <= 28 * 28) {
        collected[index] = true;
        picked_up = true;
      }
    }
    won = score() == 3 && actor.x >= width - 56 && std::abs(actor.y - height / 2) < 56;
    return picked_up;
  }
};

template <class Renderer>
void draw(const Scene& scene, Renderer& renderer) {
  renderer.fill(0, 0, width, height, sea);
  for (int y = 64; y < height - 32; y += tile_size)
    for (int x = 0; x < width; x += tile_size) renderer.texture(scene.variant, Asset::tile, x, y);
  renderer.fill(0, height / 2 - 1, width, 2, rail);
  renderer.fill(width - 40, height / 2 - 52, 12, 104, scene.score() == 3 ? exit_open : rail);
  for (std::size_t index = 0; index < scene.beacons.size(); ++index) {
    if (scene.collected[index]) continue;
    const auto marker = scene.beacons[index];
    renderer.fill(marker.x - 9, marker.y - 9, 18, 18, light);
    renderer.fill(marker.x - 4, marker.y - 4, 8, 8, sea);
  }
  const auto left = scene.actor.x - sprite_size / 2;
  const auto top = scene.actor.y - sprite_size / 2;
  renderer.texture(scene.variant, Asset::body, left, top);
  renderer.texture(scene.variant, Asset::equipment, left, top);
}

using Clock = std::chrono::steady_clock;

inline double elapsed_ms(Clock::time_point start, Clock::time_point finish) {
  return std::chrono::duration<double, std::milli>(finish - start).count();
}

inline double percentile(std::vector<double> samples, double fraction) {
  std::ranges::sort(samples);
  return samples[static_cast<std::size_t>(std::ceil(fraction * samples.size())) - 1];
}

template <class Render, class ReadBack>
int benchmark(const char* name, Render render, ReadBack read_back) {
  Scene scene;
  std::vector<double> draw_times;
  std::vector<double> finished_times;
  int pickups = 0;
  int win_frame = 0;
  unsigned int observed_pixels = 0;
  for (int frame = -warmup_frames; frame < frame_count; ++frame) {
    if (frame == 0) scene = Scene{};
    scene.variant = (frame >= frame_count / 2) ? 1 : 0;
    const auto start = Clock::now();
    const bool picked_up = scene.advance({.right = true});
    if (frame >= 0) {
      pickups += picked_up;
      if (scene.won && win_frame == 0) win_frame = frame + 1;
    }
    render(scene);
    const auto submitted = Clock::now();
    observed_pixels += read_back();
    const auto finished = Clock::now();
    if (frame >= 0) {
      draw_times.push_back(elapsed_ms(start, submitted));
      finished_times.push_back(elapsed_ms(start, finished));
    }
  }
  std::printf(
      "{\"backend\":\"%s\",\"frames\":%d,\"draw_p50_ms\":%.4f,\"draw_p95_ms\":%.4f,"
      "\"readback_p50_ms\":%.4f,\"readback_p95_ms\":%.4f,\"win_frame\":%d,"
      "\"pickups\":%d,\"observed_pixels\":%u}\n",
      name, frame_count, percentile(draw_times, .5), percentile(draw_times, .95),
      percentile(finished_times, .5), percentile(finished_times, .95), win_frame, pickups,
      observed_pixels);
  return scene.won && pickups == 3 && observed_pixels > 0 ? 0 : 1;
}

}  // namespace w4
