"""Guard quadratic edge ordering and shared-interface node identity."""
from itertools import combinations

import pytest

from cubit_mesh_export.comsol import encode_mesh


def test_quadratic_webcut_interface():
    points = {
        1: (0, 0, 0), 2: (1, 0, 0), 3: (0, 1, 0),
        4: (0, 0, 1), 5: (0, 0, -1),
        6: (0.5, 0, 0.125), 7: (0.5, 0.5, 0), 8: (0, 0.5, 0),
        9: (0, 0, 0.5), 10: (0.5, 0, 0.5), 11: (0, 0.5, 0.5),
        12: (0, 0, -0.5), 13: (0.5, 0, -0.5), 14: (0, 0.5, -0.5),
    }
    top = (1, 2, 3, 4, 6, 7, 8, 9, 10, 11)
    bottom = (1, 3, 2, 5, 8, 7, 6, 12, 14, 13)
    cells = [
        ("tet", top, 4, 0),
        ("tet", bottom, 9, 0),
        ("tri", (1, 2, 3), 21, 1),
        ("tri", (1, 2, 5), 22, 1),
        ("tri", (1, 2, 4), 23, 1),
    ]
    surface_faces = {
        face: i for i, face in enumerate(sorted({
            face for corners in ((1, 2, 3, 4), (1, 2, 3, 5))
            for face in combinations(corners, 3)
        }), start=30)
    }
    names = {21: "webcut_interface", 22: "fixed_end", 23: "loaded_end"}
    args = (surface_faces, {4: "top", 9: "bottom"}, names)
    text, manifest = encode_mesh(points, cells, *args, meters_per_unit=1)
    assert "\n0.5 0 0.125\n" in text
    assert "\n0 1 2 3 5 7 6 8 9 10\n0 2 1 4 7 5 6 11 13 12\n" in text
    assert "\n0 1 2 5 7 6\n" in text
    selections = {row["name"]: row["file_indices"] for row in manifest["sidesets"]}
    assert set(selections) == set(names.values())
    for name, face, blocks in (
        ("webcut_interface", (1, 2, 3), [4, 9]),
        ("fixed_end", (1, 2, 5), [9]),
        ("loaded_end", (1, 2, 4), [4]),
    ):
        index, = selections[name]
        assert manifest["surfaces"][index]["adjacent_blocks"] == blocks
        assert manifest["surfaces"][index]["cubit_id"] == surface_faces[face]

    points[15] = points[6]
    cells[1] = ("tet", (*bottom[:6], 15, *bottom[7:]), 9, 0)
    with pytest.raises(ValueError, match="nonconforming quadratic node"):
        encode_mesh(points, cells, *args, meters_per_unit=1)


def test_volumes_sharing_a_block_are_refused():
    points = {
        1: (0, 0, 0), 2: (1, 0, 0), 3: (0, 1, 0), 4: (0, 0, 1), 5: (0, 0, -1),
        6: (0.5, 0, 0), 7: (0.5, 0.5, 0), 8: (0, 0.5, 0),
        9: (0, 0, 0.5), 10: (0.5, 0, 0.5), 11: (0, 0.5, 0.5),
        12: (0, 0, -0.5), 13: (0.5, 0, -0.5), 14: (0, 0.5, -0.5),
    }
    cells = [
        ("tet", (1, 2, 3, 4, 6, 7, 8, 9, 10, 11), 4, 0),
        ("tet", (1, 3, 2, 5, 8, 7, 6, 12, 14, 13), 4, 0),
    ]
    # The webcut surface (1, 2, 3) is a CAD surface inside one block.
    surface_faces = {
        face: i for i, face in enumerate(sorted({
            face for corners in ((1, 2, 3, 4), (1, 2, 3, 5))
            for face in combinations(corners, 3)
        }), start=30)
    }
    with pytest.raises(ValueError, match="put each volume in its own block"):
        encode_mesh(points, cells, surface_faces, {4: "both"}, {}, meters_per_unit=1)


