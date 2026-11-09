"""Keep repository pytest sessions non-interactive."""
import importlib.abc
import os
import sys

from tools.cubit_qt_test_env import offscreen_environment, suppress_windows_error_dialogs


class _NoQtWidgets(importlib.abc.MetaPathFinder):
    def find_spec(self, fullname, path=None, target=None):
        if fullname in {"PySide6.QtWidgets", "PySide6.QtGui"}:
            raise RuntimeError("Real Qt GUI imports belong in the licensed Cubit offscreen subprocess.")


def pytest_configure(config):
    suppress_windows_error_dialogs()
    offscreen_environment(os.environ)
    sys.meta_path.insert(0, _NoQtWidgets())
