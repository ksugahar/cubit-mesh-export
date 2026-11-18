"""Guards: the Femtet PDT writer keeps nodes, HEX8 cells, block names and sideset faces.

Assumes: the record layout reproduced here was checked against Femtet readback
outside CI; this test decodes the file independently of the writer.
"""
import struct

import pytest

from cubit_mesh_export.femtet_pdt import encode_pdt

# Two unit cubes along x sharing the face x = 1.
POINTS = [(x, y, z) for x in (0.0, 1.0, 2.0) for y in (0.0, 1.0) for z in (0.0, 1.0)]


def node(x, y, z):
    return POINTS.index((x, y, z)) + 1


def hexa(x0):
    return tuple(node(x, y, z) for z in (0.0, 1.0)
                 for x, y in ((x0, 0.0), (x0 + 1, 0.0), (x0 + 1, 1.0), (x0, 1.0)))


LEFT, RIGHT = hexa(0.0), hexa(1.0)
INTERFACE = (node(1, 0, 0), node(1, 1, 0), node(1, 1, 1), node(1, 0, 1))
LEFT_END = (node(0, 0, 0), node(0, 0, 1), node(0, 1, 1), node(0, 1, 0))


def chunks(data):
    assert struct.unpack_from("<i", data)[0] == 101
    out, offset = {}, 4
    while offset < len(data):
        tag, size = struct.unpack_from("<5sQ", data, offset)
        offset += 13
        out[tag] = data[offset:offset + size]
        offset += size
    return out


def decode_mes(mes):
    ints = lambda at, n=1: struct.unpack_from(f"<{n}i", mes, at)
    offset = 8 + ints(4)[0]
    count = ints(offset)[0]
    points = [struct.unpack_from("<3d", mes, offset + 4 + 24 * i) for i in range(count)]
    offset += 4 + 24 * count
    tables = []
    for header in (1, 1, 1, 3, 1):
        rows, (n,) = [], ints(offset)
        offset += 4
        for _ in range(n):
            size = ints(offset + 4 * (header - 1))[0]
            rows.append(ints(offset + 4 * header, size) if header == 1 else ints(offset, header + size))
            offset += 4 * (header + size)
        tables.append(rows)
    tail = ints(offset, (len(mes) - offset) // 4)
    nodal = [tail[1 + 2 * i:3 + 2 * i] for i in range(tail[0])]
    at = 1 + 2 * tail[0]
    assert tail[at] == 0
    faces = [tail[at + 2 + 4 * i:at + 6 + 4 * i] for i in range(tail[at + 1])]
    return points, tables, nodal, faces


def test_two_blocks_round_trip_names_cells_and_faces():
    data = encode_pdt(POINTS, [("left", [LEFT]), ("right", [RIGHT])],
                      [("cut", [INTERFACE]), ("end", [LEFT_END])])
    parts = chunks(data)
    assert list(parts) == [b".mes\0", b".tbl\0", b".fac\0"]
    points, (edges, faces, solids, cells, regions), nodal, bound = decode_mes(parts[b".mes\0"])
    assert points == POINTS
    assert [c[3:] for c in cells] == [LEFT, RIGHT] and all(c[0] == 896 for c in cells)
    assert regions == [(1,), (2,)]
    assert (len(edges), len(faces)) == (20, 11)
    table = parts[b".tbl\0"].decode("ascii").split("\r\n")
    assert table[:6] == ["2", "left   Mat_1   5601   0", "right   Mat_2   5601   1", "2", "cut   0", "end   0"]
    assert sorted(nodal) == sorted([(n, 1) for n in INTERFACE] + [(n, 2) for n in LEFT_END])
    # One face record per sideset face: (mesh face, element, local side, sideset ordinal).
    loops = {i: {n for e in loop for n in edges[abs(e) - 1]} for i, loop in enumerate(faces, 1)}
    got = {(frozenset(loops[f]), cell, group) for f, cell, side, group in bound}
    assert got == {(frozenset(INTERFACE), 1, 1), (frozenset(LEFT_END), 1, 2)}
    for f, cell, side, _ in bound:
        assert abs(solids[cell - 1][side - 1]) == f


@pytest.mark.parametrize("sidesets, message", [
    ([("inside", [(1, 4, 9, 12)])], "outside the block boundaries"),
    ([("a", [INTERFACE]), ("b", [INTERFACE])], "more than one sideset"),
    ([("bad name", [INTERFACE])], "plain ASCII identifier"),
    ([("empty", [])], "no quad faces"),
])
def test_rejects_unsupported_sidesets(sidesets, message):
    with pytest.raises(ValueError, match=message):
        encode_pdt(POINTS, [("left", [LEFT]), ("right", [RIGHT])], sidesets)


def test_rejects_duplicate_block_names_and_bad_cells():
    with pytest.raises(ValueError, match="not unique"):
        encode_pdt(POINTS, [("b", [LEFT]), ("b", [RIGHT])], [])
    with pytest.raises(ValueError, match="degenerate"):
        encode_pdt(POINTS, [("b", [LEFT[:7] + LEFT[:1]])], [])


def test_project_export_writes_the_paired_mesh_and_refuses_reuse(tmp_path, monkeypatch):
    import sys
    import types

    from cubit_mesh_export import femtet_pdt

    monkeypatch.setitem(sys.modules, "cubit", types.ModuleType("cubit"))
    mesh = (POINTS, [("left", [LEFT]), ("right", [RIGHT])], [("cut", [INTERFACE])], [])
    monkeypatch.setattr(femtet_pdt, "read_active_mesh", lambda cubit: mesh)
    project = tmp_path / "case.femprj"
    result = femtet_pdt.export_femtet(project)
    paired = tmp_path / "case.Results" / (femtet_pdt.ANALYSIS_MODEL + ".pdt")
    assert paired.read_bytes() == encode_pdt(*mesh[:3])
    template = femtet_pdt.PROJECT_TEMPLATE.read_bytes()
    assert project.read_bytes() == template
    assert struct.unpack_from("<2i", template) == (200, 1), "not a Femtet-saved project"
    assert result["pdt"] == str(paired) and result["blocks"] == {"left": 1, "right": 1}
    with pytest.raises(FileExistsError):
        femtet_pdt.export_femtet(project)
    with pytest.raises(ValueError, match=".femprj"):
        femtet_pdt.export_femtet(tmp_path / "other.pdt")
