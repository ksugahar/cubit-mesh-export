"""Process-local safeguards for Cubit 2026.8's offscreen Qt tests."""
import ctypes
import importlib.util
import os
from pathlib import Path
import sys


def suppress_windows_error_dialogs():
    if os.name == "nt":
        kernel = ctypes.windll.kernel32
        # Preserve inherited modes and suppress OS error/crash/open-file UI.
        kernel.SetErrorMode(kernel.GetErrorMode() | 0x8003)


def offscreen_environment(env):
    env["QT_QPA_PLATFORM"] = "offscreen"
    env["QT_FORCE_STDERR_LOGGING"] = "1"
    env.pop("QT_QPA_PLATFORM_PLUGIN_PATH", None)
    env.pop("QT_PLUGIN_PATH", None)


def configure_cubit_qt():
    """Bind PySide6 and its platform plugin to the same Cubit installation."""
    suppress_windows_error_dialogs()
    offscreen_environment(os.environ)
    executable = Path(sys.executable).resolve()
    bin_dir = executable.parent.parent
    root = bin_dir.parent
    manifest = root / "BUILD_INFO"
    if not manifest.is_file() or "short=2026.8" not in manifest.read_text(encoding="utf-8").splitlines():
        raise RuntimeError("Use the licensed Coreform Cubit 2026.8 embedded Python.")
    spec = importlib.util.find_spec("PySide6")
    if spec is None or not spec.origin or not Path(spec.origin).resolve().is_relative_to(executable.parent):
        raise RuntimeError(f"Cubit embedded PySide6 is missing from {executable.parent}")
    platforms = root / "plugins" / "platforms"
    if not (platforms / "qoffscreen.dll").is_file():
        raise RuntimeError(f"Cubit offscreen Qt platform plugin is missing: {platforms / 'qoffscreen.dll'}")
    # Qt's platform bootstrap calls Win32 MessageBox itself without a console.
    if os.name == "nt" and not ctypes.windll.kernel32.GetConsoleWindow():
        raise RuntimeError("Cubit offscreen Qt requires a console for non-interactive startup failures.")
    os.environ["QT_QPA_PLATFORM_PLUGIN_PATH"] = str(platforms)
    os.environ["QT_PLUGIN_PATH"] = str(platforms.parent)
    return os.add_dll_directory(str(bin_dir))
