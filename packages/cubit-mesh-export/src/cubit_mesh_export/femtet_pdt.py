"""Write a linear HEX8 Cubit mesh as a Femtet mesh file (.pdt).

The file carries the mesh, the block names as body attributes and the
sidesets as named boundaries (node and face membership). Analysis conditions
are not written: Femtet keeps them in the project (.femprj), so they can be
set or changed after the mesh is loaded. Coordinates are written unchanged in
the Cubit model unit.
"""
from __future__ import annotations

from collections import Counter
import hashlib
from itertools import pairwise
import math
from pathlib import Path
import re
import struct

_EDGES = ((0, 1), (1, 2), (2, 3), (3, 0), (4, 5), (5, 6), (6, 7), (7, 4),
          (0, 4), (1, 5), (2, 6), (3, 7))
_FACES = ((0, 1, 2, 3), (4, 7, 6, 5), (0, 4, 5, 1), (1, 5, 6, 2), (2, 6, 7, 3), (3, 7, 4, 0))
_NAME = re.compile(r"[A-Za-z_][A-Za-z_0-9]{0,30}")
_HEX8 = 896
# Every body gets this material; the exported project defines it as air.
MATERIAL = "Air"


def _ints(*values):
    return struct.pack(f"<{len(values)}i", *values)


def _records(rows):
    return _ints(len(rows)) + b"".join(_ints(len(row), *row) for row in rows)


def _signed(loop, registry):
    def rotations(seq):
        return {seq[i:] + seq[:i] for i in range(len(seq))}
    forward = rotations(loop)
    key = min(forward | rotations(loop[::-1]))
    number, original = registry.setdefault(key, (len(registry) + 1, loop))
    return number if original in forward else -number


def _check_names(kind, names):
    for name in names:
        if not _NAME.fullmatch(name):
            raise ValueError(f"{kind} name {name!r} is not a plain ASCII identifier of at most 31 characters")
    repeated = sorted(n for n, c in Counter(names).items() if c > 1)
    if repeated:
        raise ValueError(f"{kind} names are not unique: {repeated}")


