#pragma once

#include <cstdint>
#include <filesystem>
#include <json.hpp>
#include <optional>
#include <string>
#include <string_view>

namespace wds::platform::detail {

enum class PathState { ok, missing, unsafe };

std::string portable(const std::filesystem::path& path);
bool valid_path(std::string_view path);
PathState node(const std::filesystem::path& path, bool directory);
PathState project_path(const std::filesystem::path& root, const std::filesystem::path& relative);
std::optional<std::string> read_bytes(const std::filesystem::path& file, std::uintmax_t limit);
nlohmann::json parse_json(std::string_view bytes);

}  // namespace wds::platform::detail
