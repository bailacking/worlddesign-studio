#pragma once

#include <functional>

#include "expedition.hpp"
#include "scene_play.hpp"

namespace wds::platform {
struct VoxelScene;
}

namespace wds::player {

struct ExpeditionSlots {
  int active = 1;
  std::function<std::string(int, expedition::Expedition&, std::filesystem::path&)> select;
};

enum class ExpeditionWindow { interactive, render_smoke, input_smoke };

bool play_expedition(expedition::Expedition& run, const expedition::Icons& icons,
                     std::filesystem::path save_file,
                     ExpeditionWindow window = ExpeditionWindow::interactive,
                     ExpeditionSlots slots = {});

bool play_scene(platform::SceneSession& session, const char* title);
bool play_voxel(platform::VoxelScene& scene, const char* title);

}  // namespace wds::player
