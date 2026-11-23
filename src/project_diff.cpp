#include "project_diff.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <string_view>

#include "platform_project.hpp"
#include "project_io.hpp"

namespace wds::platform {
namespace {

using Json = nlohmann::json;
namespace fs = std::filesystem;
constexpr std::uintmax_t file_limit = 16 * 1024 * 1024;
constexpr std::uintmax_t total_limit = 128 * 1024 * 1024;
constexpr std::size_t fields_limit = 64;

std::string segment(std::string_view key) {
  std::string result;
  for (const char byte : key) {
    if (byte == '~')
      result += "~0";
    else if (byte == '/')
      result += "~1";
    else
      result += byte;
  }
  return result;
}

using Members = std::map<std::string, const Json*, std::less<>>;

std::optional<Members> identified(const Json& rows) {
  if (!rows.is_array()) return {};
  Members result;
  for (const auto& row : rows) {
    if (!row.is_object() || !row.contains("id") || !row["id"].is_string() ||
        !result.emplace(row["id"].get<std::string>(), &row).second)
      return {};
  }
  return result;
}

struct Changes {
  Json fields = Json::array();
  bool truncated = false;

  void append(const std::string& path, const char* kind) {
    if (fields.size() == fields_limit) {
      truncated = true;
      return;
    }
    fields.push_back({{"path", path}, {"kind", kind}});
  }

  void compare(const Json& before, const Json& after, const std::string& path) {
    if (before == after || truncated) return;
    if (before.is_object() && after.is_object()) {
      Members left, right;
      for (const auto& [key, value] : before.items()) left.emplace(key, &value);
      for (const auto& [key, value] : after.items()) right.emplace(key, &value);
      compare_members(left, right, path);
      return;
    }
    if (path == "/entities") {
      const auto left = identified(before);
      const auto right = identified(after);
      if (left && right) {
        compare_members(*left, *right, path);
        const auto shared_order = [](const Json& rows, const Members& other) {
          std::vector<std::string> ids;
          for (const auto& row : rows) {
            const auto& id = row["id"].get_ref<const std::string&>();
            if (other.contains(id)) ids.push_back(id);
          }
          return ids;
        };
        if (shared_order(before, *right) != shared_order(after, *left))
          append(path, "order_changed");
        return;
      }
    }
    append(path, "changed");
  }

  void compare_members(const Members& before, const Members& after, const std::string& path) {
    for (const auto& [key, value] : before) {
      const auto member = path + "/" + segment(key);
      if (const auto next = after.find(key); next != after.end())
        compare(*value, *next->second, member);
      else
        append(member, "removed");
      if (truncated) return;
    }
    for (const auto& [key, value] : after) {
      (void)value;
      if (!before.contains(key)) append(path + "/" + segment(key), "added");
      if (truncated) return;
    }
  }
};

bool json_resource(const Resource& resource) {
  return resource.kind == "scene" || (resource.kind == "data" && resource.path.ends_with(".json"));
}

struct Unreadable {
  std::string location;
};

std::string read(const fs::path& root, const Resource& resource, std::uintmax_t& remaining) {
  if (detail::project_path(root, resource.path) != detail::PathState::ok)
    throw Unreadable{resource.path};
  auto bytes = detail::read_bytes(root / resource.path, std::min(remaining, file_limit));
  if (!bytes) throw Unreadable{resource.path};
  remaining -= bytes->size();
  return std::move(*bytes);
}

void describe_content(const Resource& before, const Resource& after, const std::string& left,
                      const std::string& right, Json& row) {
  if (left == right) return;
  if (json_resource(before) && json_resource(after)) {
    try {
      const auto a = detail::parse_json(left);
      const auto b = detail::parse_json(right);
      if (a.is_discarded() || b.is_discarded()) {
        row["content"] = "unparsed_json";
        return;
      }
      Changes changes;
      changes.compare(a, b, "");
      if (changes.fields.empty()) {
        row["content"] = "formatting";
      } else {
        row["content"] = "json";
        row["fields"] = std::move(changes.fields);
        if (changes.truncated) row["fields_truncated"] = true;
      }
      return;
    } catch (const std::length_error&) {
      row["content"] = "unparsed_json";
      return;
    }
  }
  row["content"] = "bytes";
}

}  // namespace

Json compare_projects(const fs::path& before, std::uint64_t before_revision, const fs::path& after,
                      std::uint64_t after_revision) {
  const auto left = open_project(before);
  if (!left.project) return {{"code", left.code}, {"location", left.location}, {"side", "before"}};
  const auto right = open_project(after);
  if (!right.project)
    return {{"code", right.code}, {"location", right.location}, {"side", "after"}};
  if (left.project->revision != before_revision || right.project->revision != after_revision)
    return {{"code", "revision_conflict"},
            {"before_revision", left.project->revision},
            {"after_revision", right.project->revision}};
  if (left.project->id != right.project->id)
    return {{"code", "different_project"}, {"location", "/project_id"}};

  struct Pair {
    const Resource* before = nullptr;
    const Resource* after = nullptr;
  };
  std::map<std::string_view, Pair> resources;
  for (const auto& resource : left.project->resources) resources[resource.id].before = &resource;
  for (const auto& resource : right.project->resources) resources[resource.id].after = &resource;
  auto changes = Json::array();
  std::uintmax_t remaining = total_limit;
  try {
    for (const auto& [id, pair] : resources) {
      if (!pair.before || !pair.after) {
        changes.push_back({{"id", id},
                           {"kind", pair.before ? "removed" : "added"},
                           {"path", (pair.before ? pair.before : pair.after)->path}});
        continue;
      }
      const auto& a = *pair.before;
      const auto& b = *pair.after;
      Json row{{"id", id}, {"kind", "modified"}};
      if (a.path != b.path) row["path"] = {{"before", a.path}, {"after", b.path}};
      if (a.kind != b.kind) row["resource_kind"] = {{"before", a.kind}, {"after", b.kind}};
      const std::set<std::string> a_refs(a.refs.begin(), a.refs.end());
      const std::set<std::string> b_refs(b.refs.begin(), b.refs.end());
      if (a_refs != b_refs) row["refs"] = {{"before", a_refs}, {"after", b_refs}};
      const auto old_bytes = read(before, a, remaining);
      const auto new_bytes = read(after, b, remaining);
      describe_content(a, b, old_bytes, new_bytes, row);
      if (row.size() > 2) changes.push_back(std::move(row));
    }
  } catch (const Unreadable& error) {
    return {{"code", "unreadable_resource"}, {"location", error.location}};
  }
  Json reply{{"code", "ok"},
             {"project_id", left.project->id},
             {"before_revision", before_revision},
             {"after_revision", after_revision},
             {"resources", std::move(changes)}};
  if (left.project->entry_scene != right.project->entry_scene)
    reply["entry_scene"] = {{"before", left.project->entry_scene},
                            {"after", right.project->entry_scene}};
  return reply;
}

}  // namespace wds::platform
