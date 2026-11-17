#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "platform_project.hpp"
#include "player_view.hpp"
#include "project_export.hpp"

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR arguments, int) {
  using namespace wds::platform;
  const bool check_only = std::wstring_view(arguments) == L"--check";
  const auto failure = [check_only](std::string message) {
    if (!check_only) {
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
  auto scene = start_scene(folder / "game", *verified.revision);
  if (!scene.session)
    return failure("Could not load the game scene.\n\n" + scene_result_json(scene));
  if (check_only) return 0;
  if (!wds::player::play_scene(*scene.session, "WDS Game"))
    return failure("Could not start graphics. The game requires OpenGL 3.3.");
  return 0;
}
