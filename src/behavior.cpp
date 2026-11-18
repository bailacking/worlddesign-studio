#include <algorithm>
#include <array>
#include <charconv>
#include <limits>

#include "behavior_document.hpp"

namespace wds::behavior {
namespace {

using Json = nlohmann::json;
constexpr std::size_t max_text = 64 * 1024;
constexpr std::size_t max_nodes = 256;

struct DocumentError {
  std::string code;
  std::string location;
};

bool identifier(std::string_view name) {
  const auto lower = [](char c) { return c >= 'a' && c <= 'z'; };
  return !name.empty() && name.size() <= 32 && lower(name.front()) &&
         std::ranges::all_of(
             name, [&](char c) { return lower(c) || c == '_' || (c >= '0' && c <= '9'); });
}

std::optional<Value> integer(std::string_view text) {
  if (text.empty()) return {};
  Value value{};
  const auto read = std::from_chars(text.data(), text.data() + text.size(), value);
  if (read.ec == std::errc{} && read.ptr == text.data() + text.size()) return value;
  return {};
}

Json operand(std::string_view token) {
  if (const auto value = integer(token)) return *value;
  return identifier(token) ? Json(token) : Json();
}

bool action(std::string_view token) { return token == "set" || token == "add"; }

Json parse_line(std::string_view raw) {
  constexpr std::string_view space = " \t\r\n";
  const auto first = raw.find_first_not_of(space);
  if (first == raw.npos || raw[first] == '#') return {{"type", "comment"}, {"text", raw}};

  std::array<std::string_view, 9> words;
  std::size_t count = 0;
  for (auto cursor = first; cursor != raw.npos; cursor = raw.find_first_not_of(space, cursor)) {
    if (count == words.size()) return {{"type", "opaque"}, {"text", raw}};
    const auto end = raw.find_first_of(space, cursor);
    words[count++] = raw.substr(cursor, end - cursor);
    cursor = end;
    if (cursor == raw.npos) break;
  }
  if (count == 3 && (words[0] == "param" || words[0] == "state") && identifier(words[1])) {
    if (const auto value = integer(words[2]))
      return {{"type", words[0]}, {"name", words[1]}, {"value", *value}};
  }
  const std::size_t effect = count == 9 ? 6 : 2;
  if ((count == 5 || count == 9) && words[0] == "on" &&
      (words[1] == "start" || words[1] == "tick") && action(words[effect]) &&
      identifier(words[effect + 1])) {
    const auto value = operand(words[effect + 2]);
    if (!value.is_null()) {
      Json rule{{"type", "rule"},
                {"event", words[1]},
                {"then", {{"op", words[effect]}, {"target", words[effect + 1]}, {"value", value}}}};
      if (count == 5) return rule;
      const auto left = operand(words[3]);
      const auto right = operand(words[5]);
      if (words[2] == "if" && (words[4] == "==" || words[4] == "<" || words[4] == ">=") &&
          !left.is_null() && !right.is_null()) {
        rule["when"] = {{"left", left}, {"op", words[4]}, {"right", right}};
        return rule;
      }
    }
  }
  return {{"type", "opaque"}, {"text", raw}};
}

const std::string& text_at(const Json& node, const char* key) {
  return node.at(key).get_ref<const std::string&>();
}

std::string token(const Json& value) {
  return value.is_string() ? value.get<std::string>() : value.dump();
}

std::string format_line(const Json& node) {
  const auto& type = text_at(node, "type");
  if (type == "comment" || type == "opaque") return text_at(node, "text");
  if (type == "state" || type == "param")
    return type + " " + text_at(node, "name") + " " + token(node.at("value")) + "\n";
  if (type != "rule") return {};
  auto result = "on " + text_at(node, "event");
  if (node.contains("when")) {
    const auto& when = node.at("when");
    result +=
        " if " + token(when.at("left")) + " " + text_at(when, "op") + " " + token(when.at("right"));
  }
  const auto& then = node.at("then");
  return result + " " + text_at(then, "op") + " " + text_at(then, "target") + " " +
         token(then.at("value")) + "\n";
}

std::string location(std::string_view format, std::size_t index) {
  if (format == "code") return "/document/line/" + std::to_string(index + 1);
  return "/document/nodes/" + std::to_string(index);
}

Json read_nodes(std::string_view format, const Json& document) {
  if (format == "code" && document.is_string()) {
    std::string_view source = document.get_ref<const std::string&>();
    if (source.size() > max_text) throw DocumentError{"behavior_too_large", "/document"};
    auto nodes = Json::array();
    while (!source.empty()) {
      if (nodes.size() == max_nodes) throw DocumentError{"behavior_too_large", "/document"};
      const auto end = source.find('\n');
      const auto size = end == source.npos ? source.size() : end + 1;
      nodes.push_back(parse_line(source.substr(0, size)));
      source.remove_prefix(size);
    }
    return nodes;
  }
  if (format == "graph" && document.is_object() && document.size() == 2 &&
      document.contains("version") && document["version"].is_number_integer() &&
      document["version"] == 1 && document.contains("nodes") && document["nodes"].is_array()) {
    const auto& nodes = document["nodes"];
    if (nodes.size() > max_nodes) throw DocumentError{"behavior_too_large", "/document/nodes"};
    std::size_t size = 0;
    for (std::size_t index = 0; index < nodes.size(); ++index) {
      try {
        const auto line = format_line(nodes[index]);
        if (!line.empty() && line.find('\n') >= line.size() - 1 &&
            (index + 1 == nodes.size() || line.ends_with('\n')) &&
            parse_line(line) == nodes[index]) {
          size += line.size();
          if (size > max_text) throw DocumentError{"behavior_too_large", "/document"};
          continue;
        }
      } catch (const Json::exception&) {
        // Shape and token errors share a node location.
      }
      throw DocumentError{"invalid_behavior", location(format, index)};
    }
    return nodes;
  }
  throw DocumentError{"invalid_behavior", "/document"};
}

Operand resolve(const Json& item, const Program& program, const std::string& where) {
  if (item.is_number_integer()) return item.get<Value>();
  const auto& name = item.get_ref<const std::string&>();
  if (program.initial_state.contains(name) || program.parameters.contains(name)) return name;
  throw DocumentError{"unknown_behavior_value", where};
}

Comparison comparison(std::string_view op) {
  if (op == "<") return Comparison::less;
  if (op == ">=") return Comparison::at_least;
  return Comparison::equal;
}

Program compile(const Json& nodes, std::string_view format) {
  Program program;
  for (std::size_t index = 0; index < nodes.size(); ++index) {
    const auto& node = nodes[index];
    const auto& type = text_at(node, "type");
    if (type == "opaque") throw DocumentError{"unsupported_behavior", location(format, index)};
    if (type != "state" && type != "param") continue;
    const auto& name = text_at(node, "name");
    if (program.initial_state.contains(name) || program.parameters.contains(name))
      throw DocumentError{"duplicate_behavior_value", location(format, index)};
    auto& values = type == "state" ? program.initial_state : program.parameters;
    values.emplace(name, node.at("value").get<Value>());
  }
  for (std::size_t index = 0; index < nodes.size(); ++index) {
    const auto& node = nodes[index];
    if (node["type"] != "rule") continue;
    const auto where = location(format, index);
    const auto& then = node.at("then");
    const auto& target = text_at(then, "target");
    if (!program.initial_state.contains(target))
      throw DocumentError{"unknown_behavior_state", where};
    Rule rule{text_at(node, "event") == "tick" ? Event::tick : Event::start,
              {},
              target,
              resolve(then.at("value"), program, where),
              text_at(then, "op") == "add"};
    if (node.contains("when")) {
      const auto& when = node.at("when");
      rule.condition =
          Condition{resolve(when.at("left"), program, where), comparison(text_at(when, "op")),
                    resolve(when.at("right"), program, where)};
    }
    program.rules.push_back(std::move(rule));
  }
  return program;
}

bool matches(Value left, Comparison op, Value right) {
  switch (op) {
    case Comparison::less:
      return left < right;
    case Comparison::at_least:
      return left >= right;
    case Comparison::equal:
      return left == right;
  }
  return false;
}

}  // namespace

LoadResult load_document(std::string_view format, const Json& document) {
  try {
    return {compile(read_nodes(format, document), format), "ok", {}};
  } catch (const DocumentError& error) {
    return {{}, error.code, error.location};
  }
}

Json convert_document(std::string_view from, std::string_view to, const Json& document) {
  if (from != "code" && from != "graph")
    return {{"code", "invalid_argument"}, {"location", "/input/from"}};
  if (to != "code" && to != "graph")
    return {{"code", "invalid_argument"}, {"location", "/input/to"}};
  try {
    auto nodes = read_nodes(from, document);
    auto opaque = Json::array();
    std::string source;
    for (std::size_t index = 0; index < nodes.size(); ++index) {
      if (nodes[index]["type"] == "opaque") opaque.push_back(location(from, index));
      if (to == "code") source += format_line(nodes[index]);
    }
    // Canonical code can add a newline to the final supported statement.
    auto output =
        to == "code" ? Json(std::move(source)) : Json{{"version", 1}, {"nodes", std::move(nodes)}};
    read_nodes(to, output);
    return {{"code", "ok"}, {"document", std::move(output)}, {"opaque", std::move(opaque)}};
  } catch (const DocumentError& error) {
    return {{"code", error.code}, {"location", error.location}};
  }
}

void Program::dispatch(Event event, Values& state) const {
  const auto read = [&](const Operand& operand) -> Value {
    if (const auto* literal = std::get_if<Value>(&operand)) return *literal;
    const auto& name = std::get<std::string>(operand);
    const auto found = state.find(name);
    return found == state.end() ? parameters.at(name) : found->second;
  };
  for (const auto& rule : rules) {
    if (rule.event != event) continue;
    if (rule.condition && !matches(read(rule.condition->left), rule.condition->comparison,
                                   read(rule.condition->right)))
      continue;
    auto& target = state.at(rule.target);
    const auto value = read(rule.value);
    const auto sum = static_cast<std::int64_t>(target) + value;
    target = rule.add
                 ? static_cast<Value>(std::clamp<std::int64_t>(
                       sum, std::numeric_limits<Value>::min(), std::numeric_limits<Value>::max()))
                 : value;
  }
}

}  // namespace wds::behavior
