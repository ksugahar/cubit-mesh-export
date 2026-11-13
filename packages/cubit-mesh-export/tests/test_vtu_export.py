"""ASCII VTK XML (.vtu) export and the model-transfer dispatch boundary, without Cubit.

Guards: the writer stays standalone, writes parseable ASCII XML whose arrays agree with each
other, keeps coordinates bit-exact, reorders Exodus higher-order nodes into VTK order with VTK
orientation (mid nodes on the VTK edges, faces outward), and fails loudly on unsupported or
inconsistent input; the Cubit reader takes block elements through geometry, writes sideset faces
as GroupType 1 and rejects double or missing block membership; model transfer refuses to run
unless exactly one handler is installed.
Assumes: Cubit reports Exodus node order (checked against Cubit 2026.8 and VTK 9.3 outside this
repository); the VTK edge and face tables below are copied from VTK's cell definitions.
"""
from __future__ import annotations

import ast
import importlib
import sys
import types
import xml.etree.ElementTree as ET
from pathlib import Path

import numpy as np
import pytest

SRC = Path(__file__).resolve().parents[1] / "src" / "cubit_mesh_export"

# VTK cell definitions: mid-edge node k lies on VTK_EDGES[type][k]; VTK_FACES lists corner
# loops whose right-hand normal points out of the cell.
VTK_EDGES = {
    24: [(0, 1), (1, 2), (2, 0), (0, 3), (1, 3), (2, 3)],
    25: [(0, 1), (1, 2), (2, 3), (3, 0), (4, 5), (5, 6), (6, 7), (7, 4),
         (0, 4), (1, 5), (2, 6), (3, 7)],
    26: [(0, 1), (1, 2), (2, 0), (3, 4), (4, 5), (5, 3), (0, 3), (1, 4), (2, 5)],
    27: [(0, 1), (1, 2), (2, 3), (3, 0), (0, 4), (1, 4), (2, 4), (3, 4)],
}
VTK_FACES = {
    10: [(0, 1, 3), (1, 2, 3), (2, 0, 3), (0, 2, 1)],
    12: [(0, 4, 7, 3), (1, 2, 6, 5), (0, 1, 5, 4), (3, 7, 6, 2), (0, 3, 2, 1), (4, 5, 6, 7)],
    13: [(0, 1, 2), (3, 5, 4), (0, 3, 4, 1), (1, 4, 5, 2), (2, 5, 3, 0)],
    14: [(0, 3, 2, 1), (0, 1, 4), (1, 2, 4), (2, 3, 4), (3, 0, 4)],
}
LINEAR = {24: 10, 25: 12, 26: 13, 27: 14, 10: 10, 12: 12, 13: 13, 14: 14}

# Exodus reference elements (corner nodes, then mid-edge nodes on the Exodus edges).
EXO_CORNERS = {
    "tet": [(0, 0, 0), (1, 0, 0), (0, 1, 0), (0, 0, 1)],
    "hex": [(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0), (0, 0, 1), (1, 0, 1), (1, 1, 1), (0, 1, 1)],
    "wedge": [(0, 0, 0), (1, 0, 0), (0, 1, 0), (0, 0, 1), (1, 0, 1), (0, 1, 1)],
    "pyramid": [(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0), (0.5, 0.5, 1)],
}
EXO_EDGES = {
    "tet": [(0, 1), (1, 2), (2, 0), (0, 3), (1, 3), (2, 3)],
    "hex": [(0, 1), (1, 2), (2, 3), (3, 0), (0, 4), (1, 5), (2, 6), (3, 7),
            (4, 5), (5, 6), (6, 7), (7, 4)],
    "wedge": [(0, 1), (1, 2), (2, 0), (0, 3), (1, 4), (2, 5), (3, 4), (4, 5), (5, 3)],
    "pyramid": [(0, 1), (1, 2), (2, 3), (3, 0), (0, 4), (1, 4), (2, 4), (3, 4)],
}


