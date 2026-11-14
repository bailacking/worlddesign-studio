#pragma once

#include <filesystem>
#include <optional>

namespace wds::voxel {

inline constexpr int default_jump = 6;

bool valid_jump(int value);
// A missing file means defaults; invalid or inaccessible data has no value.
std::optional<int> read_jump(const std::filesystem::path& file);
bool write_jump(const std::filesystem::path& file, int value);

}  // namespace wds::voxel
