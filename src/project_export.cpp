#include "project_export.hpp"

#include "expedition.hpp"
#include "platform_project.hpp"
#include "project_io.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// CNG needs the Win32 type definitions first.
// clang-format off
#include <windows.h>
#include <bcrypt.h>
// clang-format on

#include <algorithm>
#include <array>
#include <fstream>
#include <map>
#include <random>
#include <set>
#include <stdexcept>
#include <vector>

#include "windows_runtime_spec.hpp"
#endif

namespace wds::platform {
namespace fs = std::filesystem;
using Json = nlohmann::json;

#ifdef _WIN32
namespace {

using namespace detail;
constexpr auto manifest = "wds.project.json";
constexpr auto release = "wds.release.json";
constexpr auto kit_manifest = "wds.runtime.json";
constexpr std::uintmax_t max_metadata_bytes = 16 * 1024 * 1024;
constexpr std::uintmax_t max_file_bytes = 64 * 1024 * 1024;
constexpr std::uintmax_t max_bundle_bytes = 256 * 1024 * 1024;
constexpr std::array kit_files{"Play.exe",
                               "licenses/wds.txt",
                               "licenses/nlohmann-json.txt",
                               "licenses/lodepng.txt",
                               "licenses/raylib.txt",
                               "licenses/glfw.txt",
                               "licenses/raylib-embedded.txt"};

struct ExportFailure {
  const char* code;
  std::string location;
};

[[noreturn]] void fail(const char* code, std::string location) {
  throw ExportFailure{code, std::move(location)};
}

struct Sha256Provider {
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  Sha256Provider() {
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0)
      fail("io_error", "SHA-256");
  }
  ~Sha256Provider() { BCryptCloseAlgorithmProvider(algorithm, 0); }
};

std::string digest(const std::string& bytes) {
  static const Sha256Provider provider;
  std::array<unsigned char, 32> hash{};
  if (BCryptHash(provider.algorithm, nullptr, 0,
                 reinterpret_cast<unsigned char*>(const_cast<char*>(bytes.data())),
                 static_cast<ULONG>(bytes.size()), hash.data(),
                 static_cast<ULONG>(hash.size())) != 0)
    fail("io_error", "SHA-256");
  constexpr std::string_view digits = "0123456789abcdef";
  std::string result;
  result.reserve(64);
  for (unsigned char value : hash) {
    result.push_back(digits[value >> 4]);
    result.push_back(digits[value & 15]);
  }
  return result;
}

std::string required(const fs::path& root, const fs::path& relative, const char* missing,
                     std::uintmax_t limit = max_file_bytes) {
  const auto state = project_path(root, relative);
  if (state == PathState::unsafe) fail("unsafe_path", portable(root / relative));
  if (state == PathState::missing) fail(missing, portable(root / relative));
  auto bytes = read_bytes(root / relative, limit);
  if (!bytes) fail("export_too_large", portable(root / relative));
  return std::move(*bytes);
}

Json file_entry(std::string_view path, const std::string& bytes) {
  return {{"path", path}, {"bytes", bytes.size()}, {"sha256", digest(bytes)}};
}

bool valid_runtime(const Json& metadata) {
  if (!metadata.is_object() || !metadata.contains("system_dlls") ||
      !metadata["system_dlls"].is_array() || metadata["system_dlls"].empty() ||
      metadata.value("msvc", "").empty())
    return false;
  static const auto spec = parse_json(windows_runtime_spec);
  for (const auto& [name, value] : spec.items()) {
    // Earlier players can export scenes that use only the capabilities they advertise.
    if ((name == "behavior_format" || name == "terrain_format" || name == "workshop_format" ||
         name == "voxel_format" || name == "world_ecology_format" || name == "expedition_format" ||
         name == "survival_format" || name == "exposure_format") &&
        !metadata.contains(name))
      continue;
    if (!metadata.contains(name) || metadata[name] != value) return false;
  }
  return true;
}

std::set<std::string> resource_closure(const Project& project) {
  std::map<std::string_view, const Resource*> by_id;
  for (const auto& asset : project.resources) by_id.emplace(asset.id, &asset);
  std::vector<std::string_view> pending{project.entry_scene};
  std::set<std::string> selected;
  while (!pending.empty()) {
    const auto id = pending.back();
    pending.pop_back();
    if (!selected.emplace(id).second) continue;
    const auto& asset = *by_id.at(id);
    pending.insert(pending.end(), asset.refs.begin(), asset.refs.end());
  }
  return selected;
}

void write_file(const fs::path& path, const std::string& bytes) {
  fs::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  output.close();
  if (!output) fail("io_error", portable(path));
}

class PackageStage {
 public:
  explicit PackageStage(const fs::path& parent) {
    std::random_device random;
    for (int attempt = 0; attempt < 8; ++attempt) {
      auto candidate =
          parent / (".wds-export-" + std::to_string(random()) + "-" + std::to_string(random()));
      if (fs::create_directory(candidate)) {
        root_ = std::move(candidate);
        return;
      }
    }
    fail("io_error", portable(parent));
  }
  PackageStage(const PackageStage&) = delete;
  PackageStage& operator=(const PackageStage&) = delete;
  ~PackageStage() {
    if (!root_.empty()) {
      std::error_code ignored;
      fs::remove_all(root_, ignored);
    }
  }

