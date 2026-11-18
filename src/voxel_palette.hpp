#pragma once

#include "voxel_content.hpp"

namespace wds::voxel {

bool palette_supported(std::string_view provider);
std::optional<Content> use_palette(std::string_view provider, Content original);

}  // namespace wds::voxel