def test_linear_webcut_preserves_interface_and_sideset():
    points = {1: (0, 0, 0), 2: (1, 0, 0), 3: (0, 1, 0),
              4: (0, 0, 1), 5: (0, 0, -1)}
    cells = [("tet", (1, 2, 3, 4), 4, 0), ("tet", (1, 3, 2, 5), 9, 0),
             ("tri", (1, 2, 3), 21, 1)]
    faces = {face: i for i, face in enumerate(sorted(
        set(combinations((1, 2, 3, 4), 3)) | set(combinations((1, 2, 3, 5), 3))))}
    args = (points, cells, faces, {4: "top", 9: "bottom"}, {21: "interface"})
    text, manifest = encode_mesh(*args, meters_per_unit=1, order=1)
    assert "\n3 tet # type name\n4\n2\n# Elements\n0 1 2 3\n0 2 1 4\n" in text
    assert "\n3 tri # type name\n3\n7\n" in text
    index = manifest["sidesets"][0]["file_indices"][0]
    interface = next(row for row in manifest["surfaces"] if row["file_index"] == index)
    assert interface["adjacent_blocks"] == [4, 9]
    with pytest.raises(ValueError, match="order 2 requires TETRA10"):
        encode_mesh(*args, meters_per_unit=1, order=2)

    quadratic_side = [*cells[:2], ("tri", (1, 2, 3, 4, 5, 1), 21, 1)]
    with pytest.raises(ValueError, match="Order 1 requires linear sidesets"):
        encode_mesh(points, quadratic_side, *args[2:], meters_per_unit=1, order=1)

    quadratic_block = [("tet", tuple(range(1, 11)), 4, 0), *cells[1:]]
    quadratic_points = points | {i: (0, 0, 0) for i in range(6, 11)}
    with pytest.raises(ValueError, match="order 1 requires TETRA4"):
        encode_mesh(quadratic_points, quadratic_block, *args[2:], meters_per_unit=1, order=1)


def _two_tets():
    points = {1: (0, 0, 0), 2: (1, 0, 0), 3: (0, 1, 0), 4: (0, 0, 1), 5: (0, 0, -1)}
    cells = [("tet", (1, 2, 3, 4), 4, 0), ("tet", (1, 3, 2, 5), 9, 0),
             ("tri", (1, 2, 4), 21, 1), ("tri", (1, 2, 4), 22, 1), ("tri", (1, 2, 5), 22, 1)]
    surface_faces = {face: i for i, face in enumerate(sorted({
        face for corners in ((1, 2, 3, 4), (1, 2, 3, 5)) for face in combinations(corners, 3)}), start=30)}
    return points, cells, surface_faces


def test_overlaps_and_empty_sidesets_are_reported_not_dropped():
    points, cells, surface_faces = _two_tets()
    names = {21: "load", 22: "wall", 23: "unused"}
    _text, manifest = encode_mesh(points, cells, surface_faces, {4: "top", 9: "bottom"}, names,
                                  meters_per_unit=1, order=1)
    overlap, = manifest["overlaps"]
    assert overlap["sideset_ids"] == [21, 22]
    index, = overlap["file_indices"]
    by_name = {row["name"]: row["file_indices"] for row in manifest["sidesets"]}
    assert index in by_name["load"] and index in by_name["wall"] and len(by_name["wall"]) == 2
    assert manifest["unexported_sidesets"] == [{"cubit_id": 23, "name": "unused"}]


@pytest.mark.parametrize("names, match", [
    ({21: "load", 22: "load"}, "not unique"),
    ({21: "load", 22: " "}, "no name"),
])
def test_ambiguous_sideset_names_are_refused(names, match):
    points, cells, surface_faces = _two_tets()
    with pytest.raises(ValueError, match=match):
        encode_mesh(points, cells, surface_faces, {4: "top", 9: "bottom"}, names, meters_per_unit=1, order=1)


