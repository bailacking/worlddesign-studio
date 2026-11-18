#include <raylib.h>

#include <filesystem>
#include <string>

#include "scene.hpp"

namespace {

class Graphics {
 public:
  explicit Graphics(const std::string& directory) {
    for (std::size_t variant = 0; variant < textures_.size(); ++variant) {
      const auto folder = directory + (variant == 0 ? "/a/" : "/b/");
      for (std::size_t item = 0; item < w4::asset_names.size(); ++item)
        textures_[variant][item] = LoadTexture((folder + w4::asset_names[item]).c_str());
      models_[variant] = LoadModel((folder + "beacon.obj").c_str());
    }
  }

  ~Graphics() {
    for (auto& variant : textures_)
      for (auto texture : variant) UnloadTexture(texture);
    for (auto model : models_) UnloadModel(model);
  }

  bool ready() const {
    for (const auto& variant : textures_)
      for (auto texture : variant)
        if (!IsTextureValid(texture)) return false;
    for (auto model : models_)
      if (!IsModelValid(model)) return false;
    return true;
  }

  void fill(int x, int y, int width, int height, w4::Color color) {
    DrawRectangle(x, y, width, height, {color.r, color.g, color.b, 255});
  }

  void texture(int variant, w4::Asset asset, int x, int y) {
    DrawTexture(textures_[variant][static_cast<std::size_t>(asset)], x, y, WHITE);
  }

  void draw_model(int variant) {
    const Camera3D camera{{2.6f, 2.1f, 2.9f}, {0, 0.65f, 0}, {0, 1, 0}, 35, CAMERA_PERSPECTIVE};
    ClearBackground({19, 30, 40, 255});
    BeginMode3D(camera);
    DrawGrid(10, 0.25f);
    DrawModel(models_[variant], {0, 0, 0}, 1, variant == 0 ? SKYBLUE : GOLD);
    DrawModelWires(models_[variant], {0, 0, 0}, 1, DARKGRAY);
    EndMode3D();
  }

 private:
  std::array<std::array<Texture2D, 3>, 2> textures_{};
  std::array<Model, 2> models_{};
};

class Audio {
 public:
  explicit Audio(const std::string& directory) {
    InitAudioDevice();
    if (!IsAudioDeviceReady()) return;
    for (std::size_t index = 0; index < tracks_.size(); ++index) {
      const auto folder = directory + (index == 0 ? "/a/" : "/b/");
      effects_[index] = LoadSound((folder + "pickup.wav").c_str());
      tracks_[index] = LoadMusicStream((folder + "music.wav").c_str());
      tracks_[index].looping = true;
      if (IsMusicValid(tracks_[index])) SetMusicVolume(tracks_[index], 0.45f);
    }
  }

  ~Audio() {
    if (!IsAudioDeviceReady()) return;
    for (auto effect : effects_)
      if (IsSoundValid(effect)) UnloadSound(effect);
    for (auto track : tracks_)
      if (IsMusicValid(track)) UnloadMusicStream(track);
    CloseAudioDevice();
  }

  void pickup(int variant) {
    if (IsSoundValid(effects_[variant])) PlaySound(effects_[variant]);
  }

  void stop(int variant) {
    if (IsMusicValid(tracks_[variant])) StopMusicStream(tracks_[variant]);
  }

  void update(int variant, bool play) {
    const auto music = tracks_[variant];
    if (!IsMusicValid(music) || !play) return;
    if (!IsMusicStreamPlaying(music)) PlayMusicStream(music);
    UpdateMusicStream(music);
  }

