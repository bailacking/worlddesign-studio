#include <charconv>
#include <cstdint>
#include <iostream>
#include <string_view>

#include "workspace.hpp"

namespace {

int print_result(const wds::RenameResult& result) {
  std::cout << wds::status_name(result.status) << " revision=" << result.revision
            << " changed=" << (result.changed ? "true" : "false") << '\n';
  if (!result.id.empty()) {
    std::cout << result.id << ' ' << result.old_path << " -> " << result.new_path << '\n';
  }
  if (!result.diagnostic.empty()) std::cerr << result.diagnostic << '\n';
  return result.status == wds::RenameStatus::ok ? 0 : 1;
}

int run(int argc, char** argv) {
  const bool opening = argc == 3 && std::string_view(argv[2]) == "open";
  const bool renaming = argc == 6 && std::string_view(argv[2]) == "asset.rename";
  if (!opening && !renaming) {
    std::cerr << "Usage: wds_cli PROJECT open | wds_cli PROJECT asset.rename ID NAME REVISION\n";
    return 2;
  }
  std::uint64_t revision = 0;
  if (renaming) {
    const std::string_view value(argv[5]);
    const auto end = value.data() + value.size();
    const auto parsed = std::from_chars(value.data(), end, revision);
    if (parsed.ec != std::errc{} || parsed.ptr != end) {
      return print_result({wds::RenameStatus::invalid_argument, 0});
    }
  }

  wds::Workspace workspace(argv[1]);
  if (opening) return print_result(workspace.open());
  return print_result(workspace.rename({argv[3], argv[4], revision}));
}

}  // namespace

int main(int argc, char** argv) { return run(argc, argv); }