@pytest.fixture(scope="module")
def mods():
    pkg = types.ModuleType("_cme_vtu")
    pkg.__path__ = [str(SRC)]
    sys.modules["_cme_vtu"] = pkg
    try:
        yield tuple(importlib.import_module(f"_cme_vtu.{n}") for n in ("vtu_writer", "vtu", "model_transfer"))
    finally:
        for name in [n for n in sys.modules if n == "_cme_vtu" or n.startswith("_cme_vtu.")]:
            del sys.modules[name]


def _reference(family, quadratic):
    pts = [np.array(p, float) for p in EXO_CORNERS[family]]
    if quadratic:
        pts += [(pts[a] + pts[b]) / 2 for a, b in EXO_EDGES[family]]
    return {i + 1: tuple(p) for i, p in enumerate(pts)}


def _read(path):
    root = ET.parse(path).getroot()
    assert root.attrib["type"] == "UnstructuredGrid"
    arrays = {}
    for da in root.iter("DataArray"):
        assert da.attrib["format"] == "ascii", da.attrib
        arrays[da.attrib.get("Name", "Points")] = da.text.split()
    piece = root.find(".//Piece").attrib
    xyz = np.array(arrays["Points"], float).reshape(-1, 3)
    conn = [int(v) for v in arrays["connectivity"]]
    offsets = [int(v) for v in arrays["offsets"]]
    cells = [conn[a:b] for a, b in zip([0, *offsets[:-1]], offsets)]
    assert len(xyz) == int(piece["NumberOfPoints"]) and len(cells) == int(piece["NumberOfCells"])
    return xyz, cells, [int(v) for v in arrays["types"]], arrays


def test_writer_is_standalone():
    for name in ("vtu_writer.py", "vtu.py", "model_transfer.py"):
        tree = ast.parse((SRC / name).read_text(encoding="utf-8"))
        imported = {a.name.split(".")[0] for n in ast.walk(tree) if isinstance(n, ast.Import) for a in n.names}
        imported |= {n.module.split(".")[0] for n in ast.walk(tree)
                     if isinstance(n, ast.ImportFrom) and n.module and n.level == 0}
        assert imported <= {"__future__", "pathlib", "xml", "numpy", "importlib", "json", "cubit"}, (name, imported)


@pytest.mark.parametrize("family", sorted(EXO_CORNERS))
@pytest.mark.parametrize("quadratic", [False, True])
def test_cells_are_written_in_vtk_order_and_orientation(mods, tmp_path, family, quadratic):
    vw, _, _ = mods
    nodes = _reference(family, quadratic)
    vw.write_vtu(tmp_path / "c.vtu", nodes, [(family, list(nodes), 1, vw.GROUP_BLOCK)])
    xyz, cells, types_, _ = _read(tmp_path / "c.vtu")
    p, t = xyz[cells[0]], types_[0]
    for k, (a, b) in enumerate(VTK_EDGES.get(t, [])):
        assert np.allclose(p[len(EXO_CORNERS[family]) + k], (p[a] + p[b]) / 2), (t, k)
    centre = p[:len(EXO_CORNERS[family])].mean(axis=0)
    for face in VTK_FACES[LINEAR[t]]:
        q = p[list(face)]
        area = sum(np.cross(q[j] - q[0], q[j + 1] - q[0]) for j in range(1, len(q) - 1))
        assert area @ (q.mean(axis=0) - centre) > 0, (t, face)


