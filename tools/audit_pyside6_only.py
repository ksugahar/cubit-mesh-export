#!/usr/bin/env python3
"""Check the standalone Cubit toolbar, Qt imports, and native plugin boundary."""
from __future__ import annotations

import ast
import json
import os
import re
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

if __name__ == "__main__":
    from cubit_qt_test_env import configure_cubit_qt, offscreen_environment, suppress_windows_error_dialogs
else:
    from tools.cubit_qt_test_env import configure_cubit_qt, offscreen_environment, suppress_windows_error_dialogs

ROOT = Path(__file__).resolve().parent.parent
GUI_ROOT = ROOT / 'packages/cubit-mesh-export/src/cubit_mesh_export/cubit_gui'

CUBIT_TOOLBAR_MODULES = [
    "packages/cubit-mesh-export/src/cubit_mesh_export/cubit_gui/cubit_export_menu.py",
]

PYSIDE6_ALLOWED_FILES = {
    "packages/cubit-mesh-export/src/cubit_mesh_export/cubit_gui/cubit_export_menu.py",
    "validation_test/cubit_mesh_export/standalone_gui_probe.py",
    "validation_test/cubit_mesh_export/standalone_dialog_probe.py",
    "validation_test/cubit_mesh_export/standalone_toolbar_probe.py",
    "packages/cubit-mesh-export/src/cubit_mesh_export/mcp/bootstrap.py",
    "packages/cubit-mesh-export/src/cubit_mesh_export/cubit_gui/toolbar_probe.py",
    "tools/audit_pyside6_only.py",
    "validation_test/panels/conftest.py",
    "validation_test/panels/test_cubit_export_menu.py",
}


# ----------------------------------------------------------------------
# Static checks
# ----------------------------------------------------------------------
def check_no_legacy_qt_imports() -> list[str]:
    """git grep for real PyQt5/PySide2/PyQt6 import statements (must be none)."""
    pats = [
        r"^[[:space:]]*from PyQt5",
        r"^[[:space:]]*import PyQt5",
        r"^[[:space:]]*from PySide2",
        r"^[[:space:]]*import PySide2",
        r"^[[:space:]]*from PyQt6",
        r"^[[:space:]]*import PyQt6",
    ]
    args = ["git", "grep", "-nI"]
    for p in pats:
        args += ["-e", p]
    args += ["--", "*.py"]
    r = subprocess.run(args, cwd=ROOT, capture_output=True, text=True, encoding="utf-8")
    if r.returncode not in (0, 1):
        return [f"git audit failed ({r.returncode}): {r.stderr.strip()}"]
    # git grep: rc 0 = matches found (BAD), rc 1 = no match (GOOD)
    hits = [ln for ln in r.stdout.splitlines() if ln.strip()]
    return hits


def check_pyside6_ownership_boundary() -> list[str]:
    """Reject executable PySide6 imports outside Cubit-owned code/tests."""
    pattern = r"^[[:space:]]*(from|import)[[:space:]]+PySide6"
    result = subprocess.run(
        ["git", "grep", "-lI", "-E", pattern, "--", "*.py"],
        cwd=ROOT,
        capture_output=True,
        text=True, encoding="utf-8",
    )
    issues = []
    if result.returncode not in (0, 1):
        return [f"git audit failed ({result.returncode}): {result.stderr.strip()}"]
    for raw_path in result.stdout.splitlines():
        relative = raw_path.strip().replace("\\", "/")
        if not relative or relative in PYSIDE6_ALLOWED_FILES:
            continue
        path = ROOT / relative
        try:
            tree = ast.parse(path.read_text(encoding="utf-8"), filename=relative)
        except (OSError, SyntaxError):
            continue
        for node in ast.walk(tree):
            module = ""
            if isinstance(node, ast.ImportFrom):
                module = node.module or ""
            elif isinstance(node, ast.Import):
                module = next(
                    (alias.name for alias in node.names
                     if alias.name == "PySide6" or alias.name.startswith("PySide6.")),
                    "",
                )
            if module == "PySide6" or module.startswith("PySide6."):
                issues.append(f"{relative}:{node.lineno}: {module}")
    return issues


