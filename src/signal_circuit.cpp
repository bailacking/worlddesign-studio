#include "signal_circuit.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace wds::voxel {
namespace {

using Json = nlohmann::json;
struct Invalid {
  std::string location;
};

enum class Kind { lever, wire, repeater, lamp, comparator };
enum class CompareMode { compare, subtract };

constexpr int max_tick = 65536;
struct Pending {
  int due;
  int level;
};
struct Node {
  Kind kind;
  std::vector<std::string> inputs;
  int delay = 0;
  int level = 0;
  std::optional<Pending> pending{};
  CompareMode mode = CompareMode::compare;
};
using Nodes = std::map<std::string, Node>;
using Events = std::map<int, std::map<std::string, int>>;

bool valid_id(std::string_view id) {
  return !id.empty() && id.size() <= 32 && std::ranges::all_of(id, [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
  });
}

int bounded(const Json& value, int min, int max, const std::string& path) {
  if (value.is_number_unsigned()) {
    const auto number = value.get<std::uint64_t>();
    if (number >= static_cast<std::uint64_t>(min) && number <= static_cast<std::uint64_t>(max))
      return static_cast<int>(number);
  } else if (value.is_number_integer()) {
    const auto number = value.get<std::int64_t>();
    if (number >= min && number <= max) return static_cast<int>(number);
  }
  throw Invalid{path};
}

Kind kind(const Json& value, const std::string& location) {
  if (value == "lever") return Kind::lever;
  if (value == "wire") return Kind::wire;
  if (value == "repeater") return Kind::repeater;
  if (value == "lamp") return Kind::lamp;
  if (value == "comparator") return Kind::comparator;
  throw Invalid{location};
}

CompareMode compare_mode(const Json& value, const std::string& location) {
  if (value == "compare") return CompareMode::compare;
  if (value == "subtract") return CompareMode::subtract;
  throw Invalid{location};
}

Nodes read_nodes(const Json& json) {
  if (!json.is_object() || json.empty() || json.size() > 64) throw Invalid{"/input/nodes"};
  Nodes nodes;
  for (const auto& [id, entry] : json.items()) {
    const auto path = "/input/nodes/" + id;
    if (!valid_id(id) || !entry.is_object() || !entry.contains("kind") ||
        !entry.contains("inputs") || !entry["inputs"].is_array())
      throw Invalid{path};
    const auto type = kind(entry["kind"], path + "/kind");
    Node node{type, {}};
    if (type == Kind::repeater) {
      if (entry.size() != 3 || !entry.contains("delay")) throw Invalid{path};
      node.delay = bounded(entry["delay"], 1, 8, path + "/delay");
    } else if (type == Kind::comparator) {
      if (entry.size() != 3 || !entry.contains("mode")) throw Invalid{path};
      node.mode = compare_mode(entry["mode"], path + "/mode");
    } else if (entry.size() != 2) {
      throw Invalid{path};
    }
    const auto& inputs = entry["inputs"];
    if (type == Kind::lever ? !inputs.empty()
                            : inputs.empty() || inputs.size() > (type == Kind::repeater ? 1 : 4))
      throw Invalid{path + "/inputs"};
    if (type == Kind::comparator && inputs.size() > 3) throw Invalid{path + "/inputs"};
    std::set<std::string> seen;
    for (const auto& source : inputs) {
      if (!source.is_string()) throw Invalid{path + "/inputs"};
      const auto& name = source.get_ref<const std::string&>();
      if (name == id || !json.contains(name) || !seen.insert(name).second)
        throw Invalid{path + "/inputs"};
      node.inputs.push_back(name);
    }
    nodes.emplace(id, std::move(node));
  }
  return nodes;
}

Json checkpoint(const Json& layout, const Nodes& nodes, int tick) {
  auto state = Json::object();
  for (const auto& [id, node] : nodes) {
    auto& row = state[id];
    row = {{"level", node.level}};
    if (node.pending) row["pending"] = {{"due", node.pending->due}, {"level", node.pending->level}};
  }
  return {{"version", 1}, {"layout", layout}, {"tick", tick}, {"nodes", std::move(state)}};
}

int resume(const Json& state, const Json& layout, Nodes& nodes) {
  const std::string path = "/input/resume";
  if (!state.is_object() || state.size() != 4 || !state.contains("version") ||
      !state.contains("layout") || !state.contains("tick") || !state.contains("nodes"))
    throw Invalid{path};
  bounded(state["version"], 1, 1, path + "/version");
  if (state["layout"] != layout) throw Invalid{path + "/layout"};
  const auto tick = bounded(state["tick"], 1, max_tick, path + "/tick");
  const auto& saved = state["nodes"];
  if (!saved.is_object() || saved.size() != nodes.size()) throw Invalid{path + "/nodes"};
  for (auto& [id, node] : nodes) {
    const auto current = path + "/nodes/" + id;
    const auto row = saved.find(id);
    if (row == saved.end() || !row->is_object() || !row->contains("level") ||
        row->size() != 1U + row->count("pending"))
      throw Invalid{current};
    node.level = bounded((*row)["level"], 0, 15, current + "/level");
    if ((node.kind == Kind::lamp || node.kind == Kind::repeater) && node.level != 0 &&
        node.level != 15)
      throw Invalid{current + "/level"};
    if (const auto pending = row->find("pending"); pending != row->end()) {
      const auto where = current + "/pending";
      if (node.kind != Kind::repeater || !pending->is_object() || pending->size() != 2 ||
          !pending->contains("due") || !pending->contains("level"))
        throw Invalid{where};
      const auto level = bounded((*pending)["level"], 0, 15, where + "/level");
      if (level != 15 - node.level) throw Invalid{where + "/level"};
      const auto due = bounded((*pending)["due"], tick, tick + node.delay - 1, where + "/due");
      node.pending = Pending{due, level};
    }
  }
  return tick;
}

int signal(const Node& node, const std::map<std::string, int>& snapshot) {
  if (node.kind == Kind::comparator) {
    const auto rear = snapshot.at(node.inputs.front());
    int side = 0;
    for (std::size_t index = 1; index < node.inputs.size(); ++index)
      side = std::max(side, snapshot.at(node.inputs[index]));
    if (node.mode == CompareMode::subtract) return std::max(0, rear - side);
    return rear < side ? 0 : rear;
  }
  int strength = 0;
  for (const auto& name : node.inputs) strength = std::max(strength, snapshot.at(name));
  return node.kind == Kind::wire ? std::max(0, strength - 1) : (strength ? 15 : 0);
}

Events read_events(const Json& json, const Nodes& nodes, int ticks) {
  if (!json.is_array() || json.size() > 256) throw Invalid{"/input/events"};
  Events events;
  for (std::size_t i = 0; i < json.size(); ++i) {
    const auto path = "/input/events/" + std::to_string(i);
    const auto& row = json[i];
    if (!row.is_object() || row.size() != 3 || !row.contains("tick") || !row.contains("level") ||
        !row.contains("id") || !row["id"].is_string())
      throw Invalid{path};
    const auto tick = bounded(row["tick"], 0, ticks - 1, path + "/tick");
    const auto level = bounded(row["level"], 0, 15, path + "/level");
    const auto& id = row["id"].get_ref<const std::string&>();
    const auto found = nodes.find(id);
    if (found == nodes.end() || found->second.kind != Kind::lever) throw Invalid{path + "/id"};
    if (!events[tick].emplace(id, level).second) throw Invalid{path};
  }
  return events;
}

}  // namespace

