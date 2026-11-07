"""ASCII VTK XML UnstructuredGrid (.vtu) writer, with no Cubit or VTK dependency.

Cells come in Exodus node order, the order Cubit reports, and are written in VTK order.
Each cell carries ``GroupID`` (block or sideset id) and ``GroupType`` (0 block, 1 sideset),
the same cell data as the Legacy ``export vtk`` command. Coordinates are written with
round-trip precision.
"""
from __future__ import annotations

from pathlib import Path
from xml.sax.saxutils import quoteattr

import numpy as np

# (Exodus element family, node count) -> (VTK cell type, Exodus-to-VTK node order)
_ID = lambda n: tuple(range(n))  # noqa: E731
CELL_TYPES = {
    ("tet", 4): (10, _ID(4)),
    ("tet", 10): (24, _ID(10)),
    ("hex", 8): (12, _ID(8)),
    # Exodus HEX20 mid-edge order is bottom, vertical, top; VTK is bottom, top, vertical.
    ("hex", 20): (25, (*range(8), *range(8, 12), *range(16, 20), *range(12, 16))),
    # The VTK wedge base (0, 1, 2) faces away from the top; the Exodus base faces the top.
    # Swapping nodes 1 and 2 on both triangles reverses it, and the mid-edge nodes follow:
    # VTK edges 01 12 20 34 45 53 03 14 25 are Exodus 02 21 10 35 54 43 03 25 14.
    ("wedge", 6): (13, (0, 2, 1, 3, 5, 4)),
    ("wedge", 15): (26, (0, 2, 1, 3, 5, 4, 8, 7, 6, 14, 13, 12, 9, 11, 10)),
    ("pyramid", 5): (14, _ID(5)),
    ("pyramid", 13): (27, _ID(13)),
    ("tri", 3): (5, _ID(3)),
    ("tri", 6): (22, _ID(6)),
    ("quad", 4): (9, _ID(4)),
    ("quad", 8): (23, _ID(8)),
}
GROUP_BLOCK, GROUP_SIDESET = 0, 1


class VtuError(ValueError):
    pass


def vtk_cell(family: str, connectivity) -> tuple[int, tuple]:
    """Return the VTK cell type and the connectivity in VTK order."""
    conn = tuple(connectivity)
    key = (family, len(conn))
    if key not in CELL_TYPES:
        supported = ", ".join(f"{f}{n}" for f, n in sorted(CELL_TYPES))
        raise VtuError(f"{family} with {len(conn)} nodes is not supported (supported: {supported})")
    vtk_type, order = CELL_TYPES[key]
    return vtk_type, tuple(conn[i] for i in order)


def _ascii(values, fmt) -> str:
    return "\n".join(" ".join(fmt(v) for v in row) for row in values)


def write_vtu(path, nodes: dict, cells) -> dict:
    """Write ``nodes`` {id: (x, y, z)} and ``cells`` [(family, node ids, group id, group type)].

    Node ids may be any integers; only nodes that cells use are written, in ascending id order.
    """
    if not cells:
        raise VtuError("no cells to write")
    vtk = []
    for family, conn, group_id, group_type in cells:
        if group_type not in (GROUP_BLOCK, GROUP_SIDESET):
            raise VtuError(f"group type must be {GROUP_BLOCK} or {GROUP_SIDESET}, got {group_type!r}")
        vtk.append((*vtk_cell(family, conn), int(group_id), int(group_type)))
    used = sorted({n for _, conn, _, _ in vtk for n in conn})
    missing = [n for n in used if n not in nodes]
    if missing:
        raise VtuError(f"cells refer to missing nodes, e.g. {missing[:5]}")
    index = {n: i for i, n in enumerate(used)}
    xyz = np.array([nodes[n] for n in used], float).reshape(len(used), 3)
    if not np.isfinite(xyz).all():
        raise VtuError("node coordinates must be finite")
    connectivity = [[index[n] for n in conn] for _, conn, _, _ in vtk]
    offsets = np.cumsum([len(c) for c in connectivity])
    rec = lambda v: repr(float(v))  # noqa: E731  shortest round-trip text
    num = lambda v: str(int(v))  # noqa: E731
    text = (
        '<?xml version="1.0"?>\n'
        '<VTKFile type="UnstructuredGrid" version="1.0" byte_order="LittleEndian" header_type="UInt64">\n'
        "<UnstructuredGrid>\n"
        f'<Piece NumberOfPoints="{len(used)}" NumberOfCells="{len(vtk)}">\n'
        "<Points>\n"
        '<DataArray type="Float64" NumberOfComponents="3" format="ascii">\n'
        f"{_ascii(xyz, rec)}\n"
        "</DataArray>\n"
        "</Points>\n"
        "<Cells>\n"
        '<DataArray type="Int64" Name="connectivity" format="ascii">\n'
        f"{_ascii(connectivity, num)}\n"
        "</DataArray>\n"
        '<DataArray type="Int64" Name="offsets" format="ascii">\n'
        f"{_ascii([offsets], num)}\n"
        "</DataArray>\n"
        '<DataArray type="UInt8" Name="types" format="ascii">\n'
        f"{_ascii([[t for t, _, _, _ in vtk]], num)}\n"
        "</DataArray>\n"
        "</Cells>\n"
        f"<CellData Scalars={quoteattr('GroupID')}>\n"
        '<DataArray type="Int32" Name="GroupID" format="ascii">\n'
        f"{_ascii([[g for _, _, g, _ in vtk]], num)}\n"
        "</DataArray>\n"
        '<DataArray type="Int32" Name="GroupType" format="ascii">\n'
        f"{_ascii([[k for _, _, _, k in vtk]], num)}\n"
        "</DataArray>\n"
        "</CellData>\n"
        "</Piece>\n"
        "</UnstructuredGrid>\n"
        "</VTKFile>\n"
    )
    Path(path).write_text(text, encoding="ascii", newline="\n")
    types = {}
    for t, _, _, k in vtk:
        types[(t, k)] = types.get((t, k), 0) + 1
    return {"path": str(path), "points": len(used), "cells": len(vtk),
            "cell_types": {f"{t}/{'block' if k == GROUP_BLOCK else 'sideset'}": n
                           for (t, k), n in sorted(types.items())}}