def encode_pdt(points, blocks, sidesets):
    """Return the .pdt bytes.

    ``points``: coordinates of nodes 1..N. ``blocks``: ``[(name, [hex8, ...])]``
    with 1-based node indices in Exodus HEX8 order. ``sidesets``:
    ``[(name, [quad, ...])]``; every quad must be a boundary face of a block
    (exterior or between blocks), and a face may belong to one sideset only.
    """
    if any(len(p) != 3 or not all(map(math.isfinite, p)) for p in points):
        raise ValueError("node coordinates must be finite 3D points")
    if not blocks:
        raise ValueError("no HEX8 blocks")
    _check_names("block", [name for name, _ in blocks])
    _check_names("sideset", [name for name, _ in sidesets])

    edges, faces, loops, solids, cells, regions = {}, {}, {}, [], [], []
    exterior = []
    for _, group in blocks:
        if not group:
            raise ValueError("a block has no HEX8 elements")
        group_faces, group_loops, region = Counter(), {}, []
        for nodes in group:
            nodes = tuple(nodes)
            if len(nodes) != 8 or len(set(nodes)) != 8:
                raise ValueError(f"degenerate or non-HEX8 element {nodes}")
            if min(nodes) < 1 or max(nodes) > len(points):
                raise ValueError("HEX8 connectivity references an absent node")
            cells.append(nodes)
            region.append(len(cells))
            for a, b in _EDGES:
                e = (nodes[a], nodes[b])
                edges.setdefault(tuple(sorted(e)), (len(edges) + 1, e))
            incident = []
            for face in _FACES:
                loop = tuple(nodes[i] for i in face)
                sign = _signed(loop, faces)
                incident.append(sign)
                group_faces[abs(sign)] += 1
                group_loops[abs(sign)] = loop
                if sign > 0 and sign not in loops:
                    linked = []
                    for a, b in pairwise(loop + loop[:1]):
                        k, direction = edges[tuple(sorted((a, b)))]
                        linked.append(k if direction == (a, b) else -k)
                    loops[sign] = linked
            solids.append(incident)
        if max(group_faces.values()) > 2:
            raise ValueError("nonmanifold HEX8 face")
        regions.append(region)
        exterior.extend(group_loops[k] for k, count in group_faces.items() if count == 1)
    edge_list = [edge for _, edge in edges.values()]

    # Region-local boundary faces; a face between two blocks has two owners.
    owners = {}
    for cell_ids in regions:
        counts = Counter(abs(f) for c in cell_ids for f in solids[c - 1])
        for c in cell_ids:
            for side, f in enumerate(solids[c - 1], 1):
                if counts[abs(f)] == 1:
                    owners.setdefault(abs(f), []).append((c, side))
    face_index = {tuple(sorted({n for e in loop for n in edge_list[abs(e) - 1]})): i
                  for i, loop in loops.items()}
    node_bindings, face_bindings = set(), {}
    for group, (name, quads) in enumerate(sidesets, 1):
        if not quads:
            raise ValueError(f"sideset {name!r} has no quad faces")
        for quad in quads:
            index = face_index.get(tuple(sorted(quad)))
            if index not in owners:
                raise ValueError(f"sideset {name!r} has a face outside the block boundaries: {tuple(quad)}")
            if index in face_bindings:
                raise ValueError(f"a face {tuple(quad)} belongs to more than one sideset")
            face_bindings[index] = group
            node_bindings.update((node, group) for node in quad)

    corners, corner_edges, facets = [], [], []
    region_facets = [[] for _ in regions]
    region_of = {c: r for r, ids in enumerate(regions) for c in ids}
    for face in sorted(owners):
        loop = loops[face]
        for region in sorted({region_of[c] for c, _ in owners[face]}):
            positions = {}
            for e in loop:
                pair = edge_list[abs(e) - 1]
                node = pair[0] if e > 0 else pair[1]
                corners.append(node)
                positions[node] = len(corners)
            ids = []
            for e in loop:
                a, b = edge_list[abs(e) - 1]
                corner_edges.append((positions[a], positions[b], 1, abs(e)))
                ids.append(len(corner_edges))
            facets.append((len(ids), *ids, 1, face))
            region_facets[region].append(len(facets))
    bindings = sorted(node_bindings)
    rows = [(f, *owners[f][0], g) for f, g in sorted(face_bindings.items())]
    tail = [len(bindings), *(v for row in bindings for v in row),
            0, len(rows), *(v for row in rows for v in row), 0, 0, 0, 0]
    tail += [len(corners), *corners, len(corner_edges), *(v for row in corner_edges for v in row)]
    tail += [len(facets), *(v for row in facets for v in row), len(regions)]
    for ids, cell_ids in zip(region_facets, regions, strict=True):
        tail += [len(ids), *ids, len(cell_ids), *cell_ids]
    tail.append(len(regions))
    for region in range(1, len(regions) + 1):
        tail += [1, region]
    tail += [0, 0]

    xyz = b"".join(struct.pack("<3d", *p) for p in points)
    mes = (_ints(250, 5) + b"test\0" + _ints(len(points)) + xyz
           + _records(edge_list) + _records(list(loops.values())) + _records(solids)
           + _ints(len(cells))
           + b"".join(_ints(_HEX8, i, 8, *nodes) for i, nodes in enumerate(cells, 1))
           + _records(regions) + _ints(*tail))

    table = [str(len(blocks))]
    table += [f"{name}   {MATERIAL}   5601   {i}" for i, (name, _) in enumerate(blocks)]
    table += [str(len(sidesets)), *(f"{name}   0" for name, _ in sidesets)]
    for count, kind in ((6 * len(cells), 5001), (6 * len(cells), 5002),
                        (sum(map(len, owners.values())), 5004),
                        (len(blocks), 5006)):
        table += [str(count), *[f"0   {kind}   0"] * count]
    table.append("100")
    tbl = ("\r\n".join(table) + "\r\n").encode("ascii")

    fac = _ints(100, len(points)) + xyz + _ints(4 * len(exterior))
    fac += b"".join(_ints(1, a, b) for loop in exterior for a, b in pairwise(loop + loop[:1]))
    fac += _ints(len(exterior))
    fac += b"".join(_ints(2, a, b, c, c, d, a) for a, b, c, d in exterior)
    return _ints(101) + b"".join(struct.pack("<5sQ", tag, len(value)) + value
                                 for tag, value in ((b".mes", mes), (b".tbl", tbl), (b".fac", fac)))


