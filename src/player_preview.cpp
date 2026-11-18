#include <raylib.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <iostream>
#include <map>

#include "platform_project.hpp"
#include "player_view.hpp"

namespace wds::player {
namespace {

using namespace wds::platform;

std::string game_title(std::string title) {
  bool first = true;
  for (auto& ch : title) {
    if (ch == '-') {
      ch = ' ';
      first = true;
    } else if (first) {
      ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
      first = false;
    }
  }
  return title;
}

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

struct Movement {
  SceneInput keys;
  std::string hint;
};

Movement movement(const std::optional<SceneControls>& controls) {
  if (!controls) return {};
  Movement result;
  constexpr std::array labels{"Left", "Right", "Up", "Down"};
  constexpr std::array default_keys{'A', 'D', 'W', 'S'};
  auto arrows_and_wasd = true;
  for (std::size_t direction = 0; direction < labels.size(); ++direction) {
    const auto& keys = controls->keys[direction];
    result.keys |= keys;
    const auto arrows = static_cast<std::size_t>(SceneKey::left);
    arrows_and_wasd = arrows_and_wasd && keys.test(default_keys[direction] - 'A') &&
                      keys.test(arrows + direction);
    for (std::size_t index = 0; index < keys.size(); ++index) {
      if (!keys.test(index)) continue;
      if (!result.hint.empty()) result.hint += "  ";
      result.hint += labels[direction][0];
      result.hint += ' ';
      result.hint +=
          index < arrows ? std::string(1, static_cast<char>('A' + index)) : labels[index - arrows];
      break;
    }
  }
  if (arrows_and_wasd) result.hint = "WASD / arrows to move";
  return result;
}

const char* status_text(const SceneSnapshot& scene, bool started, bool focused) {
  if (scene.outcome == SceneOutcome::won) return "Delivered!";
  if (scene.outcome == SceneOutcome::lost) return "Delivery failed.";
  if (scene.outcome == SceneOutcome::timed_out) return "Time is up.";
  if (scene.ticks == SceneSession::max_ticks) return "Playback finished.";
  if (!focused) return "Paused";
  if (!started) return "Reach the goal";
  return "Playing";
}

void draw_tiles(const TileWorld& terrain, float scale, float left, float top, bool canopy) {
  const auto corner = terrain.corner();
  const float step = TileWorld::tile_size * scale;
  for (int y = corner.y; y < corner.y + TileWorld::side; ++y) {
    for (int x = corner.x; x < corner.x + TileWorld::side; ++x) {
      if (!terrain.resident({x, y})) continue;
      auto tile = terrain.tile({x, y});
      if (canopy && tile != Tile::canopy) continue;
      if (!canopy && tile == Tile::canopy) tile = Tile::grass;
      const auto color = tile_color(tile);
      const Color tint{static_cast<unsigned char>((color >> 16) & 255),
                       static_cast<unsigned char>((color >> 8) & 255),
                       static_cast<unsigned char>(color & 255), 255};
      DrawRectangleRec(
          {left + static_cast<float>(x) * step, top - static_cast<float>(y + 1) * step, step, step},
          tint);
    }
  }
}

std::string_view workshop_feedback(std::string_view code) {
  if (code == "ok") return "Done.";
  if (code == "source_out_of_reach") return "Move closer to the source.";
  if (code == "source_empty") return "Source depleted.";
  if (code == "container_full") return "Container full.";
  if (code == "station_locked") return "Station or recipe locked.";
  if (code == "missing_ingredients") return "Missing ingredients.";
  return code;
}

struct WorkshopPanel {
  bool open = false;
  std::size_t selected = 0;
  std::string result;

  void step(SceneSession& session) {
    const auto options = session.workshop()->actions(true);
    if (options.empty()) return;
    selected = std::min(selected, options.size() - 1);
    if (IsKeyPressed(KEY_UP) && selected > 0) --selected;
    if (IsKeyPressed(KEY_DOWN) && selected + 1 < options.size()) ++selected;
    if (IsKeyPressed(KEY_ENTER)) result = workshop_feedback(session.work(options[selected]));
  }

