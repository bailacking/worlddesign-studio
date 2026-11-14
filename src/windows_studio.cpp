#include "platform_project.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>
#include <string_view>

namespace {

enum ControlId { folder_input = 101, project_id, open_button, create_button, resources, status };

std::wstring text(HWND control) {
  const int length = GetWindowTextLengthW(control);
  std::wstring result(static_cast<std::size_t>(length) + 1, L'\0');
  result.resize(static_cast<std::size_t>(GetWindowTextW(control, result.data(), length + 1)));
  return result;
}

std::wstring widen(std::string_view ascii) { return {ascii.begin(), ascii.end()}; }

std::string ascii_id(std::wstring_view value) {
  std::string result;
  for (const auto ch : value) {
    if (ch > 127) return {};
    result.push_back(static_cast<char>(ch));
  }
  return result;
}

HWND add_control(HWND window, const wchar_t* kind, const wchar_t* label, DWORD flags, int x, int y,
                 int width, int height, int id = 0) {
  const auto control = CreateWindowExW(
      0, kind, label, WS_CHILD | WS_VISIBLE | flags, x, y, width, height, window,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
  if (control) {
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),
                 TRUE);
  }
  return control;
}

void present(HWND window, const wds::platform::ProjectResult& result) {
  const auto list = GetDlgItem(window, resources);
  SendMessageW(list, LB_RESETCONTENT, 0, 0);
  if (!result.project) {
    const auto message =
        L"Cannot open project: " + widen(result.code) + L" at " + widen(result.location);
    SetDlgItemTextW(window, status, message.c_str());
    return;
  }
  const auto& project = *result.project;
  const auto summary = widen(project.id) + L"  |  Revision " + std::to_wstring(project.revision) +
                       L"  |  Entry scene: " + widen(project.entry_scene);
  SetDlgItemTextW(window, status, summary.c_str());
  for (const auto& resource : project.resources) {
    const auto row =
        widen(resource.id) + L"  [" + widen(resource.kind) + L"]  " + widen(resource.path);
    SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(row.c_str()));
  }
}

LRESULT CALLBACK handle(HWND window, UINT message, WPARAM param, LPARAM data) {
  switch (message) {
    case WM_CREATE:
      add_control(window, L"STATIC", L"Project folder", 0, 16, 18, 100, 22);
      add_control(window, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 120, 16, 540, 24,
                  folder_input);
      add_control(window, L"STATIC", L"New project ID", 0, 16, 53, 100, 22);
      add_control(window, L"EDIT", L"my-game", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 120, 50,
                  252, 24, project_id);
      add_control(window, L"BUTTON", L"Open", WS_TABSTOP, 386, 48, 110, 28, open_button);
      add_control(window, L"BUTTON", L"Create", WS_TABSTOP, 510, 48, 150, 28, create_button);
      add_control(window, L"STATIC", L"Open a WDS project or create one in a new folder.", 0, 16,
                  91, 644, 24, status);
      add_control(window, L"STATIC", L"Project resources", 0, 16, 124, 644, 22);
      add_control(window, L"LISTBOX", L"",
                  WS_BORDER | WS_VSCROLL | WS_TABSTOP | LBS_NOINTEGRALHEIGHT, 16, 150, 644, 220,
                  resources);
      return GetDlgItem(window, resources) && GetDlgItem(window, folder_input) ? 0 : -1;
    case WM_COMMAND: {
      const auto action = LOWORD(param);
      if (HIWORD(param) != BN_CLICKED || (action != open_button && action != create_button)) break;
      const auto folder = text(GetDlgItem(window, folder_input));
      if (folder.empty()) {
        present(window, {std::nullopt, "missing_folder", "."});
      } else if (action == create_button) {
        present(window, wds::platform::create_project(
                            folder, ascii_id(text(GetDlgItem(window, project_id)))));
      } else {
        present(window, wds::platform::open_project(folder));
      }
      return 0;
    }
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(window, message, param, data);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
  WNDCLASSW type{};
  type.hInstance = instance;
  type.lpszClassName = L"WdsPlatformStudio";
  type.lpfnWndProc = handle;
  type.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  type.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
  if (!RegisterClassW(&type)) return 2;
  const auto window =
      CreateWindowExW(0, type.lpszClassName, L"WDS Studio | Project Workspace",
                      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT,
                      CW_USEDEFAULT, 700, 432, nullptr, nullptr, instance, nullptr);
  if (!window) return 2;
  ShowWindow(window, show);
  SetFocus(GetDlgItem(window, folder_input));
  MSG message{};
  BOOL result;
  while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
    if (!IsDialogMessageW(window, &message)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
  }
  return result == -1 ? 2 : static_cast<int>(message.wParam);
}
