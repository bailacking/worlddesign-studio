#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>

#include "platform_project.hpp"

namespace {

using namespace wds::platform;
using namespace std::chrono_literals;

void require(bool value, const char* explanation) {
  if (value) return;
  std::cerr << explanation << '\n';
  std::exit(1);
}

SceneSession load() {
  auto result = start_scene(".", 0);
  require(result.session.has_value(), scene_result_json(result).c_str());
  return std::move(*result.session);
}

SceneInput keys(std::initializer_list<const char*> values) {
  SceneInput input;
  for (const auto* value : values) input.set(static_cast<std::size_t>(*scene_key(value)));
  return input;
}

std::string read(const char* path) {
  std::ifstream file(path, std::ios::binary);
  require(static_cast<bool>(file), path);
  return {std::istreambuf_iterator<char>{file}, {}};
}

}  // namespace

int main() {
  const auto scene = read("assets/reef.json");
  const auto manifest = read("wds.project.json");
  const auto sprite = read("assets/courier.png");
  auto session = load();
  const auto& state = session.snapshot();
  const auto& player = state.entities.back();
  require(state.interactive && session.sprites().size() == 4 && player.x == -223,
          "Expected declared sprite resources and the saved player spawn");
  for (const auto& image : session.sprites())
    require(image.width > 0 && image.height > 0 &&
                image.rgba.size() == std::size_t{image.width} * image.height * 4,
            "PNG texture does not contain complete RGBA pixels");

  SceneClock even;
  SceneClock uneven;
  require(even.consume(100ms) == 3, "100ms should be three simulation ticks");
  require(uneven.consume(33ms) == 0 && uneven.consume(34ms) == 2 && uneven.consume(33ms) == 1,
          "Frame timing should preserve fractional ticks");
  require(uneven.consume(900ms) == 7 && uneven.consume(17ms) == 1 && uneven.consume(-1ms) == 0,
          "Long frames should have a bounded catch-up");

  require(session.advance(1, keys({"Right", "D"})) && player.x == -217,
          "Arrow and letter aliases must not double movement");
  require(session.advance(1, keys({"Right", "A"})) && player.x == -217,
          "Opposing directions must cancel");
  require(session.advance(50, keys({"D"})) && player.x == -23 && player.y == 0 &&
              state.outcome == SceneOutcome::playing,
          "Player must stop exactly at the barrier");
  require(session.advance(19, keys({"W"})) && player.x == -23 && player.y == 114,
          "Player must slide beside a wall without crossing it");
  require(session.advance(38, keys({"Right"})) && player.x == 205,
          "Player must be able to move around the barrier");
  require(session.advance(20, keys({"Down"})) && state.outcome == SceneOutcome::won &&
              player.y == 42 && state.ticks == 121,
          "Entering the delivery zone must stop at the first winning tick");
  require(session.advance(10, keys({"D"})) && player.x == 205 && state.ticks == 121,
          "Terminal snapshots must remain available without further simulation");

  auto hazard = load();
  require(hazard.advance(13, keys({"Down"})) && hazard.advance(30, keys({"Right"})) &&
              hazard.snapshot().outcome == SceneOutcome::lost && hazard.snapshot().ticks == 23,
          "Entering the reef must stop on the first losing tick");
  auto idle = load();
  require(idle.advance(SceneSession::max_ticks) &&
              idle.snapshot().outcome == SceneOutcome::timed_out &&
              idle.snapshot().entities.back().x == -223 && !idle.advance(1),
          "Empty input should be still and time out at the tick budget");
  require(!start_scene(".", 1).session, "The saved revision must be checked");
  require(edit_entity(".", {"remove", "courier", 0}, false).code == "invalid_scene" &&
              edit_scene(".", {"courier", "x", 0, 0}, false).code == "invalid_scene",
          "Edits must not remove the active player or spawn it inside a wall");
  require(read("assets/reef.json") == scene && read("wds.project.json") == manifest &&
              read("assets/courier.png") == sprite,
          "Starting, advancing and discarding a session must preserve source bytes");
}