  const fs::path& root() const { return root_; }
  void publish(const fs::path& destination) {
    if (!MoveFileExW(root_.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH)) {
      if (node(destination, true) != PathState::missing)
        fail("target_exists", portable(destination));
      fail("io_error", portable(destination));
    }
    root_.clear();
  }

 private:
  fs::path root_;
};

fs::path export_destination(const fs::path& source, const fs::path& requested) {
  if (requested.empty() || requested.filename().empty() || requested.filename() == "." ||
      requested.filename() == "..")
    fail("invalid_export_destination", portable(requested));
  const auto absolute = fs::absolute(requested).lexically_normal();
  const auto parent = absolute.parent_path();
  if (node(parent, true) != PathState::ok) fail("invalid_export_destination", portable(parent));
  for (auto ancestor = parent;; ancestor = ancestor.parent_path()) {
    if (fs::equivalent(source, ancestor)) fail("invalid_export_destination", portable(absolute));
    if (ancestor == ancestor.root_path()) break;
  }
  if (node(absolute, true) != PathState::missing) fail("target_exists", portable(absolute));
  return absolute;
}

bool valid_release_path(std::string_view path) {
  if (path == "Play.exe" || path == "game/wds.project.json") return true;
  if (path.starts_with("game/")) return valid_path(path.substr(5));
  if (path.starts_with("licenses/")) return valid_path("assets/" + std::string(path.substr(9)));
  return false;
}

}  // namespace

fs::path executable_directory() {
  std::wstring buffer(32768, L'\0');
  const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  if (length == 0 || length >= buffer.size()) return {};
  buffer.resize(length);
  return fs::path(buffer).parent_path();
}
#endif

