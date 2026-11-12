"""Export a conforming Cubit TETRA10 mesh as COMSOL ASCII MPHTXT."""
from __future__ import annotations

from collections import defaultdict
import hashlib
import json
import math
from pathlib import Path

from . import vtu


# Cubit's expanded TETRA10 connectivity uses the same edge order as VTK.
_CUBIT_EDGES = ((0, 1), (1, 2), (0, 2), (0, 3), (1, 3), (2, 3))
_COMSOL_EDGES = ((0, 1), (0, 2), (1, 2), (0, 3), (1, 3), (2, 3))
_FACES = ((0, 1, 3), (1, 2, 3), (0, 2, 3), (0, 1, 2))


def _edge(a, b):
    return tuple(sorted((a, b)))


def encode_mesh(nodes, cells, surface_faces, block_names, sideset_names, *, meters_per_unit):
    """Return MPHTXT and an index manifest, preserving original quadratic nodes.

    Each geometric surface is keyed by its Cubit ID. The manifest describes
    indices in the file. A consumer must verify the imported COMSOL selections.
    """
    if not math.isfinite(meters_per_unit) or meters_per_unit <= 0:
        raise ValueError("meters_per_unit must be positive and finite")
    node_ids = sorted(nodes)
    indices = {node: i for i, node in enumerate(node_ids)}
    points = [tuple(float(x) * meters_per_unit for x in nodes[node]) for node in node_ids]
    if any(len(p) != 3 or not all(map(math.isfinite, p)) for p in points):
        raise ValueError("MPHTXT requires finite 3D node coordinates")

    facets = defaultdict(list)
    midpoints = {}
    edge_of_midpoint = {}
    volume_keys = set()
    tetrahedra = []
    domains = []
    sides = defaultdict(set)
    side_nodes = []
    blocks = set()
    for kind, conn, group, category in cells:
        if any(node not in indices for node in conn):
            raise ValueError("connectivity references an absent node")
        if category == vtu.vw.GROUP_SIDESET and kind == "tri" and len(conn) in (3, 6):
            face = tuple(sorted(conn[:3]))
            sides[face].add(group)
            if len(conn) == 6:
                side_nodes.append((face, set(conn[3:])))
            continue
        if category != vtu.vw.GROUP_BLOCK or kind != "tet" or len(set(conn)) != 10 or len(conn) != 10:
            raise ValueError("MPHTXT supports TETRA10 blocks and triangular sidesets only")
        corners = conn[:4]
        volume_key = tuple(sorted(corners))
        if volume_key in volume_keys:
            raise ValueError(f"duplicate tetrahedron {volume_key}")
        volume_keys.add(volume_key)
        blocks.add(group)
        for (a, b), midpoint in zip(_CUBIT_EDGES, conn[4:]):
            edge = _edge(corners[a], corners[b])
            if (midpoints.setdefault(edge, midpoint) != midpoint
                    or edge_of_midpoint.setdefault(midpoint, edge) != edge):
                raise ValueError(f"nonconforming quadratic node on edge {edge}")
        ordered = [*corners, *(midpoints[_edge(corners[a], corners[b])] for a, b in _COMSOL_EDGES)]
        tetrahedra.append([indices[node] for node in ordered])
        domains.append(group)
        for face in _FACES:
            facets[tuple(sorted(corners[i] for i in face))].append(group)
    if not tetrahedra:
        raise ValueError("no TETRA10 elements")
    if any(len(owners) > 2 for owners in facets.values()):
        raise ValueError("nonmanifold tetrahedral face")
    exterior_or_interface = {
        face for face, owners in facets.items() if len(owners) == 1 or len(set(owners)) == 2
    }
    if set(surface_faces) != exterior_or_interface:
        raise ValueError("CAD surface mesh does not cover exactly the exterior and inter-block faces")
    if set(sides) - exterior_or_interface:
        raise ValueError("sideset contains a face outside the exported boundaries")
    for (a, b, c), nodes_on_face in side_nodes:
        if nodes_on_face != {midpoints[a, b], midpoints[a, c], midpoints[b, c]}:
            raise ValueError(f"sideset has different quadratic nodes on face {(a, b, c)}")

    triangles = []
    labels = []
    surface_ids = {}
    selections = defaultdict(set)
    for face in sorted(exterior_or_interface):
        surface = surface_faces[face]
        key = (surface, tuple(sorted(facets[face])), tuple(sorted(sides.get(face, ()))))
        label = surface_ids.setdefault(key, len(surface_ids))
        a, b, c = face
        triangles.append([indices[node] for node in (a, b, c, midpoints[a, b], midpoints[a, c], midpoints[b, c])])
        labels.append(label)
        for sideset in key[2]:
            selections[sideset].add(label)

    domain_ids = {block: i for i, block in enumerate(sorted(blocks), start=1)}
    lines = ["0 1", "1", "# Tags", "5 mesh1", "1", "# Types", "3 obj",
             "0 0 1", "4 Mesh", "8", "3", "0", str(len(points)),
             "# Mesh vertex coordinates"]
    lines.extend(" ".join(format(value, ".17g") for value in point) for point in points)
    lines.append("2 # number of element types")
    for kind, width, elements, entities in (
        ("tet2", 10, tetrahedra, [domain_ids[block] for block in domains]),
        ("tri2", 6, triangles, labels),
    ):
        lines.extend((f"4 {kind} # type name", str(width), str(len(elements)), "# Elements"))
        lines.extend(" ".join(map(str, row)) for row in elements)
        lines.extend((str(len(elements)), "# Geometric entity indices"))
        lines.extend(map(str, entities))
    text = "\n".join(lines) + "\n"
    manifest = {
        "coordinate_unit": "m",
        "mphtxt_sha256": hashlib.sha256(text.encode("ascii")).hexdigest(),
        "blocks": [{"cubit_id": block, "name": block_names[block], "file_index": index}
                   for block, index in domain_ids.items()],
        "surfaces": [{"cubit_id": surface, "file_index": index,
                      "adjacent_blocks": list(owners), "sideset_ids": list(groups)}
                     for (surface, owners, groups), index in surface_ids.items()],
        "sidesets": [{"cubit_id": group, "name": sideset_names[group], "file_indices": sorted(ids)}
                     for group, ids in sorted(selections.items())],
    }
    return text, manifest


