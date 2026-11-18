#pragma once

#include "scene_play.hpp"

namespace wds::platform {
struct VoxelScene;
}

namespace wds::player {

bool play_scene(platform::SceneSession& session, const char* title);
bool play_voxel(platform::VoxelScene& scene, const char* title);

}  // namespace wds::player
