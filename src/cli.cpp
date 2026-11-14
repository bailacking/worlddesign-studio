#include <charconv>
#include <cstdint>
#include <iostream>
#include <string_view>

#include "sandbox_rules.hpp"
#include "workspace.hpp"

namespace {

int rules(int argc, char** argv) {
  using namespace wds::voxel;
  const bool show = argc == 4 && std::string_view(argv[3]) == "show";
  const bool preview = argc == 6 && std::string_view(argv[3]) == "preview";
  const bool apply = argc == 6 && std::string_view(argv[3]) == "apply";
  if (!show && !preview && !apply) {
    std::cerr << "Usage: wds_cli --sandbox-rules FILE show | --sandbox-rules FILE "
                 "preview|apply EXPECTED NEW\n";
    return 2;
  }
  const auto current = read_jump(argv[2]);
  if (!current) {
    std::cerr << "invalid_rules\n";
    return 1;
  }
  if (show) {
    std::cout << "jump_speed=" << *current << '\n';
    return 0;
  }
  const auto parse = [](std::string_view text) {
    int value = 0;
    const auto end = text.data() + text.size();
    const auto result = std::from_chars(text.data(), end, value);
    return result.ec == std::errc{} && result.ptr == end ? value : 0;
  };
  const int expected = parse(argv[4]);
  const int next = parse(argv[5]);
  if (!valid_jump(expected) || !valid_jump(next)) {
    std::cerr << "invalid_argument: use integers 3 through 9\n";
    return 1;
  }
  if (expected != *current) {
    std::cerr << "rule_conflict: jump_speed=" << *current << '\n';
    return 1;
  }
  if (apply && next != *current && !write_jump(argv[2], next)) {
    std::cerr << "save_failed\n";
    return 1;
  }
  std::cout << (preview ? "preview" : "applied") << ": jump_speed " << *current << " -> " << next
            << '\n';
  return 0;
}

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
  if (argc > 1 && std::string_view(argv[1]) == "--sandbox-rules") return rules(argc, argv);
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
