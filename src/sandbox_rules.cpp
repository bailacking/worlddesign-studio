#include "sandbox_rules.hpp"

#include "voxel_content.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <fstream>
#include <string>

namespace wds::voxel {
namespace {

namespace fs = std::filesystem;
constexpr auto header = "wds-sandbox-rules-v1";

bool write_settings(const fs::path& file, const auto& encode) {
  const auto directory = file.has_parent_path() ? file.parent_path() : fs::path(".");
  std::error_code error;
  fs::create_directories(directory, error);
  if (error) return false;
  const auto current = fs::symlink_status(file, error);
  if (current.type() != fs::file_type::not_found && (error || !fs::is_regular_file(current)))
    return false;
  auto staging = file;
  staging += ".tmp";
  if (fs::symlink_status(staging, error).type() != fs::file_type::not_found) return false;
  std::ofstream output(staging, std::ios::binary);
  encode(output);
  output.close();
  bool written = output.good();
  if (written) {
#ifdef _WIN32
    written = MoveFileExW(staging.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
#else
    fs::rename(staging, file, error);
    written = !error;
#endif
  }
  if (!written) fs::remove(staging, error);
  return written;
}

}  // namespace

bool valid_jump(int value) { return value >= 3 && value <= 9; }

std::optional<int> read_jump(const fs::path& file) {
  std::error_code error;
  const auto status = fs::symlink_status(file, error);
  if (status.type() == fs::file_type::not_found) return default_jump;
  if (error || !fs::is_regular_file(status)) return std::nullopt;
  std::ifstream input(file, std::ios::binary);
  std::string signature, extra;
  int jump = 0;
  if (!(input >> signature >> jump) || signature != header || !valid_jump(jump) ||
      (input >> extra) || input.bad() || !input.eof()) {
    return std::nullopt;
  }
  return jump;
}

bool write_jump(const fs::path& file, int value) {
  return valid_jump(value) && write_settings(file, [value](std::ostream& output) {
           output << header << '\n' << value << '\n';
         });
}

bool write_project_content(const fs::path& directory, const Content& content) {
  if (!read_project_jump(directory)) return false;
  return write_settings(directory / project_content,
                        [&content](std::ostream& output) { write_content(output, content); });
}

bool create_sandbox_project(const fs::path& directory, std::string_view preset) {
  const auto content = template_content(preset);
  if (!content) return false;
  std::error_code error;
  if (!fs::create_directory(directory, error)) return false;
  const auto file = directory / project_content;
  std::ofstream output(file, std::ios::binary);
  write_content(output, *content);
  output.close();
  if (output.good() && write_jump(directory / project_rules, preset == "mesa" ? 8 : default_jump)) {
    return true;
  }
  fs::remove(file, error);
  fs::remove(directory, error);
  return false;
}

std::optional<int> read_project_jump(const fs::path& directory) {
  std::error_code error;
  if (!fs::is_directory(fs::symlink_status(directory, error)) || error) return std::nullopt;
  const auto status = fs::symlink_status(directory / project_rules, error);
  if (error || !fs::is_regular_file(status)) return std::nullopt;
  return read_jump(directory / project_rules);
}

}  // namespace wds::voxel
