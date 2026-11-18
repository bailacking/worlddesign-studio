#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace wds::platform {

inline constexpr std::string_view windows_export_target = "windows-x64";

struct ExportResult {
  std::string code;
  std::string location{};
  std::optional<std::uint64_t> revision{};
  std::string destination{};
  std::size_t resources = 0;
};

ExportResult export_project(const std::filesystem::path& project, std::uint64_t revision,
                            std::string_view target, const std::filesystem::path& destination);
std::string export_result_json(const ExportResult& result);

#ifdef _WIN32
std::filesystem::path executable_directory();
ExportResult verify_release(const std::filesystem::path& package);
#endif

}  // namespace wds::platform