class _MeshCubit:
    """Two linear tets in blocks 4 and 9 for export_mphtxt; anything not modelled is empty."""

    def __init__(self, sidesets, sideset_curves=()):
        self.coords = {1: (0, 0, 0), 2: (1, 0, 0), 3: (0, 1, 0), 4: (0, 0, 1), 5: (0, 0, -1)}
        self.tets = {1: (1, 2, 3, 4), 2: (1, 3, 2, 5)}
        faces = sorted({f for t in self.tets.values() for f in combinations(sorted(t), 3)})
        self.tris = {i: face for i, face in enumerate(faces, start=1)}
        self.surface_of = {i: 30 + i for i in self.tris}
        self.sidesets, self.sideset_curves = sidesets, set(sideset_curves)

    def __getattr__(self, name):
        return lambda *args: []

    def get_block_id_list(self):
        return [4, 9]

    def get_sideset_id_list(self):
        return list(self.sidesets)

    def get_block_tets(self, block):
        return [1] if block == 4 else [2]

    def get_sideset_tris(self, sid):
        return list(self.sidesets[sid][1])

    def get_sideset_curves(self, sid):
        return [7] if sid in self.sideset_curves else []

    def get_entities(self, kind):
        return {"tet": list(self.tets), "surface": list(self.surface_of.values())}.get(kind, [])

    def get_surface_tris(self, surface):
        return [t for t, s in self.surface_of.items() if s == surface]

    def get_expanded_connectivity(self, kind, i):
        return self.tets[i] if kind == "tet" else self.tris[i]

    def get_nodal_coordinates(self, node):
        return self.coords[node]

    def get_exodus_entity_name(self, kind, i):
        return {4: "top", 9: "bottom"}[i] if kind == "block" else self.sidesets[i][0]


def _export(monkeypatch, tmp_path, cubit):
    import sys
    from cubit_mesh_export import comsol
    monkeypatch.setitem(sys.modules, "cubit", cubit)
    return comsol.export_mphtxt(tmp_path / "m.mphtxt", meters_per_unit=1, order=1)


def test_normal_export_reports_an_empty_sideset(monkeypatch, tmp_path):
    cubit = _MeshCubit({21: ("load", [])})
    load = next(t for t, face in cubit.tris.items() if face == (1, 2, 4))
    cubit.sidesets = {21: ("load", [load]), 22: ("empty", [])}
    manifest = _export(monkeypatch, tmp_path, cubit)
    assert [row["name"] for row in manifest["sidesets"]] == ["load"]
    assert manifest["unexported_sidesets"] == [{"cubit_id": 22, "name": "empty"}]
    assert (tmp_path / "m.mphtxt").is_file()


def test_normal_export_still_refuses_non_face_members(monkeypatch, tmp_path):
    cubit = _MeshCubit({22: ("edge_set", [])}, sideset_curves={22})
    with pytest.raises(Exception, match="holds curves"):
        _export(monkeypatch, tmp_path, cubit)
    assert not (tmp_path / "m.mphtxt").exists()


def test_vtu_still_refuses_an_empty_sideset():
    from cubit_mesh_export import vtu
    with pytest.raises(Exception, match="no tri or quad faces"):
        vtu.read_active_cells(_MeshCubit({22: ("empty", [])}))


def test_a_face_repeated_in_one_sideset_is_refused_but_two_sidesets_may_share_it():
    points, cells, surface_faces = _two_tets()
    names = {21: "load", 22: "wall"}
    repeated = cells + [("tri", (2, 4, 1), 21, 1)]   # same corners, other order
    with pytest.raises(ValueError, match="sideset 21 lists face"):
        encode_mesh(points, repeated, surface_faces, {4: "top", 9: "bottom"}, names,
                    meters_per_unit=1, order=1)
    _text, manifest = encode_mesh(points, cells, surface_faces, {4: "top", 9: "bottom"}, names,
                                  meters_per_unit=1, order=1)
    assert manifest["overlaps"][0]["sideset_ids"] == [21, 22]
