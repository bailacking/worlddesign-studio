#pragma once

#include <filesystem>
#include <optional>
#include <string_view>

namespace wds::voxel {

struct Content;

inline constexpr int default_jump = 6;

bool valid_jump(int value);
// A missing file means defaults; invalid or inaccessible data has no value.
std::optional<int> read_jump(const std::filesystem::path& file);
bool write_jump(const std::filesystem::path& file, int value);

inline constexpr auto project_rules = "sandbox-rules-v1.txt";
inline constexpr auto project_save = "sandbox-v1.dat";
bool create_sandbox_project(const std::filesystem::path& directory, std::string_view preset);
std::optional<int> read_project_jump(const std::filesystem::path& directory);
bool write_project_content(const std::filesystem::path& directory, const Content& content);

}  // namespace wds::voxel
