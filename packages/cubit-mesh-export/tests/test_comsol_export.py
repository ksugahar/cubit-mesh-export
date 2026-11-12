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
