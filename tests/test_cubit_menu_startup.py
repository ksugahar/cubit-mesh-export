"""Regression tests for Cubit's startup hook and GUI ownership boundary."""

from __future__ import annotations

import ast
import importlib.util
import os
from pathlib import Path
import re
import sys
import types

import pytest

ROOT = Path(__file__).resolve().parents[1]
REGISTER_TOOLBAR = ROOT / "packages/cubit-mesh-export/src/cubit_mesh_export/cubit_gui/register_toolbar.py"
EXPORT_MENU = ROOT / "packages/cubit-mesh-export/src/cubit_mesh_export/cubit_gui/cubit_export_menu.py"
AUDIT_SCRIPT = ROOT / "tools" / "audit_pyside6_only.py"

_AUDIT_SPEC = importlib.util.spec_from_file_location(
    "cubit_export_audit_pyside6_only", AUDIT_SCRIPT
)
assert _AUDIT_SPEC is not None and _AUDIT_SPEC.loader is not None
_AUDIT_MODULE = importlib.util.module_from_spec(_AUDIT_SPEC)
_AUDIT_SPEC.loader.exec_module(_AUDIT_MODULE)
check_deployed_panel_source = _AUDIT_MODULE.check_deployed_panel_source
check_pyside6_ownership_boundary = _AUDIT_MODULE.check_pyside6_ownership_boundary


def test_pyside6_is_confined_to_cubit_owned_code_and_tests():
    assert check_pyside6_ownership_boundary() == []


@pytest.mark.parametrize("check", [
    _AUDIT_MODULE.check_no_legacy_qt_imports,
    check_pyside6_ownership_boundary,
])
@pytest.mark.parametrize("returncode", [0, 1, 128])
def test_import_audit_distinguishes_no_matches_from_git_failure(monkeypatch, check, returncode):
    monkeypatch.setattr(_AUDIT_MODULE.subprocess, "run", lambda *a, **kw:
                        types.SimpleNamespace(returncode=returncode, stdout="",
                                              stderr="repository unavailable"))
    issues = check()
    if returncode == 128:
        assert issues == ["git audit failed (128): repository unavailable"]
    else:
        assert issues == []


def test_cubit_qt_missing_offscreen_plugin_fails_before_qt_import(monkeypatch, tmp_path):
    from tools import cubit_qt_test_env as qt

    embedded = tmp_path / "bin" / "python3"
    embedded.mkdir(parents=True)
    (tmp_path / "BUILD_INFO").write_text("short=2026.8\n", encoding="utf-8")
    monkeypatch.setattr(qt.sys, "executable", str(embedded / "python.exe"))
    monkeypatch.setattr(qt.importlib.util, "find_spec", lambda name:
                        types.SimpleNamespace(origin=str(embedded / "Lib/PySide6/__init__.py")))
    monkeypatch.setattr(qt, "offscreen_environment", lambda env: None)
    with pytest.raises(RuntimeError, match="offscreen Qt platform plugin is missing"):
        qt.configure_cubit_qt()


@pytest.mark.parametrize("returncode", [2, -1073741819])
def test_failed_export_process_cannot_pass_with_an_existing_artifact(monkeypatch, tmp_path, returncode):
    path = EXPORT_MENU.parents[1] / "smoke_test.py"
    spec = importlib.util.spec_from_file_location("cubit_process_smoke", path)
    smoke = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(smoke)
    monkeypatch.setattr(smoke, "_find_cubit_exe", lambda: tmp_path / "coreform_cubit.exe")
    monkeypatch.setattr(smoke, "_find_sample_jou", lambda *_: tmp_path / "sample.jou")
    monkeypatch.setattr(smoke.tempfile, "mkdtemp", lambda **_: str(tmp_path))

    def failed_export(*args, **kwargs):
        (tmp_path / "smoke.vol").write_text("exported before failure", encoding="utf-8")
        return types.SimpleNamespace(returncode=returncode, stdout="export finished", stderr="shutdown failed")

    monkeypatch.setattr(smoke.subprocess, "run", failed_export)
    monkeypatch.setattr(smoke, "_validate_exported_vol", lambda *a, **kw:
                        pytest.fail("failed process must not be accepted via artifact validation"))
    assert smoke.run_smoke_test() == 1
    assert (tmp_path / "smoke.vol").is_file()
    assert "shutdown failed" in (tmp_path / "cubit.log").read_text(encoding="utf-8")


