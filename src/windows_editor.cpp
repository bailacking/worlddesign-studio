#include "workspace.hpp"

#define WIN32_LEAN_AND_MEAN
// clang-format off
#include <windows.h>
#include <shellapi.h>
// clang-format on

#include <memory>
#include <string>
#include <string_view>

namespace {

constexpr int open_button = 101;
constexpr int rename_button = 102;

struct Editor {
  HWND root{};
  HWND id{};
  HWND filename{};
  HWND status{};
  std::unique_ptr<wds::Workspace> workspace;
  std::uint64_t revision{};
  wds::RenameStatus last_status = wds::RenameStatus::recovery_required;
};

std::wstring value(HWND control) {
  const int length = GetWindowTextLengthW(control);
  std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
  text.resize(static_cast<std::size_t>(GetWindowTextW(control, text.data(), length + 1)));
  return text;
}

std::string ascii(HWND control) {
  std::string text;
  for (const wchar_t ch : value(control)) {
    if (ch > 127) return {};
    text.push_back(static_cast<char>(ch));
  }
  return text;
}

void show(Editor& editor, const wds::RenameResult& result) {
  editor.last_status = result.status;
  if (result.status == wds::RenameStatus::ok ||
      result.status == wds::RenameStatus::revision_conflict) {
    editor.revision = result.revision;
  }
  const auto name = wds::status_name(result.status);
  std::wstring status(name.begin(), name.end());
  status += L" | revision " + std::to_wstring(result.revision);
  if (result.changed) {
    status += L" | " + std::wstring(result.new_path.begin(), result.new_path.end());
  }
  if (!result.diagnostic.empty()) {
    status += L"\r\n" + std::wstring(result.diagnostic.begin(), result.diagnostic.end());
  }
  SetWindowTextW(editor.status, status.c_str());
}

HWND control(HWND window, const wchar_t* kind, const wchar_t* text, DWORD flags, int x, int y,
             int width, int height, int id = 0) {
  const auto child = CreateWindowExW(
      0, kind, text, WS_CHILD | WS_VISIBLE | flags, x, y, width, height, window,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
  SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
  return child;
}

LRESULT CALLBACK handle(HWND window, UINT message, WPARAM param, LPARAM data) {
  if (message == WM_NCCREATE) {
    const auto* create = reinterpret_cast<CREATESTRUCTW*>(data);
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
  }
  auto* editor = reinterpret_cast<Editor*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_CREATE) {
    control(window, L"STATIC", L"Project folder", 0, 16, 16, 100, 24);
    editor->root = control(window, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 116, 14, 390, 24);
    control(window, L"BUTTON", L"Open / Recover", 0, 116, 48, 142, 28, open_button);
    control(window, L"STATIC", L"Resource ID", 0, 16, 95, 100, 24);
    editor->id = control(window, L"EDIT", L"B", WS_BORDER | ES_AUTOHSCROLL, 116, 92, 100, 24);
    control(window, L"STATIC", L"New name", 0, 16, 130, 100, 24);
    editor->filename =
        control(window, L"EDIT", L"stone.txt", WS_BORDER | ES_AUTOHSCROLL, 116, 127, 260, 24);
    control(window, L"BUTTON", L"Rename", 0, 390, 125, 116, 28, rename_button);
    editor->status =
        control(window, L"STATIC", L"Open an existing M0 project to begin.", 0, 16, 172, 492, 64);
    if (!editor->root || !editor->id || !editor->filename || !editor->status) return -1;
    return 0;
  }
  if (message == WM_COMMAND && HIWORD(param) == EN_CHANGE &&
      reinterpret_cast<HWND>(data) == editor->root) {
    editor->workspace.reset();
    SetWindowTextW(editor->status, L"Open the project folder to begin.");
    return 0;
  }
  if (message == WM_COMMAND && HIWORD(param) == BN_CLICKED) {
    try {
      if (LOWORD(param) == open_button) {
        editor->workspace.reset();
        const auto root = value(editor->root);
        if (root.empty()) {
          SetWindowTextW(editor->status, L"Enter a project folder.");
          editor->last_status = wds::RenameStatus::invalid_argument;
        } else {
          auto next = std::make_unique<wds::Workspace>(root);
          const auto result = next->open();
          show(*editor, result);
          if (result.status == wds::RenameStatus::ok) editor->workspace = std::move(next);
        }
      } else if (LOWORD(param) == rename_button) {
        if (editor->workspace) {
          show(*editor, editor->workspace->rename(
                            {ascii(editor->id), ascii(editor->filename), editor->revision}));
        } else {
          SetWindowTextW(editor->status, L"Open the project first.");
          editor->last_status = wds::RenameStatus::invalid_argument;
        }
      }
    } catch (const std::exception&) {
      editor->last_status = wds::RenameStatus::recovery_required;
      SetWindowTextW(editor->status, L"Could not process the project. Reopen and inspect it.");
    }
    return 0;
  }
  if (message == WM_DESTROY) {
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProcW(window, message, param, data);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_window) {
  WNDCLASSW type{};
  type.lpfnWndProc = handle;
  type.hInstance = instance;
  type.lpszClassName = L"WdsM0Editor";
  type.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  type.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
  if (!RegisterClassW(&type)) return 2;

  Editor editor;
  const auto window =
      CreateWindowExW(0, type.lpszClassName, L"WDS M0 Project",
                      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT,
                      CW_USEDEFAULT, 544, 282, nullptr, nullptr, instance, &editor);
  if (!window) return 2;

  int argc = 0;
  auto* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (!argv) return 2;
  const bool verify = argc == 3 && std::wstring_view(argv[1]) == L"--verify";
  if (verify) {
    SetWindowTextW(editor.root, argv[2]);
    SendMessageW(window, WM_COMMAND, open_button, 0);
    if (editor.last_status == wds::RenameStatus::ok) {
      SendMessageW(window, WM_COMMAND, rename_button, 0);
    }
    LocalFree(argv);
    DestroyWindow(window);
    return editor.last_status == wds::RenameStatus::ok && editor.revision == 1 ? 0 : 1;
  }
  if (argc == 2) SetWindowTextW(editor.root, argv[1]);
  LocalFree(argv);
  ShowWindow(window, show_window);
  MSG message{};
  BOOL status{};
  while ((status = GetMessageW(&message, nullptr, 0, 0)) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  return status == -1 ? 2 : static_cast<int>(message.wParam);
}
