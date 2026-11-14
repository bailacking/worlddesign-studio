#include "sandbox_rules.hpp"
#include "voxel_world.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// clang-format off
#include <windows.h>
#include <shellapi.h>
// clang-format on

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

using wds::voxel::Player;
using wds::voxel::Vec3;
using wds::voxel::World;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
constexpr int image_width = 240;
constexpr int image_height = 144;
constexpr UINT_PTR frame_timer = 1;

struct Sandbox {
  World world;
  Player player;
  std::array<std::uint32_t, image_width * image_height> pixels{};
  Clock::time_point previous = Clock::now();
  bool jump_was_down = false;
  fs::path save_path;
  std::wstring status;
  bool save_blocked = false;
  int jump_speed = wds::voxel::default_jump;
  bool invalid_rules = false;
  bool mouse_look = false;
  int pending_jump = 0;
  fs::path rules_path;
  Clock::time_point edit_feedback{};
  bool project_mode = false;
};

void load(Sandbox& app, const fs::path& project) {
  if (!project.empty()) {
    app.project_mode = true;
    const auto jump = wds::voxel::read_project_jump(project);
    if (!jump) {
      app.invalid_rules = true;
      return;
    }
    app.jump_speed = *jump;
    app.rules_path = project / wds::voxel::project_rules;
    app.save_path = project / wds::voxel::project_save;
  } else {
    const auto size = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (size == 0) {
      app.status = L"Save folder unavailable";
      return;
    }
    std::wstring local(size, L'\0');
    const auto length = GetEnvironmentVariableW(L"LOCALAPPDATA", local.data(), size);
    if (length == 0 || length >= size) {
      app.status = L"Save folder unavailable";
      return;
    }
    local.resize(length);
    app.save_path = fs::path(local) / L"WDS" / wds::voxel::project_save;
    app.rules_path = app.save_path.parent_path() / wds::voxel::project_rules;
    const auto jump = wds::voxel::read_jump(app.rules_path);
    if (jump) app.jump_speed = *jump;
    app.invalid_rules = !jump;
  }
  std::error_code error;
  const auto existing = fs::symlink_status(app.save_path, error);
  if (existing.type() == fs::file_type::not_found) {
    app.status = L"New world";
    return;
  }
  if (!error && fs::is_regular_file(existing)) {
    std::ifstream in(app.save_path, std::ios::binary);
    if (app.world.read(in)) {
      app.status = L"World loaded";
      return;
    }
  }
  app.save_blocked = true;
  app.status = L"Save unreadable. Original file kept.";
}

void mouse_mode(HWND window, Sandbox& app, bool enabled) {
  if (enabled == app.mouse_look) return;
  if (enabled) {
    RAWINPUTDEVICE input{1, 2, 0, window};
    if (!RegisterRawInputDevices(&input, 1, sizeof(input))) return;
    RECT area{};
    GetClientRect(window, &area);
    MapWindowPoints(window, nullptr, reinterpret_cast<POINT*>(&area), 2);
    if (!ClipCursor(&area)) return;
  } else {
    ClipCursor(nullptr);
  }
  app.mouse_look = enabled;
  ShowCursor(!enabled);
  InvalidateRect(window, nullptr, FALSE);
}