def read_active_mesh(cubit):
    """Return ``(points, blocks, sidesets, node_ids)`` from the active Cubit mesh."""
    from . import vtu

    nodes, cells = vtu.read_active_cells(cubit)
    block_ids = list(cubit.get_block_id_list())
    side_ids = list(cubit.get_sideset_id_list())
    hexes = {b: [] for b in block_ids}
    quads = {s: [] for s in side_ids}
    for kind, conn, group, category in cells:
        if category == vtu.vw.GROUP_BLOCK and kind == "hex" and len(conn) == 8:
            hexes[group].append(conn)
        elif category == vtu.vw.GROUP_SIDESET and kind == "quad" and len(conn) == 4:
            quads[group].append(conn)
        else:
            raise ValueError(f"Femtet PDT export supports HEX8 blocks and QUAD4 sidesets only, found {kind} "
                             f"with {len(conn)} nodes in {'block' if category == vtu.vw.GROUP_BLOCK else 'sideset'} {group}")
    # Number nodes in order of first use by the block elements, as Cubit's
    # Exodus export does; this is the numbering checked against Femtet.
    index = {}
    for b in block_ids:
        for conn in hexes[b]:
            for n in conn:
                index.setdefault(n, len(index) + 1)
    node_ids = list(index)
    loose = sorted({n for s in side_ids for q in quads[s] for n in q} - set(index))
    if loose:
        raise ValueError(f"sideset nodes {loose[:5]} are not on any block element")
    points = [tuple(float(x) for x in nodes[n]) for n in node_ids]
    blocks = [(cubit.get_exodus_entity_name("block", b), [tuple(index[n] for n in c) for c in hexes[b]])
              for b in block_ids]
    sidesets = [(cubit.get_exodus_entity_name("sideset", s), [tuple(index[n] for n in q) for q in quads[s]])
                for s in side_ids]
    return points, blocks, sidesets, node_ids


# Femtet stores the mesh of project ``X.femprj`` as ``X.Results/<analysis model>.pdt``.
# The bundled project was saved by Femtet with no bodies, a magnetostatic analysis
# and its default analysis model name.
ANALYSIS_MODEL = "\u89e3\u6790\u30e2\u30c7\u30eb"  # Femtet's default analysis model name
PROJECT_TEMPLATE = Path(__file__).with_name("femtet_project.femprj")


def export_femtet(path):
    """Write the active Cubit mesh as a new Femtet project and its paired mesh.

    Creates ``X.femprj`` and ``X.Results/<analysis model>.pdt``; both must not exist.
    Femtet resolves the paired mesh from the project (``ResultFilePath``).
    """
    import cubit

    project = Path(path)
    if project.suffix.lower() != ".femprj":
        raise ValueError("output path must end in .femprj")
    results = project.with_name(project.stem + ".Results")
    if project.exists() or results.exists():
        raise FileExistsError(f"choose an unused Femtet project name: {project}")
    points, blocks, sidesets, _ = read_active_mesh(cubit)
    data = encode_pdt(points, blocks, sidesets)
    template = PROJECT_TEMPLATE.read_bytes()
    results.mkdir()
    mesh = results / (ANALYSIS_MODEL + ".pdt")
    with mesh.open("xb") as stream:
        stream.write(data)
    with project.open("xb") as stream:
        stream.write(template)
    return {
        "project": str(project),
        "project_sha256": hashlib.sha256(template).hexdigest(),
        "pdt": str(mesh),
        "pdt_sha256": hashlib.sha256(data).hexdigest(),
        "nodes": len(points),
        "blocks": {name: len(cells) for name, cells in blocks},
        "sidesets": {name: len(faces) for name, faces in sidesets},
    }