 private:
  std::array<Sound, 2> effects_{};
  std::array<Music, 2> tracks_{};
};

bool validate_audio_files(const std::string& directory) {
  for (const auto* arm : {"a", "b"}) {
    for (const auto* file : {"pickup.wav", "music.wav"}) {
      const auto source = directory + "/" + arm + "/" + file;
      const Wave wave = LoadWave(source.c_str());
      if (!IsWaveValid(wave)) return false;
      UnloadWave(wave);
    }
  }
  return true;
}

int measure(Graphics& graphics, const char* output) {
  RenderTexture2D target = LoadRenderTexture(w4::width, w4::height);
  if (!IsRenderTextureValid(target)) return 1;
  const int result = w4::benchmark(
      "raylib-opengl",
      [&](const w4::Scene& scene) {
        BeginTextureMode(target);
        w4::draw(scene, graphics);
        EndTextureMode();
      },
      [&] {
        Image image = LoadImageFromTexture(target.texture);
        const auto pixel = GetImageColor(image, w4::width - 34, w4::height / 2 - 1);
        UnloadImage(image);
        return pixel.g;
      });
  Image final = LoadImageFromTexture(target.texture);
  ImageFlipVertical(&final);
  bool exported = ExportImage(final, output);
  UnloadImage(final);
  const auto evidence = std::filesystem::path(output).parent_path();
  for (int variant = 0; variant < 2; ++variant) {
    BeginTextureMode(target);
    graphics.draw_model(variant);
    EndTextureMode();
    Image preview = LoadImageFromTexture(target.texture);
    ImageFlipVertical(&preview);
    const auto path = evidence / (variant == 0 ? "model-a.png" : "model-b.png");
    exported = ExportImage(preview, path.string().c_str()) && exported;
    UnloadImage(preview);
  }
  UnloadRenderTexture(target);
  return result == 0 && exported ? 0 : 1;
}

void play(Graphics& graphics, const std::string& directory) {
  Audio audio(directory);
  w4::Scene scene;
  bool music = false;
  bool model = false;
  SetTargetFPS(60);
  while (!WindowShouldClose()) {
    const int previous_variant = scene.variant;
    if (IsKeyPressed(KEY_R)) scene = w4::Scene{};
    if (IsKeyPressed(KEY_TAB)) scene.variant ^= 1;
    if (scene.variant != previous_variant) audio.stop(previous_variant);
    if (IsKeyPressed(KEY_M)) {
      music = !music;
      if (!music) audio.stop(scene.variant);
    }
    if (IsKeyPressed(KEY_V)) model = !model;
    const bool focused = IsWindowFocused();
    if (!model && focused &&
        scene.advance({.left = IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A),
                       .right = IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D),
                       .up = IsKeyDown(KEY_UP) || IsKeyDown(KEY_W),
                       .down = IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S)}))
      audio.pickup(scene.variant);
    if (IsKeyPressed(KEY_SPACE)) audio.pickup(scene.variant);
    audio.update(scene.variant, music);
    BeginDrawing();
    if (model)
      graphics.draw_model(scene.variant);
    else
      w4::draw(scene, graphics);
    DrawText(TextFormat("Reef Courier | %s   -   Lanterns %d / 3", scene.variant == 0 ? "A" : "B",
                        scene.score()),
             18, 17, 19, RAYWHITE);
    DrawText(scene.won && !model ? "Delivered!   R: restart"
                                 : "WASD: move   TAB: A/B   SPACE: sound   M: music   V: model",
             18, w4::height - 27, 16, RAYWHITE);
    EndDrawing();
  }
}
}  // namespace

int main(int argc, char** argv) {
  const bool bench = argc == 4 && std::string_view(argv[1]) == "--bench";
  if (!bench && argc > 2) {
    std::fputs("Usage: w4_raylib [ASSETS] | --bench ASSETS FRAME.png\n", stderr);
    return 2;
  }
  if (bench) SetConfigFlags(FLAG_WINDOW_HIDDEN | FLAG_WINDOW_UNFOCUSED);
  InitWindow(w4::width, w4::height, "Reef Courier - raylib");
  if (!IsWindowReady()) return 1;

  const std::string assets =
      argc == 1 ? (std::filesystem::path(GetApplicationDirectory()) / "assets").string()
                : argv[bench ? 2 : 1];
  int result = 0;
  {
    Graphics graphics(assets);
    if (!graphics.ready() || !validate_audio_files(assets)) {
      std::fputs("Could not load both variants of graphics and audio.\n", stderr);
      result = 1;
    } else if (bench) {
      result = measure(graphics, argv[3]);
    } else {
      play(graphics, assets);
    }
  }
  CloseWindow();
  return result;
}
