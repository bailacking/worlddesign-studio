#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "expedition.hpp"
#include "platform_project.hpp"
#include "player_view.hpp"
#include "project_export.hpp"

namespace {

namespace fs = std::filesystem;
using wds::expedition::Expedition;

int expedition_slot(std::wstring_view argument, std::wstring_view prefix) {
  if (argument.size() != prefix.size() + 1) return 0;
  const auto digit = argument.back();
  return digit >= L'1' && digit <= L'3' ? digit - L'0' : 0;
}

std::wstring save_name(int slot) {
  if (slot == 1) return L"expedition.save.json";
  return L"expedition.slot-" + std::to_wstring(slot) + L".json";
}

class SaveSlots {
 public:
  SaveSlots(const fs::path& folder, const Expedition& initial)
      : folder_(folder), initial_(initial) {}
  SaveSlots(const SaveSlots&) = delete;
  SaveSlots& operator=(const SaveSlots&) = delete;

  ~SaveSlots() {
    if (lock_ != INVALID_HANDLE_VALUE) CloseHandle(lock_);
  }

  std::string select(int slot, Expedition& run, fs::path& save) {
    if (slot == active_) return {};
    const auto guard = slot == 1
                           ? folder_ / L".expedition.lock"
                           : folder_ / (L".expedition.slot-" + std::to_wstring(slot) + L".lock");
    auto next = folder_ / save_name(slot);
    auto candidate = initial_;
    const auto pending = CreateFileW(guard.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                     OPEN_ALWAYS, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (pending == INVALID_HANDLE_VALUE) return "The selected save slot is unavailable.";
    std::error_code error;
    const bool exists = fs::exists(next, error);
    if (error || (exists && !wds::expedition::restore(candidate, next))) {
      CloseHandle(pending);
      return "Could not read the selected save slot.";
    }
    if (lock_ != INVALID_HANDLE_VALUE) CloseHandle(lock_);
    lock_ = pending;
    active_ = slot;
    run = std::move(candidate);
    save = std::move(next);
    return {};
  }

 private:
  fs::path folder_;
  Expedition initial_;
  int active_ = 0;
  HANDLE lock_ = INVALID_HANDLE_VALUE;
};

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR arguments, int) {
  using namespace wds::platform;
  const auto option = std::wstring_view(arguments);
  const bool check_only = option == L"--check";
  const bool smoke = option == L"--expedition-smoke";
  const bool check_save = option.starts_with(L"--check-save=");
  const bool check_switch = option == L"--check-slot-switch";
  const bool choose_save = option.starts_with(L"--slot=");
  const auto slot = check_save    ? expedition_slot(option, L"--check-save=")
                    : choose_save ? expedition_slot(option, L"--slot=")
                                  : 1;
  const auto failure = [check_only, smoke, check_save, check_switch](std::string message) {
    if (!check_only && !smoke && !check_save && !check_switch) {
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
  if ((check_save || choose_save || check_switch) && !journey.expedition)
    return failure("This game has no expedition saves.");
  if (journey.expedition) {
    auto save = folder / save_name(slot);
    if (check_save) {
      std::error_code error;
      if (!std::filesystem::exists(save, error)) return error ? 1 : 2;
      return wds::expedition::restore(*journey.expedition, save) ? 0 : 1;
    }
    auto& run = *journey.expedition;
    SaveSlots slots(folder, run);
    if (auto error = slots.select(slot, run, save); !error.empty())
      return failure(std::move(error));
    if (check_switch) {
      for (const int target : {2, 3, 1}) {
        if (auto error = slots.select(target, run, save); !error.empty())
          return failure(std::move(error));
      }
      return 0;
    }
    const auto select = [&](int target, Expedition& expedition, fs::path& destination) {
      return slots.select(target, expedition, destination);
    };
    const bool played =
        wds::player::play_expedition(run, journey.icons, std::move(save), false, {slot, select});
    return played ? 0 : failure("Could not open the expedition graphics.");
  }
  const bool played = scene.voxel_scene ? wds::player::play_voxel(*scene.voxel_scene, nullptr)
                                        : wds::player::play_scene(*scene.session, nullptr);
  if (!played) return failure("Could not start graphics. The game requires OpenGL 3.3.");
  return 0;
}
