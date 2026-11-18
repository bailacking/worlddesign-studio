#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace wds::behavior {

enum class Event { start, tick };
enum class Comparison { equal, less, at_least };

using Value = std::int32_t;
using Values = std::map<std::string, Value, std::less<>>;
using Operand = std::variant<Value, std::string>;

struct Condition {
  Operand left;
  Comparison comparison;
  Operand right;
};

struct Rule {
  Event event;
  std::optional<Condition> condition;
  std::string target;
  Operand value;
  bool add;
};

struct Program {
  Values parameters;
  Values initial_state;
  std::vector<Rule> rules;

  void dispatch(Event event, Values& state) const;
};

}  // namespace wds::behavior