Json replay_circuit(const Json& input) {
  try {
    if (!input.is_object() ||
        input.size() != 3U + input.count("checkpoint") + input.count("resume") ||
        !input.contains("ticks") || !input.contains("nodes") || !input.contains("events"))
      throw Invalid{"/input"};
    if (input.contains("checkpoint") && !input["checkpoint"].is_boolean())
      throw Invalid{"/input/checkpoint"};
    const auto ticks = bounded(input["ticks"], 1, 256, "/input/ticks");
    auto nodes = read_nodes(input["nodes"]);
    const auto events = read_events(input["events"], nodes, ticks);
    int start = 0;
    if (input.contains("resume")) start = resume(input["resume"], input["nodes"], nodes);
    if (start + ticks > max_tick) throw Invalid{"/input/ticks"};
    auto trace = Json::array();
    for (int tick = start; tick < start + ticks; ++tick) {
      if (const auto found = events.find(tick - start); found != events.end()) {
        for (const auto& [id, level] : found->second) {
          auto& node = nodes.at(id);
          trace.push_back({{"tick", tick},
                           {"phase", "input"},
                           {"id", id},
                           {"from", node.level},
                           {"to", level}});
          node.level = level;
        }
      }
      std::map<std::string, int> snapshot;
      for (const auto& [id, node] : nodes) snapshot.emplace(id, node.level);
      for (auto& [id, node] : nodes) {
        if (node.kind == Kind::lever) continue;
        const int wanted = signal(node, snapshot);
        if (node.kind == Kind::repeater) {
          if (node.pending && node.pending->level != wanted) {
            trace.push_back({{"tick", tick},
                             {"phase", "cancel"},
                             {"id", id},
                             {"due", node.pending->due},
                             {"to", node.pending->level},
                             {"inputs", node.inputs}});
            node.pending.reset();
          }
          if (wanted == node.level) continue;
          if (!node.pending) {
            node.pending = Pending{tick + node.delay, wanted};
            trace.push_back({{"tick", tick},
                             {"phase", "schedule"},
                             {"id", id},
                             {"due", node.pending->due},
                             {"to", wanted},
                             {"inputs", node.inputs}});
          }
          if (node.pending->due != tick) continue;
          node.pending.reset();
        }
        if (wanted == node.level) continue;
        trace.push_back({{"tick", tick},
                         {"phase", "update"},
                         {"id", id},
                         {"from", node.level},
                         {"to", wanted},
                         {"inputs", node.inputs}});
        node.level = wanted;
      }
    }
    auto levels = Json::object();
    for (const auto& [id, node] : nodes) levels[id] = node.level;
    Json reply{{"code", "ok"}, {"levels", std::move(levels)}, {"trace", std::move(trace)}};
    if (input.value("checkpoint", false))
      reply["checkpoint"] = checkpoint(input["nodes"], nodes, start + ticks);
    return reply;
  } catch (const Invalid& error) {
    return {{"code", "invalid_circuit"}, {"location", error.location}};
  }
}

}  // namespace wds::voxel