ExportResult export_project(const fs::path& project, std::uint64_t revision,
                            std::string_view target, const fs::path& destination) {
#ifdef _WIN32
  if (target != windows_export_target) return {"unsupported_export_target", std::string(target)};
  const auto opened = open_project(project);
  if (!opened.project) return {opened.code, opened.location};
  if (opened.project->revision != revision)
    return {"revision_conflict", "/revision", opened.project->revision};
  const auto& source = *opened.project;
  try {
    const auto output = export_destination(project, destination);
    const auto source_manifest =
        required(project, manifest, "missing_manifest", max_metadata_bytes);
    const auto expected = parse_json(source_manifest);
    if (!expected.is_object() || expected.value("revision", std::uint64_t{}) != revision ||
        expected.value("project_id", "") != source.id ||
        expected.value("entry_scene", "") != source.entry_scene)
      fail("source_changed", manifest);
    auto all_resources = Json::array();
    for (const auto& asset : source.resources)
      all_resources.push_back(
          {{"id", asset.id}, {"kind", asset.kind}, {"path", asset.path}, {"refs", asset.refs}});
    if (expected.at("resources") != all_resources) fail("source_changed", manifest);

    const auto binary_dir = executable_directory();
    if (binary_dir.empty()) fail("io_error", "wds_cli.exe");
    const auto kit = binary_dir / "wds-runtime" / windows_export_target;
    if (node(kit.parent_path(), true) != PathState::ok || node(kit, true) != PathState::ok)
      fail("missing_runtime_dependency", portable(kit));
    auto metadata =
        parse_json(required(kit, kit_manifest, "missing_runtime_dependency", max_metadata_bytes));
    if (!valid_runtime(metadata) || !metadata.contains("files") || !metadata["files"].is_array() ||
        metadata["files"].size() != kit_files.size())
      fail("invalid_runtime_dependency", portable(kit / kit_manifest));
    const auto lock_files = std::move(metadata["files"]);
    metadata.erase("files");

    PackageStage staging(output.parent_path());
    const auto& stage = staging.root();
    auto files = Json::array();
    std::uintmax_t size = 0;
    const auto save = [&](std::string path, const std::string& bytes) {
      if (bytes.size() > max_bundle_bytes - size) fail("export_too_large", path);
      size += bytes.size();
      write_file(stage / path, bytes);
      files.push_back(file_entry(path, bytes));
    };
    for (const auto* path : kit_files) {
      const auto bytes = required(kit, path, "missing_runtime_dependency");
      if (std::ranges::find(lock_files, file_entry(path, bytes)) == lock_files.end())
        fail("invalid_runtime_dependency", portable(kit / path));
      save(path, bytes);
    }

    const auto selected = resource_closure(source);
    auto projected = expected;
    projected["resources"] = Json::array();
    std::vector<std::pair<std::string, std::string>> snapshots;
    const auto copy = [&](std::string relative, std::string packaged) {
      const auto bytes = required(project, relative, "missing_file");
      save(std::move(packaged), bytes);
      snapshots.emplace_back(std::move(relative), files.back().at("sha256").get<std::string>());
    };
    const auto copy_notice = [&](const std::string& relative, std::string packaged) {
      if (project_path(project, relative) != PathState::missing)
        copy(relative, std::move(packaged));
    };
    copy_notice("LICENSE", "licenses/project.txt");
    for (const auto& asset : all_resources) {
      const auto& id = asset.at("id").get_ref<const std::string&>();
      if (!selected.contains(id)) continue;
      const auto& path = asset.at("path").get_ref<const std::string&>();
      projected["resources"].push_back(asset);
      copy(path, "game/" + path);
      copy_notice(path + ".license.txt", "licenses/assets/" + id + "/notice.txt");
    }
    save("game/wds.project.json", projected.dump(2) + "\n");
    auto scene = start_scene(stage / "game", revision, true);
    if (!scene.session && !scene.voxel_scene)
      return {scene.code,
              scene.resource_path.empty() ? scene.location : scene.resource_path + scene.location,
              revision};

    if (scene.voxel_scene && !metadata.contains("voxel_format"))
      fail("invalid_runtime_dependency", portable(kit / kit_manifest) + "/voxel_format");
    if (scene.session && scene.session->snapshot().behavior_state &&
        !metadata.contains("behavior_format"))
      fail("invalid_runtime_dependency", portable(kit / kit_manifest) + "/behavior_format");
    if (scene.session && scene.session->terrain() && !metadata.contains("terrain_format"))
      fail("invalid_runtime_dependency", portable(kit / kit_manifest) + "/terrain_format");
    const auto route = expedition::load(stage / "game", revision);
    if (route.code != "ok" && route.code != "no_expedition")
      fail(route.code.c_str(), route.location);
    if (route.expedition && !metadata.contains("expedition_format"))
      fail("invalid_runtime_dependency", portable(kit / kit_manifest) + "/expedition_format");
    const bool survival = (route.expedition && route.expedition->uses_survival_rules()) ||
                          (scene.session && scene.session->workshop() &&
                           scene.session->workshop()->uses_survival_rules()) ||
                          (scene.voxel_scene && scene.voxel_scene->workshop &&
                           scene.voxel_scene->workshop->uses_survival_rules());
    if (survival && !metadata.contains("survival_format"))
      fail("invalid_runtime_dependency", portable(kit / kit_manifest) + "/survival_format");
    if (route.expedition && route.expedition->uses_exposure_rules() &&
        !metadata.contains("exposure_format"))
      fail("invalid_runtime_dependency", portable(kit / kit_manifest) + "/exposure_format");
    const bool frontier = scene.voxel_scene && scene.voxel_scene->world.terrain() &&
                          scene.voxel_scene->world.terrain()->frontier();
    const bool ecology =
        scene.session && scene.session->terrain() && scene.session->terrain()->ecology();
    if ((frontier || ecology) && !metadata.contains("world_ecology_format"))
      fail("invalid_runtime_dependency", portable(kit / kit_manifest) + "/world_ecology_format");
    if (((scene.session && scene.session->workshop()) ||
         (scene.voxel_scene && scene.voxel_scene->workshop)) &&
        !metadata.contains("workshop_format"))
      fail("invalid_runtime_dependency", portable(kit / kit_manifest) + "/workshop_format");

    // Recheck the source snapshot before publishing. The staged files are the ones we validated.
    const auto current = open_project(project);
    if (!current.project || current.project->revision != revision ||
        required(project, manifest, "source_changed", max_metadata_bytes) != source_manifest)
      fail("source_changed", manifest);
    for (const auto& [path, sha256] : snapshots)
      if (digest(required(project, path, "source_changed")) != sha256) fail("source_changed", path);

    const Json lock{{"format_version", 1},
                    {"target", target},
                    {"project_id", source.id},
                    {"revision", revision},
                    {"entry_scene", source.entry_scene},
                    {"runtime", std::move(metadata)},
                    {"files", std::move(files)}};
    const auto encoded = lock.dump(2) + "\n";
    if (encoded.size() > max_metadata_bytes) fail("export_too_large", release);
    write_file(stage / release, encoded);
    staging.publish(output);
    return {"ok", {}, revision, portable(output), selected.size()};
  } catch (const ExportFailure& failure) {
    return {failure.code, failure.location, revision};
  } catch (const fs::filesystem_error& error) {
    return {"io_error", portable(error.path1()), revision};
  } catch (const Json::exception&) {
    return {"invalid_runtime_dependency", kit_manifest, revision};
  } catch (const std::length_error&) {
    return {"invalid_runtime_dependency", kit_manifest, revision};
  }
#else
  (void)project;
  (void)revision;
  (void)destination;
  return {"unsupported_export_target", std::string(target)};
#endif
}

