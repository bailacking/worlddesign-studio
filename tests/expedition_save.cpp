#include <array>
#include <cstdlib>
#include <filesystem>
#include <string_view>

#include "expedition.hpp"

namespace {

using namespace wds::expedition;

int farm(Expedition& run, const std::filesystem::path& file, std::string_view stage) {
  if (stage == "farm-start") {
    const std::array actions{Action{Command::gather, "seed-mat"},
                             Action{Command::gather, "herbs"},
                             Action{Command::gather, "bark-store"},
                             Action{Command::craft, "build-stove"},
                             Action{Command::east},
                             Action{Command::use, "plot"},
                             Action{Command::use, "trail"}};
    for (const auto& action : actions)
      if (run.apply(action) != "ok") return 12;
    return save(run, file) ? 0 : 13;
  }
  if (!restore(run, file)) return 14;
  if (stage == "farm-cook") {
    if (run.region().id != "tide-terrace" || run.state().crops.at("plot") != 2) return 15;
    const std::array actions{Action{Command::wait},
                             Action{Command::wait},
                             Action{Command::use, "return-path"},
                             Action{Command::use, "plot"},
                             Action{Command::east},
                             Action{Command::cook, "kitchen", {"root", "herb"}}};
    for (const auto& action : actions)
      if (run.apply(action) != "ok") return 16;
    return save(run, file) ? 0 : 17;
  }
  return stage == "farm-verify" && run.region().id == "wind-garden" &&
                 run.position() == TilePoint{3, 1} && run.state().tick == 13 &&
                 run.state().crops.at("plot") == -1 && run.workshop().has_item("stew") &&
                 !run.workshop().has_item("root") && !run.workshop().has_item("herb")
             ? 0
             : 18;
}

}  // namespace

int main(int argc, char** argv) {
  using namespace wds::expedition;
  if (argc != 4) return 1;
  const auto path = std::filesystem::path(argv[2]);
  auto loaded = load(argv[1], 0);
  if (!loaded.expedition) return 2;
  auto& run = *loaded.expedition;
  const std::string_view stage(argv[3]);
  if (stage.starts_with("farm-")) return farm(run, path, stage);
  if (stage == "start") {
    if (run.apply({Command::gather, "driftwood"}) != "ok" || run.apply({Command::east}) != "ok" ||
        run.apply({Command::use, "rising"}) != "ok" || run.apply({Command::east}) != "ok")
      return 3;
    return save(run, path) ? 0 : 4;
  }
  if (!restore(run, path)) return 5;
  if (run.region().id != "reed-bank" || run.state().tick != 4 ||
      run.position() != TilePoint{3, 1} || !run.workshop().has_item("reed"))
    return 6;
  if (run.apply({Command::use, "cave-mouth"}) != "ok") return 7;
  if (stage == "interrupt") {
    save(run, path, [] { std::_Exit(73); });
    return 8;
  }
  if (stage != "verify" || !save(run, path)) return 9;
  loaded = load(argv[1], 0);
  if (!loaded.expedition || !restore(*loaded.expedition, path)) return 10;
  const auto& resumed = *loaded.expedition;
  return resumed.region().id == "blue-cave" && resumed.state().tick == 5 &&
                 resumed.position() == TilePoint{1, 1} &&
                 resumed.state().positions.front() == TilePoint{3, 1} &&
                 resumed.workshop().has_item("reed")
             ? 0
             : 11;
}
