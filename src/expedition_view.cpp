#include <raylib.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "expedition.hpp"
#include "player_view.hpp"

namespace wds::player {
namespace {

using expedition::Action;
using expedition::Command;
using expedition::Expedition;
using expedition::Site;
using expedition::TilePoint;

constexpr Color background{17, 25, 32, 255};
constexpr Color ink{232, 238, 227, 255};
constexpr Color subdued{162, 184, 178, 255};
constexpr Color accent{245, 206, 122, 255};

Color ground(const Expedition& run, int tile) {
  if (const auto& palette = run.region().palette) {
    const auto rgb = (*palette)[static_cast<std::size_t>(tile)];
    return {static_cast<unsigned char>(rgb >> 16), static_cast<unsigned char>(rgb >> 8),
            static_cast<unsigned char>(rgb), 255};
  }
  constexpr std::array land{Color{77, 113, 80, 255}, Color{101, 132, 88, 255},
                            Color{115, 133, 95, 255}, Color{57, 70, 77, 255}};
  if (run.region().habitat == "ocean")
    return tile == 2 ? Color{50, 107, 145, 255} : Color{44, 85, 126, 255};
  if (run.region().habitat == "ember")
    return Color{static_cast<unsigned char>(126 + tile * 15), 72, 53, 255};
  if (run.region().habitat == "cavern" || run.region().habitat == "ruins")
    return Color{69, 79, static_cast<unsigned char>(91 + tile * 12), 255};
  if (run.region().habitat == "sky")
    return Color{110, static_cast<unsigned char>(132 + tile * 12), 162, 255};
  return land[static_cast<std::size_t>(tile)];
}

Color mark(const Site& site, const Expedition& run) {
  if (const auto enemy = run.state().encounters.find(site.id);
      enemy != run.state().encounters.end())
    return enemy->second > 0 ? Color{231, 117, 104, 255} : subdued;
  if (site.kind == "portal") return {165, 178, 255, 255};
  if (site.kind == "source") return {153, 223, 155, 255};
  return accent;
}

void quad(Vector2 a, Vector2 b, Vector2 c, Vector2 d, Color color) {
  DrawTriangle(a, b, c, color);
  DrawTriangle(a, c, d, color);
}

using Textures = std::map<std::string, Texture2D, std::less<>>;

void draw_map(const Expedition& run, const Site* selected, const Textures& icons) {
  constexpr float west = 30;
  constexpr float top = 105;
  constexpr float width = 675;
  constexpr float height = 420;
  const auto& region = run.region();
  const auto columns = static_cast<float>(region.width);
  const auto rows = static_cast<float>(region.height);
  const bool voxel = run.rules().voxel;
  const auto step = voxel ? std::min(width / (columns + rows), height / ((columns + rows) * 0.6f))
                          : std::min(width / columns, height / rows);
  const auto offset = west + (width - step * columns) * 0.5f;
  const auto rise = [&](TilePoint point) {
    return step * (run.terrain(point) == 3 ? 0.68f : 0.23f);
  };
  const auto center = [&](TilePoint point, bool surface) {
    const auto x = static_cast<float>(point.x);
    const auto y = static_cast<float>(point.y);
    if (!voxel) return Vector2{offset + (x + 0.5f) * step, top + (y + 0.5f) * step};
    return Vector2{west + (width + (rows - columns) * step) * 0.5f + (x - y) * step,
                   top + (x + y + 2) * step * 0.55f - (surface ? rise(point) : 0)};
  };
  for (int depth = 0; depth < region.width + region.height - 1; ++depth) {
    for (int x = std::max(0, depth - region.height + 1); x <= std::min(depth, region.width - 1);
         ++x) {
      const TilePoint tile{x, depth - x};
      const auto shade = ground(run, run.terrain(tile));
      const auto point = center(tile, true);
      if (!voxel) {
        DrawRectangleRec({point.x - step * 0.5f, point.y - step * 0.5f, step - 1, step - 1}, shade);
        continue;
      }
      const auto drop = rise(tile);
      const Vector2 left{point.x - step, point.y};
      const Vector2 right{point.x + step, point.y};
      const Vector2 north{point.x, point.y - step * 0.55f};
      const Vector2 south{point.x, point.y + step * 0.55f};
      quad(left, {left.x, left.y + drop}, {south.x, south.y + drop}, south,
           ColorBrightness(shade, -0.31f));
      quad(south, {south.x, south.y + drop}, {right.x, right.y + drop}, right,
           ColorBrightness(shade, -0.18f));
      quad(north, left, south, right, shade);
    }
  }
  for (const auto& site : region.sites) {
    const auto point = center(run.location(site), true);
    if (&site == selected) DrawCircleV(point, step * 0.43f, ink);
    if (const auto icon = icons.find(site.icon); icon != icons.end()) {
      const auto texture = icon->second;
      const auto size = step * 0.72f;
      DrawTexturePro(texture,
                     {0, 0, static_cast<float>(texture.width), static_cast<float>(texture.height)},
                     {point.x, point.y, size, size}, {size / 2, size / 2}, 0, mark(site, run));
    } else {
      DrawPoly(point, site.kind == "portal" ? 4 : 6, step * 0.27f, 0, mark(site, run));
    }
  }
  DrawCircleV(center(run.position(), true), step * 0.17f, WHITE);
}

void ui_text(int x, int y, std::string text, int size = 17, Color color = ink) {
  DrawText(text.c_str(), x, y, size, color);
}

std::optional<Action> input(const Expedition& run, const Site* selected, std::string recipe) {
  if (IsKeyPressed(KEY_W) || IsKeyPressed(KEY_UP)) return Action{Command::north};
  if (IsKeyPressed(KEY_S) || IsKeyPressed(KEY_DOWN)) return Action{Command::south};
  if (IsKeyPressed(KEY_A) || IsKeyPressed(KEY_LEFT)) return Action{Command::west};
  if (IsKeyPressed(KEY_D) || IsKeyPressed(KEY_RIGHT)) return Action{Command::east};
  if (IsKeyPressed(KEY_PERIOD)) return Action{Command::wait};
  if (IsKeyPressed(KEY_R)) return Action{Command::revive};
  if (IsKeyPressed(KEY_C) && !recipe.empty()) return Action{Command::craft, std::move(recipe)};
  if (IsKeyPressed(KEY_E)) {
    for (const auto& slot : run.workshop().state().bag)
      if (run.workshop().catalog().items.at(slot.id).tags.contains("food"))
        return Action{Command::eat, slot.id};
  }
  if (selected) {
    if (IsKeyPressed(KEY_F)) return Action{Command::use, selected->id};
    if (IsKeyPressed(KEY_SPACE)) return Action{Command::attack, selected->id};
    if (IsKeyPressed(KEY_G)) return Action{Command::gather, selected->id};
  }
  return {};
}

}  // namespace

bool play_expedition(Expedition& run, const expedition::Icons& icons,
                     const std::filesystem::path& save_file, bool smoke) {
  std::string status;
  std::error_code error;
  if (!save_file.empty() &&
      ((std::filesystem::exists(save_file, error) && !expedition::restore(run, save_file)) ||
       error))
    return false;
  if (smoke) {
    if (run.apply({Command::east}) != "ok") return false;
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
  }
  InitWindow(1050, 630, run.rules().voxel ? "Copper Relay" : "Drift Naturalists");
  if (!IsWindowReady()) return false;
  Textures textures;
  for (const auto& [id, icon] : icons) {
    const Image image{const_cast<unsigned char*>(icon.rgba.data()), icon.width, icon.height, 1,
                      PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    const auto texture = LoadTextureFromImage(image);
    if (IsTextureValid(texture)) textures.emplace(id, texture);
  }
  const bool ready = textures.size() == icons.size();
  SetTargetFPS(45);
  std::size_t selected = 0;
  std::size_t recipe_index = 0;
  do {
    std::vector<const Site*> targets;
    for (const auto& site : run.region().sites)
      if (run.nearby(site.id)) targets.push_back(&site);
    if (IsKeyPressed(KEY_TAB)) ++selected;
    if (IsKeyPressed(KEY_Q)) ++recipe_index;
    const auto* site = targets.empty() ? nullptr : targets[selected % targets.size()];
    const auto& recipes = run.workshop().catalog().recipes;
    const auto recipe =
        recipes.empty()
            ? std::string{}
            : std::next(recipes.begin(), static_cast<std::ptrdiff_t>(recipe_index % recipes.size()))
                  ->first;
    if (auto action = input(run, site, recipe)) {
      auto candidate = run;
      status = candidate.apply(std::move(*action));
      if (status == "ok") {
        if (save_file.empty() || expedition::save(candidate, save_file)) {
          run = std::move(candidate);
          if (!run.state().receipts.empty()) status = run.state().receipts.back().cause;
        } else {
          status = "save_failed";
        }
      }
      continue;
    }
    BeginDrawing();
    ClearBackground(background);
    ui_text(30, 28, run.rules().voxel ? "COPPER RELAY" : "DRIFT NATURALISTS", 30);
    ui_text(31, 67, run.region().id + "   /   " + run.state().chapter, 20, accent);
    draw_map(run, site, textures);
    const auto& state = run.state();
    const auto line = [&](int y, const char* label, int amount) {
      ui_text(735, y, std::string(label) + " " + std::to_string(amount));
    };
    line(108, "Health", state.health);
    line(134, "Food", state.hunger);
    line(160, "Morale", state.morale);
    line(186, "Boat", state.boat);
    ui_text(
        735, 220,
        "Season " + std::to_string(run.season() + 1) + "   Tide " + std::to_string(run.tide() + 1),
        16, subdued);
    ui_text(735, 245,
            "Wetness " + std::to_string(state.wetness) + "   Heat " + std::to_string(state.heat),
            16, subdued);
    ui_text(735, 280, site ? site->id + " / " + site->kind : "No site in reach", 17, accent);
    ui_text(735, 309, recipe.empty() ? "No recipe" : "Recipe: " + recipe, 17, accent);
    ui_text(735, 345, "Inventory", 17, subdued);
    int row = 369;
    for (const auto& slot : run.workshop().state().bag) {
      ui_text(735, row, slot.id + "  x" + std::to_string(slot.count), 15);
      row += 18;
      if (row >= 535) break;
    }
    if (state.dead)
      ui_text(31, 534, "Expedition ended. R attempts recovery.", 19, accent);
    else if (state.ending)
      ui_text(31, 534, "Expedition complete", 19, accent);
    else if (!status.empty())
      ui_text(31, 534, status, 19, accent);
    ui_text(31, 570, "WASD move    Tab select site    F use    G gather    Space fight    . wait",
            17, subdued);
    ui_text(31, 597, "Q select recipe    C craft    E eat    R revive    Esc quit", 17, subdued);
    EndDrawing();
  } while (!smoke && !WindowShouldClose());
  for (const auto& [id, texture] : textures) {
    (void)id;
    UnloadTexture(texture);
  }
  CloseWindow();
  return ready;
}

}  // namespace wds::player
