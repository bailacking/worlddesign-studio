#pragma once

#include <cstdint>
#include <filesystem>
#include <json.hpp>
#include <optional>
#include <string_view>

namespace wds::platform {

struct OperationAccess {
  bool project_write = false;
  bool destination_write = false;
};

inline constexpr std::size_t max_project_request = 16 * 1024 * 1024;

nlohmann::json run_operation(const std::filesystem::path& directory, std::string_view operation,
                             std::string_view mode, std::optional<std::uint64_t> revision,
                             const nlohmann::json& input, OperationAccess access);
nlohmann::json request_operation(const std::filesystem::path& directory, std::string_view request,
                                 OperationAccess access);

namespace detail {
const nlohmann::json& operation_specs();
}

}  // namespace wds::platform