#ifdef _WIN32
ExportResult verify_release(const fs::path& package) {
  try {
    const auto metadata =
        parse_json(required(package, release, "invalid_release", max_metadata_bytes));
    if (!metadata.is_object() || metadata.value("format_version", 0) != 1 ||
        metadata.value("target", "") != windows_export_target ||
        !valid_runtime(metadata.at("runtime")) || !metadata.at("files").is_array())
      fail("invalid_release", release);
    std::set<std::string> paths;
    std::uintmax_t total = 0;
    for (const auto& entry : metadata.at("files")) {
      const auto path = entry.at("path").get<std::string>();
      if (!valid_release_path(path) || !paths.insert(path).second) fail("invalid_release", path);
      const auto bytes = required(package, path, "release_content_mismatch");
      if (entry != file_entry(path, bytes)) fail("release_content_mismatch", path);
      if (bytes.size() > max_bundle_bytes - total) fail("export_too_large", path);
      total += bytes.size();
    }
    for (const auto* path : kit_files)
      if (!paths.contains(path)) fail("invalid_release", path);
    if (!paths.contains("game/wds.project.json")) fail("invalid_release", manifest);
    const auto opened = open_project(package / "game");
    if (!opened.project) return {opened.code, opened.location};
    const auto& project = *opened.project;
    if (metadata.at("revision") != project.revision || metadata.at("project_id") != project.id ||
        metadata.at("entry_scene") != project.entry_scene)
      fail("release_content_mismatch", manifest);
    for (const auto& asset : project.resources)
      if (!paths.contains("game/" + asset.path)) fail("invalid_release", asset.path);
    return {"ok", {}, project.revision, {}, project.resources.size()};
  } catch (const ExportFailure& failure) {
    return {failure.code, failure.location};
  } catch (const fs::filesystem_error& error) {
    return {"io_error", portable(error.path1())};
  } catch (const Json::exception&) {
    return {"invalid_release", release};
  } catch (const std::length_error&) {
    return {"invalid_release", release};
  }
}
#endif

std::string export_result_json(const ExportResult& result) {
  Json output{{"code", result.code}};
  if (!result.location.empty()) output["location"] = result.location;
  if (result.revision) output["revision"] = *result.revision;
  if (result.code == "ok") {
    output["destination"] = result.destination;
    output["resources"] = result.resources;
  }
  return output.dump();
}

}  // namespace wds::platform