def check_cubit_toolbar_imports_pyside6() -> list[str]:
    """Each Cubit toolbar module must import PySide6."""
    missing = []
    for rel in CUBIT_TOOLBAR_MODULES:
        p = ROOT / rel
        if not p.is_file():
            missing.append(f"{rel}: missing file")
            continue
        txt = p.read_text(encoding="utf-8", errors="replace")
        has = "from PySide6" in txt or "import PySide6" in txt
        if not has:
            missing.append(rel)
    return missing


def check_ccm_qt_free() -> tuple[str, list[str]]:
    """cubit_mesh_export.ccm must not import any Qt5 DLL (pefile). Returns (status, issues)."""
    ccm = ROOT / "packages" / "cubit-mesh-export" / "src" / "cubit_mesh_export" / "cubit_mesh_export.ccm"
    if not ccm.is_file():
        return ("skip (no .ccm in package source)", [])
    try:
        import pefile
    except ImportError:
        return ("skip (pefile not installed)", [])
    pe = pefile.PE(str(ccm), fast_load=True)
    pe.parse_data_directories(
        directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_IMPORT"]])
    deps = {e.dll.decode().lower() for e in getattr(pe, "DIRECTORY_ENTRY_IMPORT", [])}
    qt = sorted(d for d in deps if d.startswith("qt"))
    return ("checked", [f".ccm imports Qt DLL(s): {qt}"] if qt else [])


def check_deployed_panel_source(
        cubit_file: Path | None = None,
        expected_register: Path | None = None) -> tuple[str, list[str]]:
    """Reject a registered startup shim that loads another checkout.

    The package-level verifier proves that *its own* deployment is internally
    consistent.  A developer audit also has to prove that Cubit loads the
    checkout being audited; otherwise an old release worktree can pass while
    the current menu fix is never executed.
    """
    cubit_file = cubit_file or (Path.home() / ".cubit")
    expected_register = expected_register or (
        GUI_ROOT / "register_toolbar.py")
    if not cubit_file.is_file():
        return ("skip (no ~/.cubit)", [])

    cubit_text = cubit_file.read_text(encoding="utf-8", errors="replace")
    begin = cubit_text.find("## BEGIN cubit-mesh-export toolbar")
    end = cubit_text.find("## END cubit-mesh-export toolbar")
    if begin < 0 or end < begin:
        return ("skip (exporter startup not registered)", [])
    match = re.search(r'play\s+"([^"]+)"', cubit_text[begin:end])
    if not match:
        return ("checked", [f"invalid exporter startup block: {cubit_file}"])

    startup = Path(match.group(1).replace("/", os.sep))
    if not startup.is_file():
        return ("checked", [f"registered startup shim is missing: {startup}"])
    startup_text = startup.read_text(encoding="utf-8", errors="replace")
    expected = str(expected_register.resolve()).replace("\\", "/").lower()
    actual_text = startup_text.replace("\\", "/").lower()
    if expected not in actual_text:
        loaded = re.findall(r"[^'\"]+register_toolbar\.py", actual_text)
        loaded_text = loaded[-1] if loaded else "unknown"
        return ("checked", [
            "Cubit startup loads a different checkout: "
            f"expected={expected}; loaded={loaded_text}"
        ])
    return ("checked", [])


def check_official_toolbar_contract() -> list[str]:
    """Validate the WorkflowToolbar and Claro-owned menu contracts."""
    toolbar_root = GUI_ROOT / "cubit_toolbar"
    template = toolbar_root / "toolbars" / "cubit_mesh_export_toolbar.ttb.tmpl"
    register = GUI_ROOT / "register_toolbar.py"
    export_menu = GUI_ROOT / "cubit_export_menu.py"
    issues: list[str] = []
    if not template.is_file():
        return [f"missing WorkflowToolbar template: {template}"]
    try:
        root = ET.fromstring(template.read_text(encoding="utf-8"))
    except (OSError, ET.ParseError) as exc:
        return [f"invalid WorkflowToolbar template: {exc}"]
    if root.tag != "WorkflowToolbar" or root.attrib.get("name") != "Cubit Mesh Export":
        issues.append("toolbar root must be WorkflowToolbar name='Cubit Mesh Export'")
    buttons = root.findall("WTButton")
    if len(buttons) != 7:
        issues.append(f"expected 7 official toolbar buttons, found {len(buttons)}")

    for filename in root.findall(".//filename"):
        relative = (filename.text or "").split("@TOOLBAR_INSTALL_DIR@/", 1)[-1]
        if not relative or not (toolbar_root / relative).is_file():
            issues.append(f"missing toolbar launcher: {relative!r}")

    register_text = register.read_text(encoding="utf-8", errors="replace")
    menu_text = export_menu.read_text(encoding="utf-8", errors="replace")
    if "cubit_export_menu.install_menu()" not in register_text:
        issues.append("~/.cubit startup does not install the Claro Export menu")
    if "emclaro.add_to_menu" not in menu_text:
        issues.append("Export menu does not use Cubit's official Claro API")
    if "QMenu(" in menu_text or "menu_bar.addMenu" in menu_text:
        issues.append("Export menu still injects an unsupported PySide6 QMenu")
    return issues


# ----------------------------------------------------------------------
# Headless panel smoke (run in --smoke subprocess on offscreen Qt)
# ----------------------------------------------------------------------
class _StubCubit:
    def get_block_id_list(self):
        return []

    def get_block_name(self, _bid):
        return ""

    def get_volume_count(self):
        return 1


def _run_smoke() -> int:
    """--smoke mode: construct ExportDialog under offscreen Qt."""
    fails: list[str] = []
    try:
        dll_directory = configure_cubit_qt()
        sys.path.insert(0, str(GUI_ROOT))
        from PySide6.QtWidgets import QApplication
        app = QApplication.instance() or QApplication([])
        if app.platformName() != "offscreen":
            raise RuntimeError(f"Unexpected Qt platform: {app.platformName()}")
        import cubit_export_menu as rem
        stub = _StubCubit()
        for fmt in (rem.FMT_NETGEN, rem.FMT_GMSH, rem.FMT_NASTRAN,
                    rem.FMT_MEG):
            dlg = rem.ExportDialog(fmt, "C:/temp/model.jou", stub, parent=None)
            cmd = dlg.cubit_command()
            if not (cmd.startswith("export ") and cmd.endswith(" overwrite")):
                fails.append(f"ExportDialog[{fmt}]: bad cmd {cmd!r}")
    except Exception as e:  # noqa: BLE001
        fails.append(f"ExportDialog: {type(e).__name__}: {e}")

    print(json.dumps({"ok": not fails, "fails": fails}), flush=True)
    return 1 if fails else 0


def _find_cubit_python() -> Path | None:
    """Return Cubit's embedded Python, which owns production PySide6."""
    explicit = os.environ.get("CUBIT_PYTHON")
    if explicit and Path(explicit).is_file():
        return Path(explicit)
    cubit_path = os.environ.get("CUBIT_PATH")
    if cubit_path:
        root = Path(cubit_path)
        bin_dir = root if root.name.lower() == "bin" else root / "bin"
        candidate = bin_dir / "python3" / "python.exe"
        if candidate.is_file():
            return candidate
    if os.name == "nt":
        program_files = Path(os.environ.get("ProgramFiles", r"C:\Program Files"))
        candidate = program_files / "Coreform Cubit 2026.8/bin/python3/python.exe"
        if candidate.is_file():
            return candidate
    return None


def check_panel_smoke(python_executable: Path) -> list[str]:
    """Spawn the licensed Cubit PySide6 smoke without opening a window."""
    suppress_windows_error_dialogs()
    env = os.environ.copy()
    offscreen_environment(env)
    for name in ("PYTHONHOME", "PYTHONPATH", "VIRTUAL_ENV"):
        env.pop(name, None)
    bin_dir = python_executable.resolve().parent.parent
    env["PATH"] = str(bin_dir) + os.pathsep + env.get("PATH", "")
    kwargs = {}
    if os.name == "nt":
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = subprocess.SW_HIDE
        kwargs.update(creationflags=subprocess.CREATE_NEW_CONSOLE, startupinfo=startup)
    try:
        r = subprocess.run([str(python_executable), str(Path(__file__).resolve()), "--smoke"],
                           capture_output=True, text=True, env=env, timeout=30, **kwargs)
    except subprocess.TimeoutExpired:
        return ["Cubit offscreen toolbar smoke timed out after 30 seconds"]
    line = next((ln for ln in reversed(r.stdout.splitlines()) if ln.strip().startswith("{")), "")
    if not line:
        return [f"toolbar smoke produced no JSON (rc={r.returncode}); stderr tail:\n{r.stderr[-800:]}"]
    data = json.loads(line)
    if r.returncode or not data.get("ok"):
        return list(data.get("fails") or [f"toolbar smoke failed (rc={r.returncode})"])
    return []


# ----------------------------------------------------------------------
def main(argv: list[str]) -> int:
    if "--smoke" in argv:
        return _run_smoke()

    print("=" * 64)
    print("  Cubit toolbar PySide6 / legacy Qt audit")
    print("=" * 64)
    all_issues: list[str] = []

    legacy = check_no_legacy_qt_imports()
    print(f"[{'FAIL' if legacy else 'OK'}] legacy Qt imports (PyQt5/PySide2/PyQt6): "
          f"{len(legacy)} found")
    for h in legacy:
        print("       " + h)
    all_issues += [f"legacy Qt import: {h}" for h in legacy]

    boundary = check_pyside6_ownership_boundary()
    print(f"[{'FAIL' if boundary else 'OK'}] PySide6 imports stay inside "
          f"Cubit-owned code/tests: {len(boundary)} violation(s)")
    for issue in boundary:
        print("       " + issue)
    all_issues += [f"PySide6 ownership violation: {h}" for h in boundary]

    gui_missing = check_cubit_toolbar_imports_pyside6()
    print(f"[{'FAIL' if gui_missing else 'OK'}] Cubit toolbar modules import PySide6")
    all_issues += [f"Cubit toolbar module missing PySide6 import: {m}" for m in gui_missing]

    ccm_status, ccm_issues = check_ccm_qt_free()
    print(f"[{'FAIL' if ccm_issues else 'OK'}] cubit_mesh_export.ccm Qt-free  ({ccm_status})")
    all_issues += ccm_issues

    if "--deployed" in argv:
        deploy_status, deploy_issues = check_deployed_panel_source()
        print(f"Cubit startup source: {deploy_status}")
        for issue in deploy_issues:
            print("       " + issue)
        all_issues += deploy_issues

    toolbar_contract = check_official_toolbar_contract()
    print(f"[{'FAIL' if toolbar_contract else 'OK'}] official Coreform "
          "WorkflowToolbar + Claro Export menu contract")
    for issue in toolbar_contract:
        print("       " + issue)
    all_issues += toolbar_contract

    if "--licensed-smoke" in argv:
        qt_python = _find_cubit_python()
        smoke = (check_panel_smoke(qt_python) if qt_python else
                 ["Coreform Cubit 2026.8 embedded Python is required for --licensed-smoke"])
        if not smoke:
            print(f"[OK] Cubit 2026.8 offscreen ExportDialog x4 ({qt_python})")
        for issue in smoke:
            print("[FAIL] " + issue)
        all_issues += smoke

    print("-" * 64)
    if all_issues:
        print(f"RESULT: {len(all_issues)} ISSUE(S) -- Cubit toolbar audit NOT clean")
        return 1
    print("RESULT: CLEAN")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
