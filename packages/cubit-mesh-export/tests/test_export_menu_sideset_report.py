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
    lines = menu.sideset_notes({"unexported_sidesets": [{"cubit_id": 22, "name": "empty"}],
                                "overlaps": [{"sideset_ids": [21, 22], "file_indices": [4, 5]}]})
    assert lines == ["Sidesets with no exported face: 'empty' (id 22)",
                     "Faces shared by sidesets [21, 22]: 2"]
    assert menu.sideset_notes({"overlaps": [], "unexported_sidesets": []}) == []
    assert menu.sideset_notes(None) == []
