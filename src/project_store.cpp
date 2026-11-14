#include "project_store.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <charconv>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace wds {
namespace {

namespace fs = std::filesystem;
constexpr auto manifest_name = ".wds-project";
constexpr auto journal_name = ".wds-rename";

struct Asset {
  std::string path;
  std::vector<std::string> references;
};

struct Manifest {
  std::uint64_t revision;
  std::map<std::string, Asset> assets;
};

struct Journal {
  std::uint64_t revision;
  std::string id;
  std::string old_path;
  std::string new_path;
  std::uint64_t checksum;
};

enum class Entry { missing, file, directory, unsafe };

Entry entry(const fs::path& path) {
  std::error_code ec;
  const auto status = fs::symlink_status(path, ec);
  if (status.type() == fs::file_type::not_found) return Entry::missing;
  if (ec || fs::is_symlink(status)) return Entry::unsafe;
#ifdef _WIN32
  const auto attributes = GetFileAttributesW(path.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
    return Entry::unsafe;
  }
#endif
  if (fs::is_regular_file(status)) return Entry::file;
  return fs::is_directory(status) ? Entry::directory : Entry::unsafe;
}

std::string folded(std::string value) {
  for (auto& ch : value) {
    if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
  }
  return value;
}

bool alnum(char ch) {
  return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
}

bool valid_segment(const std::string& name) {
  if (name.empty() || name.size() > 255 || !alnum(name.front()) || name.back() == '.' ||
      !std::ranges::all_of(
          name, [](char ch) { return alnum(ch) || ch == '_' || ch == '-' || ch == '.'; }))
    return false;
  const auto stem = folded(name.substr(0, name.find('.')));
  if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul") return false;
  return !(stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) &&
           stem[3] >= '0' && stem[3] <= '9');
}

bool valid_path(const std::string& path) {
  if (path.empty() || path.front() == '/' || path.back() == '/') return false;
  for (const auto part : path | std::views::split('/')) {
    if (!valid_segment(std::string(part.begin(), part.end()))) return false;
  }
  return true;
}

bool safe_parent(const fs::path& root, const std::string& path) {
  auto current = root;
  for (const auto& part : fs::path(path).parent_path()) {
    current /= part;
    if (entry(current) != Entry::directory) return false;
  }
  return true;
}

bool available(const fs::path& root, const std::string& path) {
  if (entry(root / path) != Entry::missing) return false;
  const auto target = fs::path(path);
  const auto name = folded(target.filename().string());
  for (const auto& file : fs::directory_iterator(root / target.parent_path())) {
    const auto candidate = file.path().filename().u8string();
    if (folded(std::string(candidate.begin(), candidate.end())) == name) return false;
  }
  return true;
}

bool number(std::string_view token, std::uint64_t& value) {
  const auto end = token.data() + token.size();
  const auto parsed = std::from_chars(token.data(), end, value);
  return parsed.ec == std::errc{} && parsed.ptr == end;
}

std::optional<Manifest> read_manifest(const fs::path& path) {
  if (entry(path) != Entry::file) return std::nullopt;
  std::ifstream in(path, std::ios::binary);
  std::string line;
  if (!std::getline(in, line)) return std::nullopt;
  if (line.ends_with('\r')) line.pop_back();
  constexpr std::string_view prefix = "wds-project-v1 ";
  Manifest manifest{};
  if (!line.starts_with(prefix) ||
      !number(std::string_view(line).substr(prefix.size()), manifest.revision)) {
    return std::nullopt;
  }
  std::set<std::string> paths;
  while (std::getline(in, line)) {
    std::istringstream row(line);
    Asset asset;
    std::string id;
    if (!(row >> id >> asset.path) || !valid_segment(id) || !valid_path(asset.path) ||
        !paths.insert(folded(asset.path)).second)
      return std::nullopt;
    for (std::string reference; row >> reference;) {
      asset.references.push_back(std::move(reference));
    }
    if (!manifest.assets.emplace(id, std::move(asset)).second) return std::nullopt;
  }
  if (!in.eof() || in.bad()) return std::nullopt;
  for (const auto& [id, asset] : manifest.assets) {
    for (const auto& reference : asset.references) {
      if (!manifest.assets.contains(reference)) return std::nullopt;
    }
  }
  return manifest;
}

std::string encode(const Manifest& manifest) {
  std::ostringstream out;
  out << "wds-project-v1 " << manifest.revision << '\n';
  for (const auto& [id, asset] : manifest.assets) {
    out << id << ' ' << asset.path;
    for (const auto& reference : asset.references) out << ' ' << reference;
    out << '\n';
  }
  return out.str();
}

