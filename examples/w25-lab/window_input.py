"""Exercise the exported expedition through its own hidden Windows window."""

import ctypes
import json
import subprocess
import time
from ctypes import wintypes


KEY_CODES = {"Tab": 0x09, "Escape": 0x1B, "F1": 0x70, "Period": 0xBE}
KEY_DOWN = 0x0100
KEY_UP = 0x0101
CLOSE = 0x0010


class GameWindow:
    def __init__(self, process):
        self.process = process
        self.api = ctypes.WinDLL("user32", use_last_error=True)
        self.callback = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
        self.api.EnumWindows.argtypes = [self.callback, wintypes.LPARAM]
        self.api.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
        self.api.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
        self.api.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM,
                                         wintypes.LPARAM]
        self.api.MapVirtualKeyW.argtypes = [wintypes.UINT, wintypes.UINT]
        self.api.IsWindowVisible.argtypes = [wintypes.HWND]
        self.handle = self.wait_for(self.find, "the exported game's window")
        assert not self.api.IsWindowVisible(self.handle), "the input test window must be hidden"

    def find(self):
        windows = []

        @self.callback
        def add(window, _):
            pid = wintypes.DWORD()
            self.api.GetWindowThreadProcessId(window, ctypes.byref(pid))
            if pid.value == self.process.pid:
                title = ctypes.create_unicode_buffer(80)
                self.api.GetWindowTextW(window, title, len(title))
                if title.value == "Drift Naturalists":
                    windows.append(window)
            return True

        self.api.EnumWindows(add, 0)
        return next(iter(windows), None)

    def wait_for(self, check, description):
        deadline = time.monotonic() + 7
        while time.monotonic() < deadline:
            result = check()
            if result:
                return result
            assert self.process.poll() is None, (description, self.process.returncode)
            time.sleep(0.025)
        raise AssertionError(f"Timed out waiting for {description}")

    def post(self, message, key=0, flags=0):
        if not self.api.PostMessageW(self.handle, message, key, flags):
            raise ctypes.WinError(ctypes.get_last_error())

    def press(self, *keys):
        for key in keys:
            vk = KEY_CODES[key] if key in KEY_CODES else ord(key)
            flags = 1 | (self.api.MapVirtualKeyW(vk, 0) << 16)
            self.post(KEY_DOWN, vk, flags)
            # The display loop polls key edges once per frame at 45 Hz.
            time.sleep(0.12)
            self.post(KEY_UP, vk, flags | (3 << 30))
            time.sleep(0.12)

    def save_with(self, path, history):
        def matches():
            if path.is_file():
                actions = json.loads(path.read_text(encoding="utf-8"))["actions"]
                assert history[:len(actions)] == actions, (path.name, actions, history)
                return actions == history
            return False

        self.wait_for(matches, f"{path.name} to contain {history}")


def verify_window_input(release, project, probe, environment):
    first = release / "expedition.save.json"
    second = release / "expedition.slot-2.json"
    damaged = release / "expedition.slot-3.json"
    subprocess.run([probe, project, second, "farm-start"], check=True, timeout=20)
    original_slot = second.read_bytes()
    planted = json.loads(original_slot)["actions"]
    damaged.write_bytes(b"{broken")
    game = release / "Play.exe"
    with subprocess.Popen([game, "--expedition-input-smoke"], cwd=release.parent,
                          env=environment) as process:
        try:
            window = GameWindow(process)
            window.press("D", "G", "F")
            assert not first.exists(), "an overridden default key still started the game"
            for index, keys in enumerate((("M",), ("Tab", "M"), ("Tab", "M"), ("C",), ("L",),
                                          ("Tab", "Tab", "U"), ("Tab", "U")), 1):
                window.press(*keys)
                window.save_with(first, planted[:index])

            first_slot = first.read_bytes()
            window.press("F1", "L", "3")
            assert first.read_bytes() == first_slot and second.read_bytes() == original_slot
            assert damaged.read_bytes() == b"{broken"
            # A rejected slot keeps the menu open for the next selection.
            window.press("2", "Period", "Period", "U", "Tab", "U", "L", "Tab", "X", "Z", "Z", "X", "U")
            cooked = [*planted, {"command": "wait"}, {"command": "wait"},
                      {"command": "use", "target": "return-path"}, {"command": "use", "target": "plot"},
                      {"command": "east"},
                      {"command": "cook", "target": "kitchen", "ingredients": ["herb", "root"]}]
            window.save_with(second, cooked)
            assert first.read_bytes() == first_slot
            cooked_slot = second.read_bytes()
            window.press("F1", "1", "F1", "Escape", "Period")
            window.save_with(first, [*planted, {"command": "wait"}])
            assert second.read_bytes() == cooked_slot and damaged.read_bytes() == b"{broken"
            window.post(CLOSE)
            assert process.wait(timeout=7) == 0
        finally:
            if process.poll() is None:
                process.terminate()
    subprocess.run([game, "--check-save=2"], env=environment, check=True, timeout=20)
    subprocess.run([probe, project, second, "farm-verify"], check=True, timeout=20)
    print("Windows key messages completed the garden route, switched saves and retained the damaged slot")