void save(Sandbox& app) {
  if (app.save_path.empty() || app.save_blocked) {
    app.status = L"Save unavailable. Original file kept.";
    return;
  }
  std::error_code error;
  const auto folder = app.save_path.parent_path();
  fs::create_directories(folder, error);
  wchar_t staging[MAX_PATH]{};
  if (error || GetTempFileNameW(folder.c_str(), L"wds", 0, staging) == 0) {
    app.status = L"Could not prepare save";
    return;
  }
  std::ofstream out(fs::path(staging), std::ios::binary);
  app.world.write(out);
  out.close();
  const bool written =
      out.good() && MoveFileExW(staging, app.save_path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
  if (!written) fs::remove(staging, error);
  app.status = written ? L"World saved" : L"Save failed";
}

bool down(int key) { return (GetAsyncKeyState(key) & 0x8000) != 0; }

int axis(int positive, int negative) {
  return static_cast<int>(down(positive)) - static_cast<int>(down(negative));
}

void tick(HWND window, Sandbox& app) {
  const auto now = Clock::now();
  const auto seconds =
      std::clamp(std::chrono::duration<double>(now - app.previous).count(), 0.0, 0.025);
  app.previous = now;
  if (GetForegroundWindow() != window || app.pending_jump != 0) {
    app.jump_was_down = down(VK_SPACE);
    return;
  }
  auto& player = app.player;
  player.yaw = std::remainder(player.yaw + axis(VK_RIGHT, VK_LEFT) * seconds * 1.8, 6.2831853);
  player.pitch = std::clamp(player.pitch + axis(VK_UP, VK_DOWN) * seconds * 1.4, -1.3, 1.3);
  const bool jump_down = down(VK_SPACE);
  player.step(app.world, axis('W', 'S'), axis('D', 'A'), jump_down && !app.jump_was_down, seconds,
              app.jump_speed);
  app.jump_was_down = jump_down;
  InvalidateRect(window, nullptr, FALSE);
}

void paint(HWND window, Sandbox& app) {
  PAINTSTRUCT paint_data{};
  const HDC dc = BeginPaint(window, &paint_data);
  RECT rect{};
  GetClientRect(window, &rect);
  const int width = rect.right - rect.left;
  const int height = rect.bottom - rect.top;
  const auto forward = app.player.view();
  const auto eye = app.player.eye();
  const Vec3 right{std::cos(app.player.yaw), 0, std::sin(app.player.yaw)};
  const Vec3 up{-std::sin(app.player.yaw) * std::sin(app.player.pitch), std::cos(app.player.pitch),
                std::cos(app.player.yaw) * std::sin(app.player.pitch)};

  for (int y = 0; y < image_height; ++y) {
    const double vertical = (1 - 2 * (y + 0.5) / image_height) * 0.58;
    for (int x = 0; x < image_width; ++x) {
      const double horizontal = (2 * (x + 0.5) / image_width - 1) * 0.97;
      app.pixels[static_cast<std::size_t>(y * image_width + x)] = wds::voxel::pixel(
          app.world, eye,
          {forward.x + horizontal * right.x + vertical * up.x, forward.y + vertical * up.y,
           forward.z + horizontal * right.z + vertical * up.z});
    }
  }
  BITMAPINFO bitmap{};
  bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bitmap.bmiHeader.biWidth = image_width;
  bitmap.bmiHeader.biHeight = -image_height;
  bitmap.bmiHeader.biPlanes = 1;
  bitmap.bmiHeader.biBitCount = 32;
  bitmap.bmiHeader.biCompression = BI_RGB;
  StretchDIBits(dc, 0, 0, width, height, 0, 0, image_width, image_height, app.pixels.data(),
                &bitmap, DIB_RGB_COLORS, SRCCOPY);

  const int cx = width / 2;
  const int cy = height / 2;
  const auto target = app.world.cast(eye, forward, 7);
  const auto feedback = Clock::now() < app.edit_feedback;
  const auto pen = CreatePen(PS_SOLID, 2,
                             feedback                                 ? RGB(115, 255, 153)
                             : target.block == wds::voxel::Block::air ? RGB(235, 239, 245)
                                                                      : RGB(255, 216, 80));
  const auto previous_pen = pen ? SelectObject(dc, pen) : nullptr;
  MoveToEx(dc, cx - 5, cy, nullptr);
  LineTo(dc, cx + 6, cy);
  MoveToEx(dc, cx, cy - 5, nullptr);
  LineTo(dc, cx, cy + 6);
  if (previous_pen) SelectObject(dc, previous_pen);
  if (pen) DeleteObject(pen);
  constexpr wchar_t controls[] =
      L"WASD Move   Mouse / arrows Look   Space Jump   LMB Break   RMB Place   F5 Save";
  SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, RGB(20, 33, 45));
  TextOutW(dc, 16, 16, controls, static_cast<int>(std::size(controls) - 1));
  TextOutW(dc, 16, 36, app.status.c_str(), static_cast<int>(app.status.size()));
  const auto rule = L"Jump speed: " + std::to_wstring(app.jump_speed) +
                    (app.invalid_rules ? L" (invalid rules file, using default)" : L"");
  TextOutW(dc, 16, 56, rule.c_str(), static_cast<int>(rule.size()));
  const auto edit =
      app.pending_jump != 0
          ? L"Jump speed: " + std::to_wstring(app.jump_speed) + L" -> " +
                std::to_wstring(app.pending_jump) + L"   +/- Adjust   Enter Apply   Esc Cancel"
          : std::wstring(app.mouse_look ? L"Esc Release mouse" : L"Click to capture mouse") +
                L"   F6 Edit jump rule";
  TextOutW(dc, 16, 76, edit.c_str(), static_cast<int>(edit.size()));
  EndPaint(window, &paint_data);
}

