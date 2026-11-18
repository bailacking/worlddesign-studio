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
#include "project_export.hpp"
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

int run(int argc, char** argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--project-operations") {
    std::cout << wds::platform::project_operations_json() << '\n';
    return 0;
  }
  if (argc >= 4 && std::string_view(argv[1]) == "project" &&
      std::string_view(argv[2]) != "asset.rename") {
    const std::string_view action(argv[2]);
    const auto directory = path_from_utf8(argv[3]);
    if (action == "export") {
      std::uint64_t revision = 0;
      const auto result =
          argc == 7 && parse_integer(argv[4], revision)
              ? wds::platform::export_project(directory, revision, argv[5], path_from_utf8(argv[6]))
              : wds::platform::ExportResult{"invalid_argument", "/input"};
      std::cout << wds::platform::export_result_json(result) << '\n';
      return result.code == "ok" ? 0 : 1;
    }
    const bool inspect_scene = argc == 5 && action == "scene";
    if (inspect_scene || (argc == 6 && action == "play")) {
      std::uint64_t revision = 0;
      std::uint64_t ticks = 0;
      wds::platform::SceneResult result{std::nullopt, "invalid_argument", "/revision"};
      if (parse_integer(argv[4], revision)) {
        if (inspect_scene ||
            (parse_integer(argv[5], ticks) && ticks <= wds::platform::SceneSession::max_ticks)) {
          result = wds::platform::start_scene(directory, revision, inspect_scene);
          if (result.session) result.session->advance(ticks);
        } else {
          result.location = "/ticks";
        }
      }
      std::cout << wds::platform::scene_result_json(result, inspect_scene) << '\n';
      return result.session || result.voxel_scene ? 0 : 1;
    }
    if (action == "restore-scene") {
      wds::platform::FieldResult result{"invalid_argument", "/input"};
      std::uint64_t revision = 0;
      if (argc == 5 && parse_integer(argv[4], revision)) {
        std::string request;
        std::array<char, 8192> bytes;
        while (request.size() <= wds::platform::max_scene_restore_request && std::cin) {
          std::cin.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
          request.append(bytes.data(), static_cast<std::size_t>(std::cin.gcount()));
        }
        if (!std::cin.bad()) result = wds::platform::restore_scene(directory, revision, request);
      }
      std::cout << wds::platform::field_result_json(result) << '\n';
      return result.code == "ok" ? 0 : 1;
    }
    const bool capture_field =
        argc == 9 && action == "set-scene-field" && std::string_view(argv[8]) == "--capture-change";
    if (capture_field ||
        (argc == 8 && (action == "preview-scene-field" || action == "set-scene-field"))) {
      wds::platform::SceneEdit edit{argv[4], argv[5], 0, 0};
      const auto result =
          parse_integer(argv[6], edit.revision) && parse_integer(argv[7], edit.value)
              ? wds::platform::edit_scene(directory, edit, action == "set-scene-field",
                                          capture_field)
              : wds::platform::FieldResult{"invalid_argument", "/input"};
      std::cout << wds::platform::field_result_json(result) << '\n';
      return result.code == "ok" ? 0 : 1;
    }
    if (action == "preview-entity" || action == "edit-entity") {
      const bool capture = action == "edit-entity" && argc >= 8 &&
                           std::string_view(argv[argc - 1]) == "--capture-change";
      const int count = argc - (capture ? 1 : 0);
      wds::platform::FieldResult result{"invalid_argument", "/input"};
      if (count == 7 || count == 8) {
        wds::platform::EntityEdit edit{argv[4], argv[5]};
        const bool valid_argument =
            count == 7 ? (edit.operation == "add" || edit.operation == "remove")
                       : (edit.operation == "copy" ||
                          (edit.operation == "move" && parse_integer(argv[7], edit.index)));
        if (count == 8 && edit.operation == "copy") edit.target = argv[7];
        if (valid_argument && parse_integer(argv[6], edit.revision))
          result = wds::platform::edit_entity(directory, edit, action == "edit-entity", capture);
      }
      std::cout << wds::platform::field_result_json(result) << '\n';
      return result.code == "ok" ? 0 : 1;
    }
    if (argc == 8 && (action == "preview-field" || action == "set-field")) {
      wds::platform::FieldEdit edit{argv[4], argv[5], 0, 0};
      const auto result =
          parse_integer(argv[6], edit.revision) && parse_integer(argv[7], edit.value)
              ? wds::platform::edit_field(directory, edit, action == "set-field")
              : wds::platform::FieldResult{"invalid_argument", "/input"};
      std::cout << wds::platform::field_result_json(result) << '\n';
      return result.code == "ok" ? 0 : 1;
    }
    if (argc == 4 && action == "imports") {
      const auto result = wds::platform::list_imports(directory);
      std::cout << wds::platform::import_list_json(result) << '\n';
      return result.code == "ok" ? 0 : 1;
    }
    const bool guarded_import =
        action == "import" && argc == 11 && std::string_view(argv[9]) == "--expect-digest";
    if (guarded_import || (argc == 9 && (action == "preview-import" || action == "import"))) {
      wds::platform::ImportRequest request{argv[4], argv[5], argv[6], argv[7], 0};
      if (guarded_import) request.expected_digest = argv[10];
      const auto result =
          guarded_import && request.expected_digest.empty()
              ? wds::platform::ImportResult{"invalid_argument", "/expected_digest"}
          : parse_integer(argv[8], request.revision)
              ? wds::platform::import_resource(directory, request, action == "import")
              : wds::platform::ImportResult{"invalid_argument", "/revision"};
      std::cout << wds::platform::import_result_json(result) << '\n';
      return result.code == "ok" ? 0 : 1;
    }
    const bool create = action == "create" && argc == 5;
    const bool recover = action == "recover" && argc == 4;
    const bool read =
        argc == 4 && (action == "validate" || action == "open" || action == "resources");
    if (!create && !read && !recover) {
      std::cerr << "Usage: wds_cli project create DIRECTORY ID | project open|validate|resources "
                   "DIRECTORY\n"
                   "       wds_cli project play DIRECTORY REVISION TICKS\n"
                   "       wds_cli project export DIRECTORY REVISION windows-x64 DESTINATION\n"
                   "       wds_cli project scene DIRECTORY REVISION\n"
                   "       wds_cli project preview-scene-field|set-scene-field DIRECTORY ENTITY "
                   "FIELD REVISION VALUE\n"
                   "       wds_cli project preview-entity|edit-entity DIRECTORY OPERATION "
                   "ENTITY REVISION [ARG]\n"
                   "       wds_cli project preview-import DIRECTORY SOURCE ID KIND PATH "
                   "REVISION\n"
                   "       wds_cli project import DIRECTORY SOURCE ID KIND PATH REVISION "
                   "[--expect-digest DIGEST]\n"
                   "       wds_cli project imports DIRECTORY\n"
                   "       wds_cli project restore-scene DIRECTORY REVISION < CHANGE.json\n";
      return 2;
    }
    const auto result = create    ? wds::platform::create_project(directory, argv[4])
                        : recover ? wds::platform::recover_project(directory)
                                  : wds::platform::open_project(directory);
    std::cout << (action == "resources" ? wds::platform::project_resources_json(result)
                                        : wds::platform::project_result_json(result))
              << '\n';
    return result.project ? 0 : 1;
  }
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
