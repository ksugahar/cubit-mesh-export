"""Guards: the normal JCF and COMSOL menu exports show reported sidesets instead of dropping them."""

import json
import sys
import types

from test_export_menu_verification_summary import _load_menu


def _patch_dialogs(monkeypatch, menu, config_path, out_path, shown):
    dialog = type("QFileDialog", (), {
        "getOpenFileName": staticmethod(lambda *a: (str(config_path), "")),
        "getSaveFileName": staticmethod(lambda *a: (str(out_path), "")),
    })
    box = type("QMessageBox", (), {
        "information": staticmethod(lambda parent, title, text: shown.append((title, text))),
        "warning": staticmethod(lambda parent, title, text: shown.append(("WARNING " + title, text))),
    })
    monkeypatch.setattr(menu, "QFileDialog", dialog)
    monkeypatch.setattr(menu, "QMessageBox", box)


def test_jcf_menu_shows_unused_and_shared_sidesets(monkeypatch, tmp_path, capsys):
    menu = _load_menu(monkeypatch)
    config = tmp_path / "settings.json"
    config.write_text(json.dumps({"block_materials": {}}), encoding="utf-8")
    report = {"unused_sidesets": [{"cubit_id": 8, "name": ""}, {"cubit_id": 9, "name": ""}],
              "overlaps": [{"sidesets": ["cut", "sym"], "faces": 3}]}
    fake = types.ModuleType("cubit_mesh_export.jmag")
    fake.export_jcf = lambda out, cfg: {"ok": True, **report}
    monkeypatch.setitem(sys.modules, "cubit_mesh_export.jmag", fake)
    shown = []
    _patch_dialogs(monkeypatch, menu, config, tmp_path / "m.jcf", shown)
    result = menu._run_jcf_export(None)
    assert result["unused_sidesets"] == report["unused_sidesets"]
    (title, text), = shown
    assert title == "JCF export: sidesets"
    assert "'' (id 8)" in text and "'' (id 9)" in text
    assert "Faces shared by sidesets ['cut', 'sym']: 3" in text
    assert "(id 8)" in capsys.readouterr().out


def test_comsol_menu_shows_unexported_sidesets_and_nothing_when_clean(monkeypatch):
    menu = _load_menu(monkeypatch)
    report = {"unexported_sidesets": [{"cubit_id": 22, "name": "empty"}],
              "overlaps": [{"sideset_ids": [21, 23], "file_indices": [4], "triangles": 10}]}
    fake = types.ModuleType("cubit_mesh_export.comsol")
    fake.export_mphtxt = lambda path, **kw: report
    monkeypatch.setitem(sys.modules, "cubit_mesh_export.comsol", fake)
    answers = iter([("mm", True), ("1 (TETRA4)", True)])
    dialog = type("QInputDialog", (), {"getItem": staticmethod(lambda *a: next(answers))})
    monkeypatch.setattr(sys.modules["PySide6.QtWidgets"], "QInputDialog", dialog, raising=False)
    shown = []
    _patch_dialogs(monkeypatch, menu, None, "m.mphtxt", shown)
    assert menu._run_comsol_export(None) == report
    (title, text), = shown
    assert title == "COMSOL export: sidesets"
    assert text.splitlines() == ["Sidesets with no exported face: 'empty' (id 22)",
                                 "Triangles shared by sidesets [21, 23]: 10 in 1 geometric entities"]
    assert menu.sideset_notes({"overlaps": [], "unexported_sidesets": []}) == []
    assert menu.sideset_notes(None) == []