std::string encode(const Journal& journal) {
  std::ostringstream out;
  out << "wds-rename-v1 " << journal.revision << ' ' << journal.id << ' ' << journal.old_path << ' '
      << journal.new_path << ' ' << journal.checksum << '\n';
  return out.str();
}

std::optional<Journal> read_journal(const fs::path& path) {
  if (entry(path) != Entry::file) return std::nullopt;
  std::ifstream in(path, std::ios::binary);
  std::string magic, revision, checksum, extra;
  Journal journal{};
  if (!(in >> magic >> revision >> journal.id >> journal.old_path >> journal.new_path >>
        checksum) ||
      (in >> extra) || !in.eof() || in.bad() || magic != "wds-rename-v1" ||
      !number(revision, journal.revision) || !number(checksum, journal.checksum) ||
      journal.revision == std::numeric_limits<std::uint64_t>::max() || !valid_segment(journal.id) ||
      !valid_path(journal.old_path) || !valid_path(journal.new_path) ||
      folded(journal.old_path) == folded(journal.new_path) ||
      fs::path(journal.old_path).parent_path() != fs::path(journal.new_path).parent_path()) {
    return std::nullopt;
  }
  return journal;
}

std::optional<std::uint64_t> checksum(const fs::path& path) {
  if (entry(path) != Entry::file) return std::nullopt;
  std::ifstream in(path, std::ios::binary);
  std::uint64_t value = 14695981039346656037ULL;
  for (char ch; in.get(ch);) {
    value = (value ^ static_cast<unsigned char>(ch)) * 1099511628211ULL;
  }
  return in.eof() && !in.bad() ? std::optional(value) : std::nullopt;
}

bool move_file(const fs::path& from, const fs::path& to, bool replace) {
#ifdef _WIN32
  return MoveFileExW(from.c_str(), to.c_str(), replace ? MOVEFILE_REPLACE_EXISTING : 0) != 0;
#else
  if (!replace && entry(to) != Entry::missing) return false;
  std::error_code error;
  fs::rename(from, to, error);
  return !error;
#endif
}

bool publish(const fs::path& path, const std::string& data, bool replace) {
  auto temp = path;
  temp += ".tmp";
  const auto kind = entry(temp);
  if (kind != Entry::missing && kind != Entry::file) return false;
  // Stale temporary files are only reserved staging files; never truncate a link.
  if (kind == Entry::file) {
    std::error_code error;
    if (!fs::remove(temp, error) || error) return false;
  }
  std::ofstream out(temp, std::ios::binary);
  out << data;
  out.close();
  return out.good() && move_file(temp, path, replace);
}

enum class Recovery { clean, cleanup_pending, blocked };

Recovery recover(const fs::path& root, const Manifest& manifest) {
  const auto pending = root / journal_name;
  if (entry(pending) == Entry::missing) return Recovery::clean;
  const auto journal = read_journal(pending);
  if (!journal || !safe_parent(root, journal->old_path)) return Recovery::blocked;
  const auto asset = manifest.assets.find(journal->id);
  if (asset == manifest.assets.end()) return Recovery::blocked;
  if (std::ranges::any_of(manifest.assets, [&](const auto& pair) {
        return pair.first != journal->id &&
               (folded(pair.second.path) == folded(journal->old_path) ||
                folded(pair.second.path) == folded(journal->new_path));
      })) {
    return Recovery::blocked;
  }

  const auto old = root / journal->old_path;
  const auto next = root / journal->new_path;
  const auto expected = std::optional(journal->checksum);
  if (manifest.revision == journal->revision && asset->second.path == journal->old_path) {
    if (checksum(old) == expected && available(root, journal->new_path)) {
      // The source move never took place or was already rolled back.
    } else if (available(root, journal->old_path) && checksum(next) == expected &&
               move_file(next, old, false)) {
      // The old manifest was still authoritative; restore the source path.
    } else {
      return Recovery::blocked;
    }
  } else if (manifest.revision != journal->revision + 1 ||
             asset->second.path != journal->new_path || !available(root, journal->old_path) ||
             checksum(next) != expected) {
    return Recovery::blocked;
  }

  std::error_code error;
  return fs::remove(pending, error) && !error ? Recovery::clean : Recovery::cleanup_pending;
}

bool validate_files(const fs::path& root, const Manifest& manifest) {
  return std::ranges::all_of(manifest.assets, [&](const auto& pair) {
    return safe_parent(root, pair.second.path) && entry(root / pair.second.path) == Entry::file;
  });
}

}  // namespace

