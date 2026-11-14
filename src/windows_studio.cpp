#include "platform_project.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <charconv>
#include <string>
#include <string_view>
#include <utility>

namespace {

enum ControlId {
  folder_input = 101,
  project_id,
  open_button,
  create_button,
  resources,
  status,
  resource_id,
  field_name,
  field_value,
  preview_button,
  apply_button,
  recover_button,
  edit_status,
  play_button,
  stop_button,
  play_status
};

constexpr UINT_PTR play_timer = 1;
constexpr RECT viewport{16, 465, 660, 601};

struct Studio {
  std::optional<std::uint64_t> revision;
  std::optional<wds::platform::FieldEdit> preview;
  std::optional<wds::platform::SceneSession> session;
};

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

void stop_play(HWND window, Studio& studio) {
  KillTimer(window, play_timer);
  studio.session.reset();
  EnableWindow(GetDlgItem(window, play_button), studio.revision.has_value());
  EnableWindow(GetDlgItem(window, stop_button), FALSE);
  SetDlgItemTextW(window, play_status, L"");
  InvalidateRect(window, &viewport, FALSE);
}

void set_project(HWND window, Studio& studio, const wds::platform::ProjectResult& result) {
  studio.revision = result.project ? std::optional(result.project->revision) : std::nullopt;
  stop_play(window, studio);
  studio.preview.reset();
  EnableWindow(GetDlgItem(window, apply_button), FALSE);
  present(window, result);
}

void show_play(HWND window, const wds::platform::SceneSnapshot& scene) {
  const auto summary = widen(scene.scene_id) + L"  |  Revision " + std::to_wstring(scene.revision) +
                       L"  |  Tick " + std::to_wstring(scene.ticks) + L"  |  Entities " +
                       std::to_wstring(scene.entities.size());
  SetDlgItemTextW(window, play_status, summary.c_str());
  InvalidateRect(window, &viewport, FALSE);
}

void play(HWND window, Studio& studio) {
  if (!studio.revision || studio.session) return;
  auto result =
      wds::platform::start_scene(text(GetDlgItem(window, folder_input)), *studio.revision);
  if (!result.session) {
    auto message = widen(result.code) + L" at " + widen(result.location);
    if (!result.resource_path.empty()) message += L" in " + widen(result.resource_path);
    if (result.code == "revision_conflict") message += L". Reopen the project.";
    SetDlgItemTextW(window, play_status, message.c_str());
    return;
  }
  if (SetTimer(window, play_timer, 33, nullptr) == 0) {
    SetDlgItemTextW(window, play_status, L"Could not start the preview timer.");
    return;
  }
  studio.session = std::move(result.session);
  EnableWindow(GetDlgItem(window, play_button), FALSE);
  EnableWindow(GetDlgItem(window, stop_button), TRUE);
  SetFocus(GetDlgItem(window, stop_button));
  show_play(window, studio.session->snapshot());
}

void paint_play(HWND window, const Studio& studio) {
  PAINTSTRUCT paint{};
  const auto canvas = BeginPaint(window, &paint);
  FillRect(canvas, &viewport, GetSysColorBrush(COLOR_BTNFACE));
  FrameRect(canvas, &viewport, GetSysColorBrush(COLOR_BTNSHADOW));
  if (studio.session) {
    constexpr LONG radius = 4;
    for (const auto& entity : studio.session->snapshot().entities) {
      const auto x = (viewport.left + viewport.right) / 2 + entity.x;
      const auto y = (viewport.top + viewport.bottom) / 2 - entity.y;
      if (x - radius <= viewport.left || x + radius >= viewport.right ||
          y - radius <= viewport.top || y + radius >= viewport.bottom)
        continue;
      const RECT marker{static_cast<LONG>(x) - radius, static_cast<LONG>(y) - radius,
                        static_cast<LONG>(x) + radius, static_cast<LONG>(y) + radius};
      FillRect(canvas, &marker, GetSysColorBrush(COLOR_HIGHLIGHT));
    }
  }
  EndPaint(window, &paint);
}

void edit(HWND window, Studio& studio, bool apply) {
  if (!studio.revision || (apply && !studio.preview)) return;
  const auto root = text(GetDlgItem(window, folder_input));
  const auto value = ascii_id(text(GetDlgItem(window, field_value)));
  wds::platform::FieldEdit request{ascii_id(text(GetDlgItem(window, resource_id))),
                                   ascii_id(text(GetDlgItem(window, field_name))), *studio.revision,
                                   0};
  const auto end = value.data() + value.size();
  const auto parsed = std::from_chars(value.data(), end, request.value);
  if (parsed.ec != std::errc{} || parsed.ptr != end) {
    SetDlgItemTextW(window, edit_status, L"Enter an integer value.");
    return;
  }
  if (apply && request != *studio.preview) return;
  studio.preview.reset();
  EnableWindow(GetDlgItem(window, apply_button), FALSE);
  const auto result = wds::platform::edit_field(root, request, apply);
  if (result.code != "ok") {
    auto message = widen(result.code) + L" at " + widen(result.location);
    if (result.code == "revision_conflict") message += L". Reopen the project.";
    SetDlgItemTextW(window, edit_status, message.c_str());
    return;
  }
  auto message = std::to_wstring(*result.old_value) + L" -> " + std::to_wstring(*result.new_value);
  if (apply) {
    set_project(window, studio, wds::platform::open_project(root));
    message = L"Saved: " + message;
  } else if (result.changed) {
    studio.preview = request;
    EnableWindow(GetDlgItem(window, apply_button), TRUE);
    message = L"Preview: " + message;
  } else {
    message = L"No change: " + message;
  }
  SetDlgItemTextW(window, edit_status, message.c_str());
}

LRESULT CALLBACK handle(HWND window, UINT message, WPARAM param, LPARAM data) {
  if (message == WM_NCCREATE) {
    const auto* create = reinterpret_cast<const CREATESTRUCTW*>(data);
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
  }
  auto* studio = reinterpret_cast<Studio*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  switch (message) {
    case WM_CREATE:
      add_control(window, L"STATIC", L"Project folder", 0, 16, 18, 100, 22);
      add_control(window, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 120, 16, 540, 24,
                  folder_input);
      add_control(window, L"STATIC", L"New project ID", 0, 16, 53, 100, 22);
      add_control(window, L"EDIT", L"my-game", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 120, 50,
                  252, 24, project_id);
      add_control(window, L"BUTTON", L"Open", WS_TABSTOP, 386, 48, 110, 28, open_button);
      add_control(window, L"BUTTON", L"Create", WS_TABSTOP, 510, 48, 76, 28, create_button);
      add_control(window, L"BUTTON", L"Recover", WS_TABSTOP, 594, 48, 66, 28, recover_button);
      add_control(window, L"STATIC", L"Open a WDS project or create one in a new folder.", 0, 16,
                  91, 644, 24, status);
      add_control(window, L"STATIC", L"Project resources", 0, 16, 124, 644, 22);
      add_control(window, L"LISTBOX", L"",
                  WS_BORDER | WS_VSCROLL | WS_TABSTOP | LBS_NOINTEGRALHEIGHT, 16, 150, 644, 165,
                  resources);
      add_control(window, L"STATIC", L"Data resource ID", 0, 16, 330, 160, 20);
      add_control(window, L"STATIC", L"Integer field", 0, 188, 330, 160, 20);
      add_control(window, L"STATIC", L"New value", 0, 360, 330, 100, 20);
      add_control(window, L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 16, 352, 160, 24,
                  resource_id);
      add_control(window, L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 188, 352, 160, 24,
                  field_name);
      add_control(window, L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 360, 352, 100, 24,
                  field_value);
      add_control(window, L"BUTTON", L"Preview", WS_TABSTOP, 474, 350, 92, 28, preview_button);
      add_control(window, L"BUTTON", L"Apply", WS_TABSTOP | WS_DISABLED, 574, 350, 86, 28,
                  apply_button);
      add_control(window, L"STATIC", L"Open a project to edit an existing integer field.", 0, 16,
                  391, 644, 30, edit_status);
      add_control(window, L"BUTTON", L"Play", WS_TABSTOP | WS_DISABLED, 16, 427, 80, 28,
                  play_button);
      add_control(window, L"BUTTON", L"Stop", WS_TABSTOP | WS_DISABLED, 104, 427, 80, 28,
                  stop_button);
      add_control(window, L"STATIC", L"", 0, 195, 431, 465, 26, play_status);
      return GetDlgItem(window, resources) && GetDlgItem(window, folder_input) ? 0 : -1;
    case WM_PAINT:
      paint_play(window, *studio);
      return 0;
    case WM_TIMER:
      if (param == play_timer && studio->session) {
        studio->session->advance(1);
        const auto& scene = studio->session->snapshot();
        if (scene.ticks == wds::platform::SceneSession::max_ticks) KillTimer(window, play_timer);
        show_play(window, scene);
      }
      return 0;
    case WM_COMMAND: {
      const auto action = LOWORD(param);
      if (HIWORD(param) == EN_CHANGE && (action == folder_input || action == resource_id ||
                                         action == field_name || action == field_value)) {
        studio->preview.reset();
        EnableWindow(GetDlgItem(window, apply_button), FALSE);
        SetDlgItemTextW(window, edit_status, L"");
        if (action == folder_input)
          set_project(window, *studio, {std::nullopt, "open_required", "."});
        return 0;
      }
      if (HIWORD(param) != BN_CLICKED) break;
      if (action == play_button) {
        play(window, *studio);
        return 0;
      }
      if (action == stop_button) {
        stop_play(window, *studio);
        SetFocus(GetDlgItem(window, play_button));
        return 0;
      }
      if (action == preview_button || action == apply_button) {
        edit(window, *studio, action == apply_button);
        return 0;
      }
      if (action != open_button && action != create_button && action != recover_button) break;
      const auto folder = text(GetDlgItem(window, folder_input));
      SetDlgItemTextW(window, edit_status, L"");
      if (folder.empty()) {
        set_project(window, *studio, {std::nullopt, "missing_folder", "."});
      } else if (action == create_button) {
        set_project(
            window, *studio,
            wds::platform::create_project(folder, ascii_id(text(GetDlgItem(window, project_id)))));
      } else if (action == recover_button) {
        set_project(window, *studio, wds::platform::recover_project(folder));
      } else {
        set_project(window, *studio, wds::platform::open_project(folder));
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
  Studio studio;
  const auto window =
      CreateWindowExW(0, type.lpszClassName, L"WDS Studio | Project Workspace",
                      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT,
                      CW_USEDEFAULT, 700, 660, nullptr, nullptr, instance, &studio);
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
