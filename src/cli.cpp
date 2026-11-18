#include <charconv>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string_view>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "platform_project.hpp"
#include "project_io.hpp"
#include "project_operations.hpp"
#include "sandbox_rules.hpp"
#include "voxel_content.hpp"
#include "workspace.hpp"

namespace {

std::filesystem::path path_from_utf8(std::string_view text) {
  return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

bool parse_integer(std::string_view input, auto& value) {
  const auto end = input.data() + input.size();
  const auto parsed = std::from_chars(input.data(), end, value);
  return parsed.ec == std::errc{} && parsed.ptr == end;
}

int rules(int argc, char** argv, bool project = false) {
  using namespace wds::voxel;
  const auto input = path_from_utf8(argv[2]);
  const auto file = project ? input / project_rules : input;
  if (project && (argc == 4 || argc == 5) && std::string_view(argv[3]) == "create") {
    const std::string_view preset = argc == 5 ? argv[4] : "meadow";
    if (!template_content(preset)) {
      std::cerr << "invalid_template: choose meadow or mesa\n";
      return 1;
    }
    if (!create_sandbox_project(input, preset)) {
      std::cerr << "create_failed: choose a new directory under an existing folder\n";
      return 1;
    }
    std::cout << "created: voxel-sandbox-v1 preset=" << preset << '\n';
    return 0;
  }
  const bool show = argc == 4 && std::string_view(argv[3]) == "show";
  const bool preview = argc == 6 && std::string_view(argv[3]) == "preview";
  const bool apply = argc == 6 && std::string_view(argv[3]) == "apply";
  if (!show && !preview && !apply) {
    std::cerr << "Usage: wds_cli --sandbox-project DIR create [meadow|mesa]\n"
                 "       wds_cli --sandbox-project DIR show|preview|apply\n"
                 "       preview and apply take EXPECTED NEW.\n";
    return 2;
  }
  const auto current = project ? read_project_jump(input) : read_jump(file);
  if (!current) {
    std::cerr << "invalid_rules\n";
    return 1;
  }
  if (show) {
    if (project) {
      const auto content = read_project_content(input);
      if (!content) {
        std::cerr << "invalid_content: " << project_content << '\n';
        return 1;
      }
      std::cout << "template=voxel-sandbox-v1\nplacement=" << block_name(content->placement)
                << "\nsky=" << std::hex << std::setfill('0') << std::setw(6) << content->sky
                << std::dec
                << "\nstone=" << (content->style(Block::stone).breakable ? "breakable" : "locked")
                << '\n';
    }
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
  if (apply && next != *current && !write_jump(file, next)) {
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

using nlohmann::json;
using namespace wds::platform;

std::optional<std::string> read_request() {
  std::string bytes;
  std::array<char, 8192> buffer;
  while (bytes.size() <= max_project_request && std::cin) {
    std::cin.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    bytes.append(buffer.data(), static_cast<std::size_t>(std::cin.gcount()));
  }
  if (std::cin.bad() || bytes.size() > max_project_request) return std::nullopt;
  return bytes;
}

int print_operation(const json& result) {
  std::cout << result.dump() << '\n';
  return result.value("code", "") == "ok" ? 0 : 1;
}

int bad_request(std::string_view location = "/input") {
  return print_operation({{"code", "invalid_argument"}, {"location", location}});
}

int project_command(int argc, char** argv) {
  const std::string_view action(argv[2]);
  const auto directory = path_from_utf8(argv[3]);
  if (action == "request") {
    OperationAccess access;
    for (int i = 4; i < argc; ++i) {
      const std::string_view flag(argv[i]);
      if (flag == "--allow-project-write" && !access.project_write)
        access.project_write = true;
      else if (flag == "--allow-destination-write" && !access.destination_write)
        access.destination_write = true;
      else
        return bad_request("/permissions");
    }
    const auto bytes = read_request();
    if (!bytes) return bad_request("/");
    return print_operation(request_operation(directory, *bytes, access));
  }
  const auto execute = [&](std::string_view id, std::string_view mode,
                           std::optional<std::uint64_t> revision = std::nullopt,
                           const json& input = json::object()) {
    return print_operation(run_operation(directory, id, mode, revision, input, {true, true}));
  };
  std::uint64_t revision = 0;
  if (action == "index" && (argc == 4 || argc == 5)) {
    if (argc == 4) return execute("project.index", "read");
    if (!parse_integer(argv[4], revision)) return bad_request("/revision");
    return execute("project.index", "read", revision);
  }
  if (action == "export") {
    if (argc != 7 || !parse_integer(argv[4], revision)) return bad_request();
    return execute("project.export", "apply", revision,
                   {{"target", argv[5]}, {"destination", argv[6]}});
  }
  if ((argc == 5 && action == "scene") || (argc == 6 && action == "play")) {
    if (!parse_integer(argv[4], revision)) return bad_request("/revision");
    if (action == "scene") return execute("scene.inspect", "read", revision);
    std::uint64_t ticks = 0;
    if (!parse_integer(argv[5], ticks)) return bad_request("/ticks");
    return execute("project.play", "read", revision, {{"ticks", ticks}});
  }
  if (action == "restore-scene") {
    if (argc != 5 || !parse_integer(argv[4], revision)) return bad_request();
    const auto bytes = read_request();
    if (!bytes) return bad_request();
    try {
      const auto change = wds::platform::detail::parse_json(*bytes);
      if (!change.is_object()) return bad_request();
      return execute("scene.restore", "apply", revision, {{"change", change}});
    } catch (const std::length_error&) {
      return bad_request();
    }
  }
  const bool capture_field =
      argc == 9 && action == "set-scene-field" && std::string_view(argv[8]) == "--capture-change";
  if (capture_field ||
      (argc == 8 && (action == "preview-scene-field" || action == "set-scene-field"))) {
    std::int64_t value = 0;
    if (!parse_integer(argv[6], revision) || !parse_integer(argv[7], value)) return bad_request();
    auto input = json{{"entity_id", argv[4]}, {"field", argv[5]}, {"value", value}};
    if (capture_field) input["capture_change"] = true;
    const bool apply = action == "set-scene-field";
    return execute(apply ? "scene.field.set" : "scene.field.preview", apply ? "apply" : "preview",
                   revision, input);
  }
  if (action == "preview-entity" || action == "edit-entity") {
    const bool apply = action == "edit-entity";
    const bool capture =
        apply && argc >= 8 && std::string_view(argv[argc - 1]) == "--capture-change";
    const int count = argc - (capture ? 1 : 0);
    if ((count != 7 && count != 8) || !parse_integer(argv[6], revision)) return bad_request();
    const std::string_view edit(argv[4]);
    auto input = json{{"operation", edit}, {"entity_id", argv[5]}};
    if (count == 8 && edit == "copy") {
      input["argument"] = argv[7];
    } else if (count == 8 && edit == "move") {
      std::size_t index = 0;
      if (!parse_integer(argv[7], index)) return bad_request();
      input["argument"] = index;
    } else if (count != 7 || (edit != "add" && edit != "remove")) {
      return bad_request();
    }
    if (capture) input["capture_change"] = true;
    return execute(apply ? "scene.entity.apply" : "scene.entity.preview",
                   apply ? "apply" : "preview", revision, input);
  }
  if (argc == 8 && (action == "preview-field" || action == "set-field")) {
    std::int64_t value = 0;
    if (!parse_integer(argv[6], revision) || !parse_integer(argv[7], value)) return bad_request();
    const bool apply = action == "set-field";
    return execute(apply ? "resource.field.set" : "resource.field.preview",
                   apply ? "apply" : "preview", revision,
                   {{"resource_id", argv[4]}, {"field", argv[5]}, {"value", value}});
  }
  const bool guarded_import =
      action == "import" && argc == 11 && std::string_view(argv[9]) == "--expect-digest";
  if (guarded_import || (argc == 9 && (action == "preview-import" || action == "import"))) {
    if (!parse_integer(argv[8], revision)) return bad_request("/revision");
    auto input =
        json{{"source", argv[4]}, {"resource_id", argv[5]}, {"kind", argv[6]}, {"path", argv[7]}};
    if (guarded_import) input["expected_digest"] = argv[10];
    const bool apply = action == "import";
    return execute(apply ? "resource.import.apply" : "resource.import.preview",
                   apply ? "apply" : "preview", revision, input);
  }
  if (action == "create" && (argc == 5 || (argc == 7 && std::string_view(argv[5]) == "--kind"))) {
    json input{{"project_id", argv[4]}};
    if (argc == 7) input["kind"] = argv[6];
    return execute("project.create", "apply", std::nullopt, input);
  }
  if (argc == 4) {
    if (action == "recover") return execute("project.recover", "apply");
    if (action == "open" || action == "validate" || action == "resources" || action == "imports")
      return execute("project." + std::string(action), "read");
  }
  std::cerr << "Use wds_cli --project-operations to list project commands.\n";
  return 2;
}

int run(int argc, char** argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--project-operations") {
    std::cout << wds::platform::project_operations_json() << '\n';
    return 0;
  }
  if (argc >= 4 && std::string_view(argv[1]) == "project" &&
      std::string_view(argv[2]) != "asset.rename")
    return project_command(argc, argv);
  if (argc >= 3 && std::string_view(argv[1]) == "--sandbox-rules") return rules(argc, argv);
  if (argc >= 3 && std::string_view(argv[1]) == "--sandbox-project") return rules(argc, argv, true);
  const bool opening = argc == 3 && std::string_view(argv[2]) == "open";
  const bool renaming = argc == 6 && std::string_view(argv[2]) == "asset.rename";
  if (!opening && !renaming) {
    std::cerr << "Usage: wds_cli --project-operations | wds_cli PROJECT open | "
                 "wds_cli PROJECT asset.rename ID NAME REVISION\n";
    return 2;
  }
  std::uint64_t revision = 0;
  if (renaming && !parse_integer(argv[5], revision)) {
    return print_result({wds::RenameStatus::invalid_argument, 0});
  }

  wds::Workspace workspace(path_from_utf8(argv[1]));
  if (opening) return print_result(workspace.open());
  return print_result(workspace.rename({argv[3], argv[4], revision}));
}

}  // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
  std::vector<std::string> arguments;
  for (int index = 0; index < argc; ++index) {
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[index], -1, nullptr, 0,
                                         nullptr, nullptr);
    if (size <= 0) return 2;
    std::string value(static_cast<std::size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[index], -1, value.data(), size,
                            nullptr, nullptr) == 0)
      return 2;
    value.pop_back();
    arguments.push_back(std::move(value));
  }
  std::vector<char*> pointers;
  for (auto& arg : arguments) pointers.push_back(arg.data());
  return run(argc, pointers.data());
}
#else
int main(int argc, char** argv) { return run(argc, argv); }
#endif