LRESULT CALLBACK handle(HWND window, UINT message, WPARAM param, LPARAM data) {
  if (message == WM_NCCREATE) {
    const auto* create = reinterpret_cast<CREATESTRUCTW*>(data);
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
  }
  auto* app = reinterpret_cast<Sandbox*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  switch (message) {
    case WM_CREATE:
      return SetTimer(window, frame_timer, 16, nullptr) != 0 ? 0 : -1;
    case WM_TIMER:
      if (param == frame_timer) tick(window, *app);
      return 0;
    case WM_PAINT:
      paint(window, *app);
      return 0;
    case WM_KILLFOCUS:
      mouse_mode(window, *app, false);
      return 0;
    case WM_INPUT: {
      RAWINPUT input{};
      UINT size = sizeof(input);
      if (app->mouse_look &&
          GetRawInputData(reinterpret_cast<HRAWINPUT>(data), RID_INPUT, &input, &size,
                          sizeof(RAWINPUTHEADER)) == sizeof(input) &&
          input.header.dwType == RIM_TYPEMOUSE &&
          (input.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) == 0) {
        app->player.yaw =
            std::remainder(app->player.yaw + input.data.mouse.lLastX * 0.0025, 6.2831853);
        app->player.pitch =
            std::clamp(app->player.pitch - input.data.mouse.lLastY * 0.0025, -1.3, 1.3);
      }
      return DefWindowProcW(window, message, param, data);
    }
    case WM_KEYDOWN:
      if (param == VK_ESCAPE) {
        if (app->pending_jump != 0)
          app->pending_jump = 0;
        else if (app->mouse_look)
          mouse_mode(window, *app, false);
        else
          DestroyWindow(window);
      }
      if (param == VK_F6 && app->pending_jump == 0 && !app->rules_path.empty() &&
          !app->invalid_rules) {
        mouse_mode(window, *app, false);
        app->pending_jump = app->jump_speed;
      }
      if (app->pending_jump != 0) {
        if (param == VK_ADD || param == VK_OEM_PLUS)
          app->pending_jump = std::min(9, app->pending_jump + 1);
        if (param == VK_SUBTRACT || param == VK_OEM_MINUS)
          app->pending_jump = std::max(3, app->pending_jump - 1);
        if (param == VK_RETURN) {
          const auto current = app->project_mode
                                   ? wds::voxel::read_project_jump(app->rules_path.parent_path())
                                   : wds::voxel::read_jump(app->rules_path);
          if (current != app->jump_speed) {
            app->status = L"Rules changed on disk. Reopen the project.";
          } else if (app->pending_jump == app->jump_speed ||
                     wds::voxel::write_jump(app->rules_path, app->pending_jump)) {
            app->jump_speed = app->pending_jump;
            app->status = L"Jump rule saved";
          } else {
            app->status = L"Could not save jump rule";
          }
          app->pending_jump = 0;
        }
      }
      if (param == VK_F5 && (data & (1L << 30)) == 0) {
        save(*app);
        InvalidateRect(window, nullptr, FALSE);
      }
      InvalidateRect(window, nullptr, FALSE);
      return 0;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
      if (app->pending_jump != 0) return 0;
      if (!app->mouse_look) {
        mouse_mode(window, *app, true);
        return 0;
      }
      if (app->world.edit(app->player.feet, app->player.eye(), app->player.view(),
                          message == WM_RBUTTONDOWN)) {
        app->status = message == WM_RBUTTONDOWN ? L"Block placed" : L"Block broken";
        app->edit_feedback = Clock::now() + std::chrono::milliseconds(160);
      } else {
        app->status = L"Cannot edit that block";
      }
      InvalidateRect(window, nullptr, FALSE);
      return 0;
    case WM_DESTROY:
      mouse_mode(window, *app, false);
      KillTimer(window, frame_timer);
      PostQuitMessage(0);
      return 0;
    default:
      return DefWindowProcW(window, message, param, data);
  }
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_window) {
  WNDCLASSW type{};
  type.lpfnWndProc = handle;
  type.hInstance = instance;
  type.lpszClassName = L"WdsVoxelSandbox";
  type.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  if (!RegisterClassW(&type)) return 2;

  int argc = 0;
  auto* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (!argv) return 2;
  const bool project_mode = argc == 3 && std::wstring_view(argv[1]) == L"--project";
  const auto project = project_mode ? fs::path(argv[2]) : fs::path{};
  LocalFree(argv);
  if (argc != 1 && (!project_mode || project.empty())) {
    MessageBoxW(nullptr, L"Usage: wds_sandbox --project DIRECTORY", L"WDS", MB_OK | MB_ICONERROR);
    return 2;
  }
  Sandbox app;
  load(app, project);
  if (project_mode && app.invalid_rules) {
    MessageBoxW(nullptr, L"Cannot open the project rules file.", L"WDS", MB_OK | MB_ICONERROR);
    return 1;
  }
  const auto window =
      CreateWindowExW(0, type.lpszClassName, L"WDS | Block World",
                      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT,
                      CW_USEDEFAULT, 800, 510, nullptr, nullptr, instance, &app);
  if (!window) return 2;
  ShowWindow(window, show_window);

  MSG message{};
  BOOL status{};
  while ((status = GetMessageW(&message, nullptr, 0, 0)) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  return status == -1 ? 2 : static_cast<int>(message.wParam);
}
