#include "voxel_world.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

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
};

void load(Sandbox& app) {
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
  app.save_path = fs::path(local) / L"WDS" / L"sandbox-v1.dat";
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
  if (GetForegroundWindow() != window) {
    app.jump_was_down = down(VK_SPACE);
    return;
  }
  auto& player = app.player;
  player.yaw = std::remainder(player.yaw + axis(VK_RIGHT, VK_LEFT) * seconds * 1.8, 6.2831853);
  player.pitch = std::clamp(player.pitch + axis(VK_UP, VK_DOWN) * seconds * 1.4, -1.3, 1.3);
  const bool jump_down = down(VK_SPACE);
  player.step(app.world, axis('W', 'S'), axis('D', 'A'), jump_down && !app.jump_was_down, seconds);
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
  SelectObject(dc, GetStockObject(WHITE_PEN));
  MoveToEx(dc, cx - 5, cy, nullptr);
  LineTo(dc, cx + 6, cy);
  MoveToEx(dc, cx, cy - 5, nullptr);
  LineTo(dc, cx, cy + 6);
  constexpr wchar_t controls[] =
      L"WASD Move   Arrows Look   Space Jump   LMB Break   RMB Place   F5 Save";
  SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, RGB(20, 33, 45));
  TextOutW(dc, 16, 16, controls, static_cast<int>(std::size(controls) - 1));
  TextOutW(dc, 16, 36, app.status.c_str(), static_cast<int>(app.status.size()));
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
    case WM_KEYDOWN:
      if (param == VK_ESCAPE) DestroyWindow(window);
      if (param == VK_F5 && (data & (1L << 30)) == 0) {
        save(*app);
        InvalidateRect(window, nullptr, FALSE);
      }
      return 0;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
      app->status = app->world.edit(app->player.feet, app->player.eye(), app->player.view(),
                                    message == WM_RBUTTONDOWN)
                        ? L"World changed. F5 to save."
                        : L"Cannot edit that block";
      InvalidateRect(window, nullptr, FALSE);
      return 0;
    case WM_DESTROY:
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

  Sandbox app;
  load(app);
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