  void paint(const items::Workshop& workshop) const {
    const auto left = 24;
    const auto top = 46;
    const auto width = GetScreenWidth() - 48;
    const auto height = GetScreenHeight() - 92;
    DrawRectangle(left, top, width, height, {16, 21, 31, 248});
    DrawText("Items and recipes", left + 12, top + 10, 18, RAYWHITE);
    DrawText("Up/Down: select    Enter: run    Tab: close", left + 12, top + 32, 12, LIGHTGRAY);
    const auto inventory = [&](const char* title, const items::Container& items, int x) {
      DrawText(title, x, top + 55, 14, RAYWHITE);
      const auto lines =
          std::min(items.size(), static_cast<std::size_t>(std::max(0, (height - 190) / 35)));
      for (std::size_t i = 0; i < lines; ++i) {
        const auto& stack = items[i];
        const auto label = stack.id + " x" + std::to_string(stack.count);
        DrawText(label.c_str(), x, top + 75 + static_cast<int>(i) * 14, 11, LIGHTGRAY);
      }
      if (items.size() > lines)
        DrawText(TextFormat("+%d more slots", static_cast<int>(items.size() - lines)), x,
                 top + 75 + static_cast<int>(lines) * 14, 10, LIGHTGRAY);
    };
    const auto& state = workshop.state();
    inventory("Bag", state.bag, left + 12);
    inventory("Chest", state.chest, left + width / 3);
    inventory("Drops", state.loot, left + 2 * width / 3);
    const auto options = workshop.actions(true);
    if (!options.empty()) {
      const auto current = std::min(selected, options.size() - 1);
      const auto& action = options[current];
      const bool recipe = action.type == "recipe";
      const auto label = (recipe ? workshop.catalog().recipes.at(action.id).method : action.type) +
                         "  " + action.id;
      DrawText(label.c_str(), left + 12, top + height - 60, 18, SKYBLUE);
      DrawText(
          TextFormat("%d / %d", static_cast<int>(current + 1), static_cast<int>(options.size())),
          left + 12, top + height - 39, 12, LIGHTGRAY);
    }
    if (!result.empty()) DrawText(result.c_str(), left + 12, top + height - 21, 12, RAYWHITE);
  }
};

void draw(const SceneSession& session, const Sprites& sprites, const SceneBounds& bounds,
          const char* status, const std::string& controls_hint, const WorkshopPanel& workshop) {
  const auto& scene = session.snapshot();
  const auto& terrain = session.terrain();
  const auto width = static_cast<float>(GetScreenWidth());
  const auto height = static_cast<float>(GetScreenHeight());
  auto scale = std::min({2.0f, width / (static_cast<float>(bounds.right - bounds.left) + 56),
                         height / (static_cast<float>(bounds.top - bounds.bottom) + 86)});
  auto origin_x = static_cast<float>(bounds.left + bounds.right) * 0.5f;
  auto origin_y = static_cast<float>(bounds.bottom + bounds.top) * 0.5f;
  if (terrain) {
    const auto& player = scene.entities[session.controls()->player];
    origin_x = static_cast<float>(player.x);
    origin_y = static_cast<float>(player.y);
    scale = std::max({1.0f, width / 420, height / 350});
  }
  const auto left = width * 0.5f - origin_x * scale;
  const auto top = height * 0.5f + origin_y * scale;
  BeginDrawing();
  ClearBackground({22, 30, 40, 255});
  if (terrain) draw_tiles(*terrain, scale, left, top, false);
  for (const auto& entity : scene.entities) {
    const Vector2 center{left + static_cast<float>(entity.x) * scale,
                         top - static_cast<float>(entity.y) * scale};
    sprites.draw(entity, center, scale);
  }
  if (terrain) {
    draw_tiles(*terrain, scale, left, top, true);
    const auto night = static_cast<unsigned char>(255 - terrain->daylight(scene.ticks));
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), {16, 23, 52, night});
  }
  DrawText(status, 20, 18, 22, RAYWHITE);
  const auto rate = SceneClock::ticks_per_second;
  const auto seconds = (SceneSession::max_ticks - scene.ticks + rate - 1) / rate;
  if (scene.interactive)
    DrawText(TextFormat("%llus", static_cast<unsigned long long>(seconds)), GetScreenWidth() - 64,
             18, 22, RAYWHITE);
  const bool terminal =
      scene.outcome != SceneOutcome::playing || scene.ticks == SceneSession::max_ticks;
  const char* hint = terminal ? "Enter: replay" : controls_hint.c_str();
  const auto hint_size = MeasureText(hint, 16) > GetScreenWidth() - 132 ? 13 : 16;
  DrawText(hint, 20, GetScreenHeight() - 30, hint_size, LIGHTGRAY);
  DrawText("Esc: quit", GetScreenWidth() - 100, GetScreenHeight() - 30, 16, LIGHTGRAY);
  if (session.workshop()) {
    DrawText("Tab: items", 20, GetScreenHeight() - 52, 14, LIGHTGRAY);
    if (workshop.open) workshop.paint(*session.workshop());
  }
  EndDrawing();
}

}  // namespace

bool play_scene(SceneSession& session, const char* title) {
  const auto& snapshot = session.snapshot();
  const auto bounds = scene_play_bounds(snapshot);
  const auto controls = movement(session.controls());
  const auto window_title = title ? std::string(title) : game_title(snapshot.project_id);
  std::string caption;
  SetConfigFlags(FLAG_WINDOW_RESIZABLE);
  InitWindow(900, 570, window_title.c_str());
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
      bool started = !snapshot.interactive;
      SceneInput blocked;
      WorkshopPanel workshop;
      std::string_view previous_status;
      while (!WindowShouldClose()) {
        const auto now = Clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(now - previous);
        previous = now;
        const bool focused = IsWindowFocused();
        if (focused) {
          const auto keys = read_keys();
          // Keys held across a focus change need a release before they can move again.
          blocked = had_focus ? blocked & keys : keys;
          if (session.workshop() && IsKeyPressed(KEY_TAB)) {
            workshop.open = !workshop.open;
            blocked = keys;
          }
          if (workshop.open) {
            workshop.step(session);
          } else if (had_focus) {
            const auto input = keys & ~blocked;
            const bool terminal = snapshot.outcome != SceneOutcome::playing ||
                                  snapshot.ticks == SceneSession::max_ticks;
            if (terminal && IsKeyPressed(KEY_ENTER)) {
              session.restart();
              started = !snapshot.interactive;
              blocked = keys;
              clock = {};
            } else if (!terminal) {
              started = started || (input & controls.keys).any();
              if (started) {
                const auto remaining = SceneSession::max_ticks - snapshot.ticks;
                session.advance(std::min(clock.consume(elapsed), remaining), input);
              }
            }
          }
        }
        had_focus = focused;
        const auto* status = status_text(snapshot, started, focused);
        if (previous_status != status) {
          caption = window_title + " - " + status;
          SetWindowTitle(caption.c_str());
          previous_status = status;
        }
        draw(session, sprites, bounds, status, controls.hint, workshop);
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
