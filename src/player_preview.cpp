#include <raylib.h>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <map>

#include "platform_project.hpp"
#include "player_view.hpp"

namespace wds::player {
namespace {

using namespace wds::platform;

SceneInput read_keys() {
  SceneInput keys;
  for (int letter = KEY_A; letter <= KEY_Z; ++letter)
    keys.set(static_cast<std::size_t>(letter - KEY_A), IsKeyDown(letter));
  constexpr std::array arrows{KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN};
  for (std::size_t index = 0; index < arrows.size(); ++index)
    keys.set(static_cast<std::size_t>(SceneKey::left) + index, IsKeyDown(arrows[index]));
  return keys;
}

class Sprites {
 public:
  explicit Sprites(const SceneSession& session) {
    for (const auto& sprite : session.sprites()) {
      Image pixels{const_cast<unsigned char*>(sprite.rgba.data()), static_cast<int>(sprite.width),
                   static_cast<int>(sprite.height), 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
      textures_.emplace(sprite.id, LoadTextureFromImage(pixels));
    }
  }
  Sprites(const Sprites&) = delete;
  Sprites& operator=(const Sprites&) = delete;
  ~Sprites() {
    for (const auto& [id, texture] : textures_) UnloadTexture(texture);
  }
  bool ready() const {
    return std::ranges::all_of(textures_,
                               [](const auto& item) { return IsTextureValid(item.second); });
  }
  void draw(const SceneEntity& entity, Vector2 center, float scale) const {
    if (entity.half_width == 0) {
      DrawCircleV(center, 5, SKYBLUE);
      return;
    }
    const auto half_width = static_cast<float>(entity.half_width) * scale;
    const auto half_height = static_cast<float>(entity.half_height) * scale;
    const Rectangle target{center.x - half_width, center.y - half_height, half_width * 2,
                           half_height * 2};
    const auto match = textures_.find(entity.sprite);
    if (match != textures_.end()) {
      const auto texture = match->second;
      const Rectangle source{0, 0, static_cast<float>(texture.width),
                             static_cast<float>(texture.height)};
      DrawTexturePro(texture, source, target, {}, 0, WHITE);
      return;
    }
    Color color = SKYBLUE;
    if (entity.role == SceneRole::goal) color = LIME;
    if (entity.role == SceneRole::hazard) color = MAROON;
    DrawRectangleRec(target, color);
  }

 private:
  std::map<std::string, Texture2D> textures_;
};

void draw(const SceneSnapshot& scene, const Sprites& sprites, const SceneBounds& bounds,
          bool focused) {
  const auto width = static_cast<float>(GetScreenWidth());
  const auto height = static_cast<float>(GetScreenHeight());
  const auto scale = std::min({2.0f, width / (static_cast<float>(bounds.right - bounds.left) + 56),
                               height / (static_cast<float>(bounds.top - bounds.bottom) + 86)});
  const auto origin_x = static_cast<float>(bounds.left + bounds.right) * 0.5f;
  const auto origin_y = static_cast<float>(bounds.bottom + bounds.top) * 0.5f;
  BeginDrawing();
  ClearBackground({22, 30, 40, 255});
  for (const auto& entity : scene.entities) {
    const Vector2 center{width * 0.5f + (static_cast<float>(entity.x) - origin_x) * scale,
                         height * 0.5f - (static_cast<float>(entity.y) - origin_y) * scale};
    sprites.draw(entity, center, scale);
  }
  const char* status = "Playing";
  if (scene.outcome == SceneOutcome::won)
    status = "Delivered!";
  else if (scene.outcome == SceneOutcome::lost)
    status = "Delivery failed.";
  else if (scene.outcome == SceneOutcome::timed_out)
    status = "Time is up.";
  else if (!focused)
    status = "Paused";
  else if (scene.ticks == SceneSession::max_ticks)
    status = "Playback finished.";
  DrawText(status, 20, 18, 22, RAYWHITE);
  DrawText(TextFormat("Tick %llu / %llu", static_cast<unsigned long long>(scene.ticks),
                      static_cast<unsigned long long>(SceneSession::max_ticks)),
           20, GetScreenHeight() - 30, 17, LIGHTGRAY);
  DrawText("Esc: close", GetScreenWidth() - 106, GetScreenHeight() - 30, 17, LIGHTGRAY);
  EndDrawing();
}

}  // namespace

bool play_scene(SceneSession& session, const char* title) {
  const auto bounds = scene_play_bounds(session.snapshot());
  SetConfigFlags(FLAG_WINDOW_RESIZABLE);
  InitWindow(900, 570, title);
  if (!IsWindowReady()) return false;
  SetWindowMinSize(360, 260);
  SetTargetFPS(60);
  bool ready = false;
  {
    Sprites sprites(session);
    ready = sprites.ready();
    if (ready) {
      using Clock = std::chrono::steady_clock;
      auto previous = Clock::now();
      SceneClock clock;
      bool had_focus = false;
      SceneInput blocked;
      while (!WindowShouldClose()) {
        const auto now = Clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(now - previous);
        previous = now;
        const bool focused = IsWindowFocused();
        if (focused) {
          const auto keys = read_keys();
          // Keys held across a focus change need a release before they can move again.
          blocked = had_focus ? blocked & keys : keys;
          if (had_focus) {
            const auto remaining = SceneSession::max_ticks - session.snapshot().ticks;
            session.advance(std::min(clock.consume(elapsed), remaining), keys & ~blocked);
          }
        }
        had_focus = focused;
        draw(session.snapshot(), sprites, bounds, focused);
      }
    }
  }
  CloseWindow();
  return ready;
}

}  // namespace wds::player

#ifndef WDS_PACKAGED_PLAYER
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
#else
int main(int argc, char** argv) {
#endif
  using namespace wds::platform;
  if (argc != 2) {
    std::cerr << "Usage: wds_player_preview PROJECT_DIRECTORY\n";
    return 2;
  }
  const std::filesystem::path path(argv[1]);
  const auto opened = open_project(path);
  if (!opened.project) {
    std::cerr << project_result_json(opened) << '\n';
    return 1;
  }
  auto result = start_scene(path, opened.project->revision);
  if (!result.session) {
    std::cerr << scene_result_json(result) << '\n';
    return 1;
  }
  if (wds::player::play_scene(*result.session, "WDS Scene Preview")) return 0;
  std::cerr << "Unable to prepare graphics for playback.\n";
  return 1;
}
#endif