std::string_view status_name(RenameStatus status) {
  using enum RenameStatus;
  switch (status) {
    case ok:
      return "ok";
    case permission_denied:
      return "permission_denied";
    case invalid_argument:
      return "invalid_argument";
    case recovery_required:
      return "recovery_required";
    case revision_conflict:
      return "revision_conflict";
    case asset_not_found:
      return "asset_not_found";
    case path_conflict:
      return "path_conflict";
    case save_failed:
      return "save_failed";
  }
  return "unknown";
}

ProjectStore::ProjectStore(fs::path root) {
  if (entry(root) != Entry::directory) throw std::invalid_argument("unsafe project directory");
  root_ = fs::canonical(root);
}

RenameResult ProjectStore::open() {
  const std::lock_guard lock(mutex_);
  try {
    const auto manifest = read_manifest(root_ / manifest_name);
    if (!manifest)
      return {RenameStatus::recovery_required, 0, false, {}, {}, {}, "invalid project manifest"};
    const auto state = recover(root_, *manifest);
    if (state == Recovery::blocked || !validate_files(root_, *manifest)) {
      return {RenameStatus::recovery_required, manifest->revision, false, {}, {}, {},
              "project requires inspection"};
    }
    return {RenameStatus::ok,
            manifest->revision,
            false,
            {},
            {},
            {},
            state == Recovery::cleanup_pending ? "journal cleanup pending" : ""};
  } catch (const fs::filesystem_error& error) {
    return {RenameStatus::recovery_required, 0, false, {}, {}, {}, error.code().message()};
  }
}

RenameResult ProjectStore::rename(const RenameRequest& request, bool writable) {
  using enum RenameStatus;
  if (!writable) return {permission_denied, 0};
  if (!valid_segment(request.id) || !valid_segment(request.filename)) return {invalid_argument, 0};
  const std::lock_guard lock(mutex_);
  std::uint64_t revision = 0;
  try {
    auto manifest = read_manifest(root_ / manifest_name);
    if (!manifest) return {recovery_required, 0, false, {}, {}, {}, "invalid project manifest"};
    revision = manifest->revision;
    if (entry(root_ / journal_name) != Entry::missing || !validate_files(root_, *manifest)) {
      return {recovery_required, revision, false, {}, {}, {}, "project requires inspection"};
    }
    if (revision != request.expected_revision) return {revision_conflict, revision};
    const auto asset = manifest->assets.find(request.id);
    if (asset == manifest->assets.end()) return {asset_not_found, revision};
    const auto old_path = asset->second.path;
    const auto new_path = (fs::path(old_path).parent_path() / request.filename).generic_string();
    if (old_path == new_path) return {ok, revision, false, request.id, old_path, new_path};
    if (std::ranges::any_of(
            manifest->assets,
            [&](const auto& pair) { return folded(pair.second.path) == folded(new_path); }) ||
        !available(root_, new_path))
      return {path_conflict, revision};
    const auto value = checksum(root_ / old_path);
    if (!value || revision == std::numeric_limits<std::uint64_t>::max()) {
      return {save_failed, revision, false, {}, {}, {}, "cannot prepare source or revision"};
    }

    const Journal journal{revision, request.id, old_path, new_path, *value};
    const auto pending = root_ / journal_name;
    if (!publish(pending, encode(journal), false)) {
      return {entry(pending) == Entry::missing ? save_failed : recovery_required,
              revision,
              false,
              {},
              {},
              {},
              "could not publish rename journal"};
    }
    const auto checked = read_journal(pending);
    if (!checked || encode(*checked) != encode(journal)) {
      return {recovery_required, revision, false, {}, {}, {}, "rename journal verification failed"};
    }

    const bool moved = move_file(root_ / old_path, root_ / new_path, false);
    if (moved) {
      asset->second.path = new_path;
      ++manifest->revision;
      publish(root_ / manifest_name, encode(*manifest), true);
    }
    const auto actual = read_manifest(root_ / manifest_name);
    if (!actual) return {recovery_required, revision, false, {}, {}, {}, "commit state unknown"};
    const auto state = recover(root_, *actual);
    if (actual->revision == revision + 1 && state != Recovery::blocked) {
      return {ok,
              actual->revision,
              true,
              request.id,
              old_path,
              new_path,
              state == Recovery::cleanup_pending ? "journal cleanup pending" : ""};
    }
    return {
        actual->revision == revision && state == Recovery::clean ? save_failed : recovery_required,
        actual->revision,
        false,
        {},
        {},
        {},
        "rename could not be committed"};
  } catch (const fs::filesystem_error& error) {
    return {recovery_required, revision, false, {}, {}, {}, error.code().message()};
  }
}

}  // namespace wds
