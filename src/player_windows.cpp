#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "expedition.hpp"
#include "platform_project.hpp"
#include "player_view.hpp"
#include "project_export.hpp"

namespace {

int expedition_slot(std::wstring_view argument, std::wstring_view prefix) {
  if (argument.size() != prefix.size() + 1) return 0;
  const auto digit = argument.back();
  return digit >= L'1' && digit <= L'3' ? digit - L'0' : 0;
}

std::wstring save_name(int slot) {
  if (slot == 1) return L"expedition.save.json";
  return L"expedition.slot-" + std::to_wstring(slot) + L".json";
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR arguments, int) {
  using namespace wds::platform;
  const auto option = std::wstring_view(arguments);
  const bool check_only = option == L"--check";
  const bool smoke = option == L"--expedition-smoke";
  const bool check_save = option.starts_with(L"--check-save=");
  const bool choose_save = option.starts_with(L"--slot=");
  const auto slot = check_save    ? expedition_slot(option, L"--check-save=")
                    : choose_save ? expedition_slot(option, L"--slot=")
                                  : 1;
  const auto failure = [check_only, smoke, check_save](std::string message) {
    if (!check_only && !smoke && !check_save) {
      const int length = MultiByteToWideChar(CP_UTF8, 0, message.c_str(), -1, nullptr, 0);
      std::wstring text(static_cast<std::size_t>(length), L'\0');
      MultiByteToWideChar(CP_UTF8, 0, message.c_str(), -1, text.data(), length);
      MessageBoxW(nullptr, text.c_str(), L"WDS Game", MB_ICONERROR | MB_OK);
    }
    return 1;
  };
  if (slot == 0) {
    failure("Save slots are numbered from 1 to 3.");
    return 2;
  }
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
  if ((check_save || choose_save) && !journey.expedition)
    return failure("This game has no expedition saves.");
  if (journey.expedition) {
    const auto save = folder / save_name(slot);
    if (check_save) {
      std::error_code error;
      if (!std::filesystem::exists(save, error)) return error ? 1 : 2;
      return wds::expedition::restore(*journey.expedition, save) ? 0 : 1;
    }
    const auto guard = slot == 1
                           ? folder / L".expedition.lock"
                           : folder / (L".expedition.slot-" + std::to_wstring(slot) + L".lock");
    const HANDLE lock = CreateFileW(guard.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                    OPEN_ALWAYS, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (lock == INVALID_HANDLE_VALUE) return failure("The expedition save is already in use.");
    const bool played = wds::player::play_expedition(*journey.expedition, journey.icons, save);
    CloseHandle(lock);
    return played ? 0 : failure("Could not open the expedition save or graphics.");
  }
  const bool played = scene.voxel_scene ? wds::player::play_voxel(*scene.voxel_scene, nullptr)
                                        : wds::player::play_scene(*scene.session, nullptr);
  if (!played) return failure("Could not start graphics. The game requires OpenGL 3.3.");
  return 0;
}