def test_deployment_audit_rejects_startup_from_another_checkout(tmp_path):
    gui_rel = Path("packages/cubit-mesh-export/src/cubit_mesh_export/cubit_gui")
    expected = tmp_path / "current" / gui_rel / "register_toolbar.py"
    foreign = tmp_path / "old-release" / gui_rel / "register_toolbar.py"
    expected.parent.mkdir(parents=True)
    foreign.parent.mkdir(parents=True)
    expected.write_text("# current\n", encoding="utf-8")
    foreign.write_text("# stale\n", encoding="utf-8")

    startup = tmp_path / "startup.py"
    startup.write_text(
        f"exec(open(r'{foreign.as_posix()}').read())\n",
        encoding="utf-8",
    )
    cubit_file = tmp_path / ".cubit"
    cubit_file.write_text(
        "## BEGIN cubit-mesh-export toolbar\n"
        f'play "{startup.as_posix()}"\n'
        "## END cubit-mesh-export toolbar\n",
        encoding="utf-8",
    )

    status, issues = check_deployed_panel_source(cubit_file, expected)
    assert status == "checked"
    assert len(issues) == 1
    assert "different checkout" in issues[0]
    assert expected.as_posix().lower() in issues[0]

    startup.write_text(
        f"exec(open(r'{expected.as_posix()}').read())\n",
        encoding="utf-8",
    )
    status, issues = check_deployed_panel_source(cubit_file, expected)
    assert status == "checked"
    assert issues == []


def test_cubit_startup_installs_the_export_menu():
    """The startup hook installs the Export menu-bar action."""
    source = REGISTER_TOOLBAR.read_text(encoding="utf-8")

    assert "cubit_export_menu.install_menu()" in source
    assert "import radia" not in source.replace("import cubit_export_menu", "")


def test_export_menu_is_one_level_menu_bar_action():
    """Export is a single menu-bar action, not a popup menu.

    Cubit rebuilds its menu bar during cold start, so the action is attached
    again once the event loop runs.  Only the module-owned action is touched.
    """
    source = EXPORT_MENU.read_text(encoding="utf-8")

    assert "bar.addAction(action)" in source
    assert "QTimer.singleShot(0, _attach_menu_action)" in source
    assert "bar.removeAction(action)" in source
    assert "emclaro" not in source.replace("``emclaro.add_to_menu``", "")
    assert "QMenu(" not in source
    assert "addMenu" not in source


def test_export_menu_does_not_force_a_journal_save():
    """A loaded Cubit model must export without creating a root-side log."""
    source = EXPORT_MENU.read_text(encoding="utf-8")

    assert "ensure_jou_path" not in source
    # Native export commands take their output path from the export dialog; no save dialog.
    assert "getSaveFileName(" not in source
    assert "save journal" not in source
    assert "_current_journal_hint" in source


def test_find_claro_matches_capital_c_object_name():
    """Cubit 2025.12 names its main window ``Claro`` -- capital C.

    The previous lowercase comparison never matched, and the
    any-QMainWindow fallback then returned ``QtJournalEditor`` instead,
    so dialogs were parented to the wrong window and legacy-menu cleanup
    operated on the wrong menu bar.
    """
    for path in (EXPORT_MENU, REGISTER_TOOLBAR):
        source = path.read_text(encoding="utf-8")
        if "objectName()" not in source:
            continue
        assert 'objectName() == "claro"' not in source, path
        assert 'objectName().lower() == "claro"' in source, path
        assert "QtJournalEditor" in source, path


def test_menu_offers_only_the_maintained_formats():
    """The menu is one flat Netgen action; retired formats are absent."""
    source = EXPORT_MENU.read_text(encoding="utf-8")

    for retired in ("export nastran_bdf", "export meg", "export vtk", "export femeem", "export gmsh",
                    "FMT_VTU", "FMT_JCF", "FMT_MEG", "FMT_COMSOL", "FMT_GMSH", "FMT_NETGEN", "COMSOL", "mphtxt",
                    "nastran", "femeem", "gmsh", "Gmsh", "_MENU_SPECS", "launch_export(", "_run_generic_export"):
        assert retired not in source, retired
    assert re.findall(r'^_MENU_LABEL = "([^"]+)"$', source, flags=re.MULTILINE) == ["Export Netgen (.vol)..."]
    assert source.count("QAction(") == 1


def test_menu_action_dispatches_netgen_export():
    """Triggering the menu-bar action runs the Netgen dialog entry point."""
    source = EXPORT_MENU.read_text(encoding="utf-8")
    tree = ast.parse(source, filename=str(EXPORT_MENU))
    node = next(n for n in tree.body
                if isinstance(n, ast.FunctionDef) and n.name == "_on_export_triggered")
    calls = []
    namespace = {"launch_netgen_export": lambda: calls.append("netgen")}
    exec(compile(ast.Module([node], type_ignores=[]), str(EXPORT_MENU), "exec"), namespace)
    namespace["_on_export_triggered"](False)
    assert calls == ["netgen"]