def export_mphtxt(path, *, meters_per_unit):
    """Write the active Cubit mesh and a JSON index manifest to new files."""
    import cubit

    target = Path(path)
    if target.suffix.lower() != ".mphtxt":
        raise ValueError("output path must end in .mphtxt")
    sidecar = target.with_suffix(".mphtxt.json")
    if target.exists() or sidecar.exists():
        raise FileExistsError(f"choose an unused MPHTXT output path: {target}")
    nodes, cells = vtu.read_active_cells(cubit)
    surface_faces = {}
    for surface in cubit.get_entities("surface"):
        for triangle in cubit.get_surface_tris(surface):
            corners = tuple(sorted(cubit.get_expanded_connectivity("tri", triangle)[:3]))
            if surface_faces.setdefault(corners, surface) != surface:
                raise ValueError(f"triangle {triangle} belongs to multiple CAD surfaces")
    text, manifest = encode_mesh(
        nodes, cells, surface_faces,
        {block: cubit.get_exodus_entity_name("block", block) for block in cubit.get_block_id_list()},
        {side: cubit.get_exodus_entity_name("sideset", side) for side in cubit.get_sideset_id_list()},
        meters_per_unit=meters_per_unit,
    )
    with sidecar.open("x", encoding="utf-8", newline="\n") as stream:
        stream.write(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")
    with target.open("x", encoding="ascii", newline="\n") as stream:
        stream.write(text)
    return manifest
