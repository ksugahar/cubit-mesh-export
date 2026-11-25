"""Unit test for the exporter-owned Cubit menu (private PySide6).

Exercises the PySide6 Cubit Mesh Export toolbar that replaced the retired
C++ Qt5 .ccl plugin in 2026-05.  Tests use Cubit 2026.8 embedded Python and its offscreen Qt plugin.

Coverage:
    A. Module imports + symbol surface
    B. Export actions are registered through Cubit's Claro API
    C. ExportDialog.cubit_command() round-trip (default options yield the
       expected `export netgen` APREPRO command)
    D. Netgen Kelvin options (add_kelvin, kelvin_mesh, kelvin_sym_*)
    F. Exactly one flat Export action that opens the Netgen dialog
    H. install_menu(): one menu-bar action, idempotent, removable
    I. Current journal is an optional output-name hint, never a prerequisite
    J. Settings persistence round-trip
    K. Mesh Evaluation is not part of the Cubit menu; the
       p-convergence demonstration lives under docs/cubit_mesh_export.

Run this file directly with the licensed Cubit 2026.8 embedded Python.
The repository pytest suite uses the separate Qt-free menu contracts.
"""

from __future__ import annotations

import os
import sys
import tempfile
import types
import unittest
from unittest.mock import MagicMock, patch

if __name__ != "__main__":
    raise unittest.SkipTest("Run the Qt validation directly with Cubit 2026.8 embedded Python.")

_REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(_REPO_ROOT, "tools"))
from cubit_qt_test_env import configure_cubit_qt  # noqa: E402
_DLL_DIRECTORY = configure_cubit_qt()
sys.path.insert(0, os.path.join(_REPO_ROOT, "packages", "cubit-mesh-export",
                              "src", "cubit_mesh_export", "cubit_gui"))
from PySide6.QtWidgets import QApplication, QMainWindow, QMenuBar  # noqa: E402


# Module under test.  Top-level `import cubit` is forbidden in the
# module (per layer 2 isolation rules) -- this import must succeed
# without Cubit on PATH when PySide6 is available.
import cubit_export_menu as rem  # noqa: E402


_QAPP = None


def _ensure_qapp():
    """Create / reuse a single QApplication for all tests."""
    global _QAPP
    if _QAPP is None:
        _QAPP = QApplication.instance() or QApplication([])
    return _QAPP


# ----------------------------------------------------------------------
# A. Module imports + symbol surface
# ----------------------------------------------------------------------
class TestModuleSurface(unittest.TestCase):

    def test_no_format_dispatch_layer(self):
        self.assertEqual(rem._SETTINGS_KEY, "netgen_vol")
        for name in ("FMT_NETGEN", "FMT_GMSH", "FMT_COMSOL", "_FORMAT_EXTS",
                     "_FORMAT_TITLES", "_MENU_SPECS", "launch_export",
                     "_run_generic_export"):
            self.assertFalse(hasattr(rem, name), name)

    def test_public_callables_present(self):
        for name in ("install_menu", "find_claro", "_current_journal_hint",
                     "ExportDialog", "launch_netgen_export"):
            self.assertTrue(hasattr(rem, name),
                            f"cubit_export_menu missing: {name}")
            self.assertTrue(callable(getattr(rem, name)))

    def test_no_top_level_cubit_import(self):
        """Module must not `import cubit` at top level -- breaks pytest /
        any non-Cubit context per Layer-2 architecture (CLAUDE.md)."""
        with open(rem.__file__, "r", encoding="utf-8") as f:
            src = f.read()
        # Allow `import cubit` only INSIDE a function (indented),
        # never at column 0.
        for line in src.splitlines():
            stripped = line.lstrip()
            if stripped.startswith("import cubit") or \
                    stripped.startswith("from cubit "):
                indent = len(line) - len(stripped)
                self.assertGreater(
                    indent, 0,
                    f"top-level cubit import found: {line!r}")


# ----------------------------------------------------------------------
# B. One-level menu-bar action
# ----------------------------------------------------------------------
class TestMenuBarAction(unittest.TestCase):

    def test_module_adds_no_popup_menu(self):
        with open(rem.__file__, "r", encoding="utf-8") as f:
            source = f.read()
        self.assertNotIn("QMenu(", source)
        self.assertNotIn("addMenu", source)
        self.assertIn("bar.addAction(action)", source)


