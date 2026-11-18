#include "project_io.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <algorithm>
#include <fstream>
#include <ranges>
#include <set>

namespace wds::platform::detail {
namespace fs = std::filesystem;
using Json = nlohmann::json;

namespace {
bool lower(char ch) { return ch >= 'a' && ch <= 'z'; }
bool digit(char ch) { return ch >= '0' && ch <= '9'; }

bool valid_segment(std::string_view name) {
  if (name.empty() || name.size() > 64 || !(lower(name.front()) || digit(name.front())) ||
      name.ends_with('.') || !std::ranges::all_of(name, [](char ch) {
        return lower(ch) || digit(ch) || ch == '-' || ch == '.';
      }))
    return false;
  const auto stem = name.substr(0, name.find('.'));
  if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul") return false;
  return !(stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) &&
           digit(stem.back()));
}

bool reparse(const fs::path& path) {
#ifdef _WIN32
  const auto attr = GetFileAttributesW(path.c_str());
  return attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
  return fs::is_symlink(fs::symlink_status(path));
#endif
}

std::string fold_ascii(std::string value) {
  for (auto& ch : value) {
    if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
  }
  return value;
}

}  // namespace

std::string portable(const fs::path& path) {
  const auto value = path.generic_u8string();
  return {value.begin(), value.end()};
}

bool valid_path(std::string_view path) {
  if (!path.starts_with("assets/") || path.ends_with('/')) return false;
  for (auto part : path | std::views::split('/')) {
    if (!valid_segment(std::string_view(part.begin(), part.end()))) return false;
  }
  return true;
}

PathState node(const fs::path& path, bool directory) {
  std::error_code error;
  const auto status = fs::symlink_status(path, error);
  if (status.type() == fs::file_type::not_found) return PathState::missing;
  if (error) throw fs::filesystem_error("status", path, error);
  if ((directory ? !fs::is_directory(status) : !fs::is_regular_file(status)) || reparse(path)) {
    return PathState::unsafe;
  }
  return PathState::ok;
}

PathState project_path(const fs::path& root, const fs::path& relative) {
  auto parent = root;
  const auto end = relative.end();
  for (auto it = relative.begin(); it != end; ++it) {
    const auto name = portable(*it);
    const auto folded = fold_ascii(name);
    int matches = 0;
    bool exact = false;
    for (const auto& child : fs::directory_iterator(parent)) {
      const auto candidate = portable(child.path().filename());
      if (fold_ascii(candidate) != folded) continue;
      ++matches;
      exact = exact || candidate == name;
    }
    if (matches == 0) return PathState::missing;
    if (matches != 1 || !exact) return PathState::unsafe;
    parent /= *it;
    auto next = it;
    const auto state = node(parent, ++next != end);
    if (state != PathState::ok) return state;
  }
  return PathState::ok;
}

std::optional<std::string> read_bytes(const fs::path& file, std::uintmax_t limit) {
  std::ifstream input(file, std::ios::binary | std::ios::ate);
  if (!input) throw fs::filesystem_error("open", file, std::make_error_code(std::errc::io_error));
  const auto length = static_cast<std::streamoff>(input.tellg());
  if (length < 0)
    throw fs::filesystem_error("size", file, std::make_error_code(std::errc::io_error));
  if (static_cast<std::uintmax_t>(length) > limit) return std::nullopt;
  input.seekg(0);
  if (!input) throw fs::filesystem_error("seek", file, std::make_error_code(std::errc::io_error));
  std::string bytes(static_cast<std::size_t>(length), '\0');
  if (!input.read(bytes.data(), length)) {
    throw fs::filesystem_error("read", file, std::make_error_code(std::errc::io_error));
  }
  return bytes;
}

Json parse_json(std::string_view bytes) {
  if (bytes.find('\0') != std::string_view::npos) return Json(Json::value_t::discarded);
  std::vector<std::set<std::string>> keys;
  bool duplicate = false;
  const auto json = Json::parse(
      bytes,
      [&](int depth, Json::parse_event_t event, Json& value) {
        using Event = Json::parse_event_t;
        if (depth > 16) throw std::length_error("project JSON is too deeply nested");
        if (event == Event::object_start) keys.emplace_back();
        if (event == Event::object_end) keys.pop_back();
        if (event == Event::key && !keys.back().insert(value.get<std::string>()).second) {
          duplicate = true;
        }
        return true;
      },
      false);
  return duplicate ? Json(Json::value_t::discarded) : json;
}

}  // namespace wds::platform::detail
