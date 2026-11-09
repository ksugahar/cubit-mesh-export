"""Verify native boundary enforcement without loading a real GUI runtime."""
import os
from pathlib import Path
import shutil
import subprocess
import sys

import pytest


@pytest.mark.skipif(os.name != "nt", reason="Windows loader inventory")
def test_loader_guard_names_the_test_that_loads_a_vendor_dll(tmp_path):
    repo = Path(__file__).resolve().parents[1]
    shutil.copyfile(repo / "conftest.py", tmp_path / "conftest.py")
    tools = tmp_path / "tools"
    tools.mkdir()
    for name in ("cubit_pytest_guard.py", "cubit_qt_test_env.py"):
        shutil.copyfile(repo / "tools" / name, tools / name)
    # A Windows system DLL under a synthetic vendor name exercises the real
    # loader enumeration without loading Qt or Cubit in either pytest process.
    probe = tmp_path / "Qt6GuardProbe.dll"
    shutil.copyfile(Path(os.environ["SystemRoot"]) / "System32" / "version.dll", probe)
    (tmp_path / "test_probe.py").write_text(
        "import ctypes\n"
        "from pathlib import Path\n"
        "def test_importing_case():\n"
        "    ctypes.WinDLL(str(Path(__file__).with_name('Qt6GuardProbe.dll')))\n",
        encoding="utf-8",
    )
    proc = subprocess.run(
        [sys.executable, "-m", "pytest", "-q", "test_probe.py"],
        cwd=tmp_path, env={**os.environ, "PYTEST_DISABLE_PLUGIN_AUTOLOAD": "1"},
        capture_output=True, text=True, timeout=30,
    )
    assert proc.returncode == 1, proc.stdout + proc.stderr
    assert "test_probe.py::test_importing_case (call): loaded Cubit/Qt DLLs in pytest" in proc.stdout
    assert probe.name in proc.stdout
