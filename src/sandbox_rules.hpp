#pragma once

#include <filesystem>
#include <optional>

namespace wds::voxel {

inline constexpr int default_jump = 6;

bool valid_jump(int value);
// A missing file means defaults; invalid or inaccessible data has no value.
std::optional<int> read_jump(const std::filesystem::path& file);
bool write_jump(const std::filesystem::path& file, int value);

inline constexpr auto project_rules = "sandbox-rules-v1.txt";
inline constexpr auto project_save = "sandbox-v1.dat";
bool create_sandbox_project(const std::filesystem::path& directory);
std::optional<int> read_project_jump(const std::filesystem::path& directory);

}  // namespace wds::voxel