# ----------------------------------------------------------------------
# C-G. ExportDialog.cubit_command() round-trip
# ----------------------------------------------------------------------
class _DialogTestBase(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        _ensure_qapp()
        cls._tmpdir = tempfile.mkdtemp(prefix="rem_test_")
        cls._jou = os.path.join(cls._tmpdir, "myjob.jou")
        # Touch a fake .jou so file_path() can derive basename
        with open(cls._jou, "w") as f:
            f.write("# fake journal\n")

    def _make_dialog(self, cubit_stub=None):
        stub = cubit_stub or MagicMock()
        stub.get_block_id_list = MagicMock(return_value=[])
        d = rem.ExportDialog(self._jou, stub)
        return d


class TestNetgenCommand(_DialogTestBase):

    def test_default_command(self):
        d = self._make_dialog()
        cmd = d.cubit_command()
        # Default order is 3 (combo index 2)
        self.assertRegex(
            cmd,
            r'^export netgen "[^"]+myjob\.vol" order 3 overwrite$')
        # No Kelvin by default
        self.assertNotIn("add_kelvin", cmd)

    def test_order_variants(self):
        d = self._make_dialog()
        for combo_idx, expected_order in [(0, 1), (1, 2), (2, 3),
                                          (3, 4), (4, 5)]:
            d._order.setCurrentIndex(combo_idx)
            cmd = d.cubit_command()
            self.assertIn(f"order {expected_order} ", cmd + " ")

    def test_kelvin_enabled_default(self):
        d = self._make_dialog()
        d._kelvin_enable.setChecked(True)
        cmd = d.cubit_command()
        self.assertIn("add_kelvin", cmd)
        # Default sym_x/y/z combo is "(none)" -> no flags emitted
        self.assertNotIn("kelvin_sym_x", cmd)
        self.assertNotIn("kelvin_sym_y", cmd)
        self.assertNotIn("kelvin_sym_z", cmd)

    def test_kelvin_with_mesh_size_and_symmetry(self):
        d = self._make_dialog()
        d._kelvin_enable.setChecked(True)
        d._kelvin_mesh_size.setText("0.05")
        # Find "bn" / "ht" in the combo items (order may vary)
        for combo, val in [(d._kelvin_sym_x, "bn"),
                            (d._kelvin_sym_y, "ht"),
                            (d._kelvin_sym_z, "bn")]:
            for i in range(combo.count()):
                if combo.itemText(i) == val:
                    combo.setCurrentIndex(i)
                    break
        cmd = d.cubit_command()
        self.assertIn("add_kelvin", cmd)
        self.assertIn("kelvin_mesh 0.05", cmd)
        self.assertIn("kelvin_sym_x bn", cmd)
        self.assertIn("kelvin_sym_y ht", cmd)
        self.assertIn("kelvin_sym_z bn", cmd)


class TestInstallMenu(unittest.TestCase):
    """install_menu() against a fixture window injected through find_claro.

    Real main-window discovery is covered separately by TestFindClaro; here
    the target window is fixed so stray top-level windows cannot win.
    """

    def setUp(self):
        self.app = _ensure_qapp()
        self.win = QMainWindow()
        self.win.setObjectName("Claro")
        self.calls = []
        self._orig_launch = rem.launch_netgen_export
        self._orig_find = rem.find_claro
        rem.launch_netgen_export = lambda: self.calls.append("netgen")
        rem.find_claro = lambda: self.win

    def tearDown(self):
        rem.remove_menu()
        self.app.processEvents()
        rem.launch_netgen_export = self._orig_launch
        rem.find_claro = self._orig_find
        self.win.deleteLater()
        self.app.processEvents()

    def _ours(self, win=None):
        win = win or self.win
        return [a for a in win.menuBar().actions()
                if a.objectName() == rem._MENU_OBJECT_NAME]

    def test_install_adds_one_top_level_export_action(self):
        self.assertTrue(rem.install_menu())
        self.app.processEvents()
        (action,) = self._ours()
        self.assertEqual(action.text(), "&Export")
        self.assertIsNone(action.menu())
        action.trigger()
        self.assertEqual(self.calls, ["netgen"])

    def test_install_is_idempotent(self):
        rem.install_menu()
        rem.install_menu()
        self.app.processEvents()
        self.assertEqual(len(self._ours()), 1)

    def test_remove_cancels_pending_reattach(self):
        rem.install_menu()
        self.assertTrue(rem.remove_menu())
        self.app.processEvents()
        self.assertEqual(self._ours(), [])

    def test_reattach_follows_a_replaced_menu_bar(self):
        rem.install_menu()
        self.win.setMenuBar(QMenuBar())  # deletes the old bar, as Cubit does at startup
        self.app.processEvents()
        (action,) = self._ours()
        self.assertIs(action.parent(), self.win.menuBar())
        self.assertTrue(rem.remove_menu())
        self.assertEqual(self._ours(), [])


class TestFindClaro(unittest.TestCase):

    def setUp(self):
        _ensure_qapp()
        self._win = QMainWindow()
        self._win.setObjectName("claro")
        self._win.show()

    def tearDown(self):
        self._win.hide()
        self._win.setParent(None)
        self._win.deleteLater()
        self._win = None
        _ensure_qapp().processEvents()

    def test_find_claro_returns_our_stub(self):
        m = rem.find_claro()
        self.assertIs(m, self._win)


# ----------------------------------------------------------------------
# I. Optional current-journal hint
# ----------------------------------------------------------------------
class TestCurrentJournalHint(unittest.TestCase):

    def test_already_loaded_via_cubit(self):
        stub = MagicMock()
        with tempfile.NamedTemporaryFile(suffix=".jou", delete=False) as f:
            jou = f.name
        try:
            stub.get_current_journal_file = MagicMock(return_value=jou)
            got = rem._current_journal_hint(stub)
            self.assertEqual(got.replace("\\", "/"), jou.replace("\\", "/"))
            stub.cmd.assert_not_called()
        finally:
            os.unlink(jou)

    def test_no_journal_is_valid_for_loaded_model(self):
        stub = MagicMock()
        stub.get_current_journal_file = MagicMock(return_value="")
        self.assertEqual(rem._current_journal_hint(stub), "")
        stub.cmd.assert_not_called()

    def test_dialog_without_journal_uses_regular_default(self):
        _ensure_qapp()
        stub = MagicMock()
        stub.get_block_id_list = MagicMock(return_value=[])
        dialog = rem.ExportDialog("", stub)
        self.assertTrue(dialog.file_path().endswith("/ExportedMesh.vol"))

    def test_loaded_model_export_never_prompts_to_save_journal(self):
        stub = MagicMock()
        stub.get_volume_count = MagicMock(return_value=1)
        stub.get_current_journal_file = MagicMock(return_value="")
        dialog = MagicMock()
        dialog.exec.return_value = rem.QDialog.Rejected
        with patch.object(rem, "ExportDialog", return_value=dialog) as factory, \
                patch.object(rem.QFileDialog, "getSaveFileName") as save_dialog:
            rem._run_netgen_export(stub, parent=None)
        save_dialog.assert_not_called()
        factory.assert_called_once_with("", stub, parent=None)


# ----------------------------------------------------------------------
# J. Settings persistence round-trip
# ----------------------------------------------------------------------
class TestSettingsRoundTrip(unittest.TestCase):

    def setUp(self):
        # Redirect settings to a temp dir
        self._tmp = tempfile.mkdtemp(prefix="rem_settings_")
        self._orig_settings_dir = rem._settings_dir
        rem._settings_dir = lambda: self._tmp

    def tearDown(self):
        rem._settings_dir = self._orig_settings_dir
        import shutil
        shutil.rmtree(self._tmp, ignore_errors=True)

    def test_load_empty_is_dict(self):
        self.assertEqual(rem._load_settings(), {})

    def test_save_then_load(self):
        rem._save_settings({"netgen_vol": {"order": 4,
                                            "dir": "C:/temp"}})
        got = rem._load_settings()
        self.assertEqual(got["netgen_vol"]["order"], 4)
        self.assertEqual(got["netgen_vol"]["dir"], "C:/temp")

    def test_load_corrupt_returns_empty(self):
        with open(os.path.join(self._tmp, "export_settings.json"),
                  "w") as f:
            f.write("{ not valid json ]")
        self.assertEqual(rem._load_settings(), {})


# ----------------------------------------------------------------------
# K. Mesh evaluation routing
# ----------------------------------------------------------------------
class TestMeshEvaluationDocsRouting(unittest.TestCase):

    def test_mesh_evaluation_runner_removed_from_menu_module(self):
        self.assertFalse(hasattr(rem, "_run_mesh_evaluation"))
        self.assertFalse(hasattr(rem, "_show_mesh_eval_result"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