def test_arrays_agree_and_coordinates_round_trip(mods, tmp_path):
    vw, _, _ = mods
    nodes = {10: (0.1, 1 / 3, -2e-17), 20: (1e300, 0, 5), 30: (0, 1, 0), 40: (7, 7, 7), 99: (9, 9, 9)}
    r = vw.write_vtu(tmp_path / "a.vtu", nodes, [("tri", (10, 20, 30), 4, vw.GROUP_BLOCK),
                                                 ("quad", (10, 20, 40, 30), 8, vw.GROUP_SIDESET)])
    xyz, cells, types_, arrays = _read(tmp_path / "a.vtu")
    assert xyz.tolist() == [list(nodes[n]) for n in (10, 20, 30, 40)]    # unused node 99 dropped
    assert cells == [[0, 1, 2], [0, 1, 3, 2]] and types_ == [5, 9]
    assert arrays["GroupID"] == ["4", "8"] and arrays["GroupType"] == ["0", "1"]
    assert r == {"path": str(tmp_path / "a.vtu"), "points": 4, "cells": 2,
                 "cell_types": {"5/block": 1, "9/sideset": 1}}
    (tmp_path / "a.vtu").read_bytes().decode("ascii")


@pytest.mark.parametrize("cells, match", [
    ([], "no cells"),
    ([("hex", range(1, 28), 1, 0)], "hex with 27 nodes is not supported"),
    ([("tri", (1, 2, 5), 1, 0)], "missing nodes"),
    ([("tri", (1, 2, 3), 1, 2)], "group type"),
    ([("tri", (1, 2, 4), 1, 0)], "finite"),
])
def test_writer_fails_loudly(mods, tmp_path, cells, match):
    vw, _, _ = mods
    nodes = {1: (0, 0, 0), 2: (1, 0, 0), 3: (0, 1, 0), 4: (np.nan, 0, 0), **{i: (i, 0, 0) for i in range(6, 28)}}
    with pytest.raises(vw.VtuError, match=match):
        vw.write_vtu(tmp_path / "x.vtu", nodes, cells)


class FakeCubit:
    """Two hexes in volume 1 (block 1 by volume), one quad in surface 5 (sideset 7 by surface)."""

    def __init__(self, blocks=None, sidesets=None, extra_hex=False, meshed=True, quads=(3,)):
        self.blocks = blocks if blocks is not None else {1: {"volumes": [1]}}
        self.sidesets = sidesets if sidesets is not None else {7: {"surfaces": [5]}}
        self.conn = {("hex", 1): tuple(range(1, 9)), ("hex", 2): tuple(range(5, 13)),
                     ("quad", 3): (1, 2, 3, 4), ("quad", 4): (5, 6, 7, 8), ("tri", 9): (1, 2, 5)}
        self.hexes = ([1, 2, 3] if extra_hex else [1, 2]) if meshed else []
        self.quads = list(quads)

    def __getattr__(self, name):
        owner, _, what = name[4:].partition("_")
        table = {"block": self.blocks, "sideset": self.sidesets}.get(owner)
        if table is not None:
            return lambda i: table[i].get(what, [])
        geometry = {("volume", "hexes"): {1: [1, 2]}, ("surface", "quads"): {5: [3]},
                    ("surface", "tris"): {6: [9]}}
        return lambda i: geometry.get((owner, what), {}).get(i, [])

    def get_block_id_list(self):
        return list(self.blocks)

    def get_sideset_id_list(self):
        return list(self.sidesets)

    def get_entities(self, kind):
        return {"hex": self.hexes, "face": self.quads}.get(kind, [])   # Cubit lists quads as "face"

    def get_expanded_connectivity(self, kind, i):
        return self.conn[(kind, i)]

    def get_nodal_coordinates(self, n):
        return (float(n), 0.0, 0.0)


def test_reader_takes_blocks_through_geometry_and_sidesets(mods):
    vw, vtu, _ = mods
    nodes, cells = vtu.read_active_cells(FakeCubit())
    assert [(k, tuple(c), g, t) for k, c, g, t in cells] == [
        ("hex", tuple(range(1, 9)), 1, vw.GROUP_BLOCK), ("hex", tuple(range(5, 13)), 1, vw.GROUP_BLOCK),
        ("quad", (1, 2, 3, 4), 7, vw.GROUP_SIDESET)]
    assert sorted(nodes) == list(range(1, 13))


