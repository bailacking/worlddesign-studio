#pragma once

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>

namespace wds {

enum class RenameStatus {
  ok,
  permission_denied,
  invalid_argument,
  recovery_required,
  revision_conflict,
  asset_not_found,
  path_conflict,
  save_failed,
};

std::string_view status_name(RenameStatus status);

struct RenameRequest {
  std::string id;
  std::string filename;
  std::uint64_t expected_revision;
};

struct RenameResult {
  RenameStatus status;
  std::uint64_t revision;
  bool changed = false;
  std::string id{};
  std::string old_path{};
  std::string new_path{};
  std::string diagnostic{};
};

class ProjectStore {
 public:
  // M0 assumes exclusive ownership of writes to the project directory.
  explicit ProjectStore(std::filesystem::path root);
  RenameResult open();
  RenameResult rename(const RenameRequest& request, bool writable);

 private:
  std::filesystem::path root_;
  std::mutex mutex_;
};

}  // namespace wds
