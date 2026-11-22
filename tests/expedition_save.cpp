#include <cstdlib>
#include <filesystem>
#include <string_view>

#include "expedition.hpp"

int main(int argc, char** argv) {
  using namespace wds::expedition;
  if (argc != 4) return 1;
  const auto path = std::filesystem::path(argv[2]);
  auto loaded = load(argv[1], 0);
  if (!loaded.expedition) return 2;
  auto& run = *loaded.expedition;
  const std::string_view stage(argv[3]);
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
