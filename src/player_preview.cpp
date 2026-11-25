#include <raylib.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <iostream>
#include <map>
#include <numbers>

#include "expedition.hpp"
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

  template <typename Apply>
  void step(const items::Workshop& workshop, bool gathering, Apply apply) {
    const auto options = workshop.actions(gathering);
    if (options.empty()) return;
    selected = std::min(selected, options.size() - 1);
    if (IsKeyPressed(KEY_UP) && selected > 0) --selected;
    if (IsKeyPressed(KEY_DOWN) && selected + 1 < options.size()) ++selected;
    if (IsKeyPressed(KEY_ENTER)) result = workshop_feedback(apply(options[selected]));
  }

  void paint(const items::Workshop& workshop, bool gathering = true) const {
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
    const auto options = workshop.actions(gathering);
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
  if (terrain) {
    draw_tiles(*terrain, scale, left, top, false);
    constexpr auto units = static_cast<float>(TileWorld::tile_size);
    for (const auto& site : session.populations()) {
      const auto color = species_color(site.species);
      const Color tint{static_cast<unsigned char>(color >> 16),
                       static_cast<unsigned char>(color >> 8), static_cast<unsigned char>(color),
                       255};
      DrawCircleV({left + (static_cast<float>(site.point.x) + 0.5f) * units * scale,
                   top - (static_cast<float>(site.point.y) + 0.5f) * units * scale},
                  3 * scale, tint);
    }
  }
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
            workshop.step(*session.workshop(), true,
                          [&](const items::Action& action) { return session.work(action); });
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

bool play_voxel(VoxelScene& scene, const char* title) {
  const auto caption = title                 ? std::string(title)
                       : scene.title.empty() ? game_title(scene.source.project_id)
                                             : scene.title;
  SetConfigFlags(FLAG_WINDOW_RESIZABLE);
  InitWindow(900, 570, caption.c_str());
  if (!IsWindowReady()) return false;
  SetWindowMinSize(360, 260);
  SetTargetFPS(30);
  constexpr int frame_width = 320;
  constexpr int frame_height = 200;
  std::vector<Color> pixels(frame_width * frame_height);
  const Image image{pixels.data(), frame_width, frame_height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
  const auto texture = LoadTextureFromImage(image);
  const bool ready = IsTextureValid(texture);
  if (ready) {
    voxel::Player player;
    const auto& terrain = scene.world.terrain();
    const bool workyard = terrain && terrain->workyard();
    voxel::SkyPreview sky;
    std::optional<voxel::LightField> light;
    WorkshopPanel workshop;
    SceneInput blocked;
    bool needs_frame = true;
    bool had_focus = false;
    while (!WindowShouldClose()) {
      const bool focused = IsWindowFocused();
      if (focused) {
        const auto keys = read_keys();
        blocked = had_focus ? blocked & keys : keys;
        if (scene.workshop && IsKeyPressed(KEY_TAB)) {
          workshop.open = !workshop.open;
          blocked = keys;
        }
        if (workshop.open) {
          workshop.step(*scene.workshop, false,
                        [&](const items::Action& action) { return scene.workshop->apply(action); });
        } else if (had_focus) {
          const auto before = player;
          const auto input = keys & ~blocked;
          const auto axis = [&](SceneKey positive, SceneKey negative) {
            return static_cast<int>(input.test(static_cast<std::size_t>(positive))) -
                   static_cast<int>(input.test(static_cast<std::size_t>(negative)));
          };
          const auto key = [](std::string_view name) { return *scene_key(name); };
          const auto dt = std::min(static_cast<double>(GetFrameTime()), 0.05);
          player.yaw = std::remainder(player.yaw + axis(SceneKey::right, SceneKey::left) * dt * 1.8,
                                      2 * std::numbers::pi);
          player.pitch =
              std::clamp(player.pitch + axis(SceneKey::up, SceneKey::down) * dt * 1.4, -1.3, 1.3);
          if (terrain && (IsKeyPressed(KEY_L) || IsKeyPressed(KEY_P))) {
            if (IsKeyPressed(KEY_L)) sky.night = !sky.night;
            if (IsKeyPressed(KEY_P)) ++sky.day;
            light.reset();
            needs_frame = true;
          }
          if (workyard) {
            if (IsKeyPressed(KEY_E)) player.toggle_cart(scene.world);
            if (IsKeyPressed(KEY_F) && player.advance_workyard(scene.world, sky.tick()).changed) {
              light.reset();
              needs_frame = true;
            }
          }
          if (player.cart) {
            const int dx = IsKeyPressed(KEY_D) - IsKeyPressed(KEY_A);
            const int dz = IsKeyPressed(KEY_S) - IsKeyPressed(KEY_W);
            if (dx || dz) player.move_cart(scene.world, dx, dz);
          } else {
            for (int step = 0; step < 2; ++step)
              player.step(scene.world, axis(key("W"), key("S")), axis(key("D"), key("A")),
                          step == 0 && IsKeyPressed(KEY_SPACE), dt * .5);
          }
          if ((IsMouseButtonPressed(MOUSE_BUTTON_LEFT) ||
               IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) &&
              scene.edit(player.feet, player.eye(), player.view(),
                         IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))) {
            light.reset();
            needs_frame = true;
          }
          needs_frame = needs_frame || before.yaw != player.yaw || before.pitch != player.pitch ||
                        before.feet.x != player.feet.x || before.feet.y != player.feet.y ||
                        before.feet.z != player.feet.z;
        }
      }
      had_focus = focused;
      if (needs_frame) {
        const auto camera = player.camera();
        if (terrain && (!light || !light->covers(player.feet)))
          light = scene.world.light_field(player.feet, sky.tick());
        const auto* lighting = light ? &*light : nullptr;
        for (int y = 0; y < frame_height; ++y) {
          for (int x = 0; x < frame_width; ++x) {
            const auto ray =
                camera.ray(1.5 * (x + .5) / frame_width - .75, .47 - .94 * (y + .5) / frame_height);
            const auto rgb = voxel::pixel(scene.world, camera.eye, ray, lighting);
            pixels[static_cast<std::size_t>(y * frame_width + x)] = {
                static_cast<unsigned char>((rgb >> 16) & 255),
                static_cast<unsigned char>((rgb >> 8) & 255), static_cast<unsigned char>(rgb & 255),
                255};
          }
        }
        UpdateTexture(texture, pixels.data());
        needs_frame = false;
      }
      BeginDrawing();
      const int width = GetScreenWidth();
      const int height = GetScreenHeight();
      DrawTexturePro(texture, {0, 0, frame_width, frame_height},
                     {0, 0, static_cast<float>(width), static_cast<float>(height)}, {}, 0, WHITE);
      DrawLine(width / 2 - 8, height / 2, width / 2 + 8, height / 2, WHITE);
      DrawLine(width / 2, height / 2 - 8, width / 2, height / 2 + 8, WHITE);
      const int hint_height = light ? 48 : 34;
      DrawRectangle(0, height - hint_height, width, hint_height, {16, 21, 31, 200});
      if (light) {
        const auto weather = std::string(light->weather()) + "   L daylight   P next day";
        DrawText(weather.c_str(), 12, height - 43, 13, RAYWHITE);
      }
      DrawText(workyard ? (player.cart ? "A west   D east   W north   S south   E exit"
                                       : "WASD move   E board cart   F advance water and fire")
                        : "WASD move   Arrows look   Space jump",
               12, height - 29, 13, RAYWHITE);
      DrawText(scene.workshop ? "Left break   Right place   Tab items   Esc quit"
                              : "Left break   Right place   Esc quit",
               12, height - 15, 13, RAYWHITE);
      if (workshop.open) workshop.paint(*scene.workshop, false);
      EndDrawing();
    }
    UnloadTexture(texture);
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
  auto result = start_scene(path, opened.project->revision, true);
  if (!result.session && !result.voxel_scene) {
    std::cerr << scene_result_json(result) << '\n';
    return 1;
  }
  auto journey = wds::expedition::load(path, opened.project->revision);
  if (journey.expedition)
    return wds::player::play_expedition(*journey.expedition, journey.icons, {}) ? 0 : 1;
  if (journey.code != "no_expedition") {
    std::cerr << journey.code << ": " << journey.location << '\n';
    return 1;
  }
  if (result.voxel_scene)
    return wds::player::play_voxel(*result.voxel_scene, result.voxel_scene->title.empty()
                                                            ? "WDS Voxel Preview"
                                                            : nullptr)
               ? 0
               : 1;
  if (wds::player::play_scene(*result.session, "WDS Scene Preview")) return 0;
  std::cerr << "Unable to prepare graphics for playback.\n";
  return 1;
}
#endif
