#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "expedition.hpp"
#include "platform_project.hpp"
#include "player_view.hpp"
#include "project_export.hpp"

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR arguments, int) {
  using namespace wds::platform;
  const auto option = std::wstring_view(arguments);
  const bool check_only = option == L"--check";
  const bool smoke = option == L"--expedition-smoke";
  const auto failure = [check_only, smoke](std::string message) {
    if (!check_only && !smoke) {
      const int length = MultiByteToWideChar(CP_UTF8, 0, message.c_str(), -1, nullptr, 0);
      std::wstring text(static_cast<std::size_t>(length), L'\0');
      MultiByteToWideChar(CP_UTF8, 0, message.c_str(), -1, text.data(), length);
      MessageBoxW(nullptr, text.c_str(), L"WDS Game", MB_ICONERROR | MB_OK);
    }
    return 1;
  };
  const auto folder = executable_directory();
  if (folder.empty()) return failure("Could not locate the exported game.");
  const auto verified = verify_release(folder);
  if (!verified.revision)
    return failure("The exported game is incomplete or has changed.\n\n" +
                   export_result_json(verified));
  auto scene = start_scene(folder / "game", *verified.revision, true);
  if (!scene.session && !scene.voxel_scene)
    return failure("Could not load the game scene.\n\n" + scene_result_json(scene));
  auto journey = wds::expedition::load(folder / "game", *verified.revision);
  if (journey.code != "ok" && journey.code != "no_expedition")
    return failure("Could not load expedition: " + journey.location);
  if (check_only) return 0;
  if (smoke)
    return journey.expedition &&
                   wds::player::play_expedition(*journey.expedition, journey.icons, {}, true)
               ? 0
               : 1;
  if (journey.expedition) {
    const auto guard = folder / L".expedition.lock";
    const HANDLE lock = CreateFileW(guard.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                    OPEN_ALWAYS, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (lock == INVALID_HANDLE_VALUE) return failure("The expedition save is already in use.");
    const bool played = wds::player::play_expedition(*journey.expedition, journey.icons,
                                                     folder / L"expedition.save.json");
    CloseHandle(lock);
    return played ? 0 : failure("Could not open the expedition save or graphics.");
  }
  const bool played = scene.voxel_scene ? wds::player::play_voxel(*scene.voxel_scene, nullptr)
                                        : wds::player::play_scene(*scene.session, nullptr);
  if (!played) return failure("Could not start graphics. The game requires OpenGL 3.3.");
  return 0;
}
