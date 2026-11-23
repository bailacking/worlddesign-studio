#include <emscripten/emscripten.h>

#include <cstring>

#include "expedition.hpp"
#include "project_io.hpp"

namespace {

constexpr auto project = "/garden";
constexpr std::size_t request_limit = 32 * 1024;

}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE const char* wds_garden_rules() {
  static const auto rules =
      wds::platform::detail::read_bytes("/garden/assets/expedition.json", 1024 * 1024);
  return rules ? rules->c_str() : nullptr;
}

EMSCRIPTEN_KEEPALIVE const char* wds_garden_replay(const char* actions) {
  static const auto initial = wds::expedition::load(project, 0);
  if (!initial.expedition) return R"({"code":"runtime_unavailable"})";
  if (!actions || std::strlen(actions) > request_limit) return R"({"code":"invalid_actions"})";
  auto input = wds::platform::detail::parse_json(actions);
  if (!input.is_array() || input.size() > 256) return R"({"code":"invalid_actions"})";
  const auto replay = wds::expedition::replay(*initial.expedition, actions);
  auto result = wds::platform::detail::parse_json(replay);
  if (result["code"] == "ok") {
    auto& steps = result["steps"];
    result["latest"] = steps.empty() ? nlohmann::json(nullptr) : std::move(steps.back());
    result.erase("steps");
  }
  static std::string reply;
  reply = result.dump();
  return reply.c_str();
}

}  // extern "C"
