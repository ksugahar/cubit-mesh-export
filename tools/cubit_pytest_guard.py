"""Guard ordinary Python against vendor GUI libraries during pytest."""
import ctypes
import importlib.abc
import os
from pathlib import PureWindowsPath
import re
import sys

from tools.cubit_qt_test_env import offscreen_environment, suppress_windows_error_dialogs


_BLOCKED_IMPORTS = {"cubit", "_cubit3", "emclaro", "PySide6", "PySide2", "PyQt5", "PyQt6"}
_QT_LIBRARY = re.compile(r"qt[456][a-z0-9_]*\.dll$", re.IGNORECASE)


class _NoCubitOrQt(importlib.abc.MetaPathFinder):
    def find_spec(self, fullname, path=None, target=None):
        if fullname.split(".", 1)[0] in _BLOCKED_IMPORTS:
            raise ImportError(f"{fullname}: use Cubit's batch/nographics subprocess, not ordinary pytest")


def _forbidden_library(path):
    parts = PureWindowsPath(path).parts
    name = parts[-1].lower() if parts else ""
    return (
        bool(_QT_LIBRARY.fullmatch(name))
        or name in {"qwindows.dll", "qoffscreen.dll", "qminimal.dll", "emclaro.pyd", "_cubit3.pyd"}
        or any(part.lower().startswith("coreform cubit ") for part in parts[:-1])
        or name.startswith(("cubit", "claro")) and name.endswith((".dll", ".ccm"))
    )


def _loaded_windows_modules():
    """Read loader paths for this process, without importing any GUI runtime."""
    from ctypes import wintypes

    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.GetCurrentProcess.restype = wintypes.HANDLE
    enum = kernel.K32EnumProcessModules
    enum.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.HMODULE),
                     wintypes.DWORD, ctypes.POINTER(wintypes.DWORD)]
    enum.restype = wintypes.BOOL
    filename = kernel.GetModuleFileNameW
    filename.argtypes = [wintypes.HMODULE, wintypes.LPWSTR, wintypes.DWORD]
    filename.restype = wintypes.DWORD
    needed = wintypes.DWORD()
    capacity = 128
    while True:
        handles = (wintypes.HMODULE * capacity)()
        if not enum(kernel.GetCurrentProcess(), handles, ctypes.sizeof(handles), ctypes.byref(needed)):
            raise ctypes.WinError(ctypes.get_last_error())
        if needed.value <= ctypes.sizeof(handles):
            break
        capacity = needed.value // ctypes.sizeof(wintypes.HMODULE) + 16
    buf = ctypes.create_unicode_buffer(32768)
    for handle in handles[:needed.value // ctypes.sizeof(wintypes.HMODULE)]:
        if filename(handle, buf, len(buf)):
            yield buf.value


def loaded_vendor_libraries():
    if os.name != "nt":
        return []
    return sorted({path for path in _loaded_windows_modules() if _forbidden_library(path)})


def _strip_cubit_path(env):
    roots = []
    for name in ("CUBIT_PATH", "CUBIT_BIN_DIR", "CUBIT_INSTALL_DIR"):
        if value := env.get(name):
            path = PureWindowsPath(value.strip('"'))
            roots.append(path.parent if path.name.lower() == "bin" else path)
    retained = []
    for entry in env.get("PATH", "").split(os.pathsep):
        path = PureWindowsPath(entry.strip('"'))
        if any(part.lower().startswith("coreform cubit ") for part in path.parts):
            continue
        if any(path.is_relative_to(root) for root in roots):
            continue
        retained.append(entry)
    env["PATH"] = os.pathsep.join(retained)


def configure():
    suppress_windows_error_dialogs()
    if os.name == "nt":
        _strip_cubit_path(os.environ)
    offscreen_environment(os.environ)
    sys.meta_path.insert(0, _NoCubitOrQt())