def test_reader_counts_each_element_once_and_takes_block_surfaces(mods):
    vw, vtu, _ = mods
    cubit = FakeCubit(blocks={1: {"volumes": [1], "hexes": [2]}, 3: {"surfaces": [6]}}, sidesets={})
    _, cells = vtu.read_active_cells(cubit)
    assert [(k, g) for k, _, g, _ in cells] == [("hex", 1), ("hex", 1), ("tri", 3)]


def test_reader_writes_a_surface_mesh_and_rejects_its_loose_faces(mods):
    vw, vtu, _ = mods
    surface = dict(blocks={2: {"faces": [3, 4]}}, sidesets={}, meshed=False, quads=(3, 4))
    _, cells = vtu.read_active_cells(FakeCubit(**surface))
    assert [(k, tuple(c), g) for k, c, g, _ in cells] == [("quad", (1, 2, 3, 4), 2), ("quad", (5, 6, 7, 8), 2)]
    surface["blocks"] = {2: {"faces": [3]}}
    with pytest.raises(vw.VtuError, match="quad 4 has no block assignment"):
        vtu.read_active_cells(FakeCubit(**surface))


@pytest.mark.parametrize("cubit, match", [
    (FakeCubit(blocks={}), "no blocks"),
    (FakeCubit(blocks={1: {"volumes": [1]}, 2: {"hexes": [2]}}), "belongs to blocks 1 and 2"),
    (FakeCubit(extra_hex=True), "hex 3 has no block assignment"),
    (FakeCubit(blocks={1: {}}, sidesets={}, meshed=False, quads=()), "hold no elements"),
    (FakeCubit(sidesets={8: {}}), "sideset 8 has no tri or quad faces"),
    (FakeCubit(blocks={1: {"volumes": [1], "edges": [5]}}), "block 1 holds edges"),
    (FakeCubit(blocks={1: {"volumes": [1]}, 2: {"nodes": [3]}}), "block 2 holds nodes"),
    (FakeCubit(sidesets={7: {"surfaces": [5], "curves": [1]}}), "sideset 7 holds curves"),
])
def test_reader_fails_loudly(mods, cubit, match):
    vw, vtu, _ = mods
    with pytest.raises(vw.VtuError, match=match):
        vtu.read_active_cells(cubit)


class _EntryPoint:
    def __init__(self, name, value, obj):
        self.name, self.value, self._obj = name, value, obj

    def load(self):
        return self._obj


@pytest.mark.parametrize("installed, match", [
    ([], "No handler is installed for 'femtet'"),
    ([("femtet", "a:run"), ("femtet", "b:run")], "2 handlers are installed"),
    ([("other", "a:run")], "No handler is installed"),
])
def test_transfer_refuses_without_exactly_one_handler(mods, monkeypatch, installed, match):
    _, _, mt = mods
    calls = []
    eps = [_EntryPoint(n, v, lambda cubit: calls.append(cubit) or {}) for n, v in installed]
    monkeypatch.setattr(mt, "entry_points", lambda group: eps if group == mt.GROUP else [])
    with pytest.raises(mt.TransferUnavailable, match=match):
        mt.transfer("femtet", object())
    assert calls == []


def test_transfer_runs_the_one_handler(mods, monkeypatch):
    _, _, mt = mods
    cubit = object()
    eps = [_EntryPoint("femtet", "lab:run", lambda c: {"saved": True, "same": c is cubit})]
    monkeypatch.setattr(mt, "entry_points", lambda group: eps if group == mt.GROUP else [])
    assert mt.describe("femtet") == "lab:run"
    assert mt.transfer("femtet", cubit) == {"saved": True, "same": True}
    eps[0]._obj = lambda c, *, element_order: {"order": element_order}
    assert mt.transfer("femtet", cubit, element_order=2) == {"order": 2}
    eps[0]._obj = lambda c: "done"
    with pytest.raises(TypeError, match="expected dict"):
        mt.transfer("femtet", cubit)
    eps[0]._obj = lambda c: {"handle": object()}
    with pytest.raises(TypeError, match="not JSON serializable"):
        mt.transfer("femtet", cubit)
