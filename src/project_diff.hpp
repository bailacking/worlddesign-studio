#pragma once

#include <cstdint>
#include <filesystem>
#include <json.hpp>

namespace wds::platform {

nlohmann::json compare_projects(const std::filesystem::path& before, std::uint64_t before_revision,
                                const std::filesystem::path& after, std::uint64_t after_revision);

}  // namespace wds::platform
