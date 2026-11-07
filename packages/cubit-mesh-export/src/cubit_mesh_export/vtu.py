"""Export the active Cubit mesh as ASCII VTK XML UnstructuredGrid (.vtu).

Every block element is written as Cubit holds it: the block element type sets the order
(for example ``block 1 element type tetra10`` gives quadratic tetrahedra). Sideset faces
follow as cells with ``GroupType`` 1. Coordinates are in Cubit model units.

Every volume element must be in a block. Tri and quad elements must be in a block when the model
has no volume elements; with volume elements present, those outside blocks are taken as volume
skin and left out, so put a separate shell surface in a block to export it.

The writer is :mod:`cubit_mesh_export.vtu_writer`; nothing else is imported.
"""
from __future__ import annotations

from . import vtu_writer as vw

_VOLUME_KINDS = ("hex", "tet", "wedge", "pyramid")
_FACE_KINDS = ("quad", "tri")
_PLURAL = {"hex": "hexes", "tet": "tets", "wedge": "wedges", "pyramid": "pyramids",
           "quad": "quads", "tri": "tris"}
_DIRECT = {("block", "quad"): "get_block_faces"}   # Cubit names block quads "faces"
_ENTITY = {"quad": "face"}   # and get_entities("quad") is empty; "face" lists the quads
_FREE_MESH = ("Cubit deletes mesh that no geometry owns (free mesh, e.g. from MeshImport) when "
              "a block or sideset is defined on it; keep the geometry that owns the mesh")


def _members(cubit, owner, owner_id, kind):
    """Elements of ``kind`` held by a block or sideset directly or through its geometry."""
    direct = _DIRECT.get((owner, kind), f"get_{owner}_{_PLURAL[kind]}")
    ids = set(getattr(cubit, direct)(owner_id))
    geometry = ("volume", "surface") if owner == "block" else ("surface",)
    for gkind in geometry:
        if gkind == "volume" and kind not in _VOLUME_KINDS:
            continue
        if gkind == "surface" and kind not in _FACE_KINDS:
            continue
        for gid in getattr(cubit, f"get_{owner}_{gkind}s")(owner_id):
            ids.update(getattr(cubit, f"get_{gkind}_{_PLURAL[kind]}")(gid))
    return ids


def read_active_cells(cubit):
    """Return ``(nodes, cells)`` for :func:`vtu_writer.write_vtu` from the active Cubit mesh."""
    blocks = list(cubit.get_block_id_list())
    if not blocks:
        raise vw.VtuError(f"the Cubit model has no blocks; {_FREE_MESH}")
    for owner, ids, members in (("block", blocks, ("curves", "edges", "nodes", "vertices")),
                                ("sideset", cubit.get_sideset_id_list(), ("curves", "edges"))):
        for i in ids:
            for member in members:
                if getattr(cubit, f"get_{owner}_{member}")(i):
                    raise vw.VtuError(f"{owner} {i} holds {member}; only volume, tri and quad "
                                      "elements are written")
    # Faces must belong to a block unless they are only the skin of volume elements.
    surface_mesh = not any(cubit.get_entities(kind) for kind in _VOLUME_KINDS)
    cells = []
    for kind in (*_VOLUME_KINDS, *_FACE_KINDS):
        owner_of = {}
        for block in blocks:
            for element in sorted(_members(cubit, "block", block, kind)):
                if element in owner_of:
                    raise vw.VtuError(f"Cubit {kind} {element} belongs to blocks "
                                      f"{owner_of[element]} and {block}")
                owner_of[element] = block
                cells.append((kind, cubit.get_expanded_connectivity(kind, element),
                              block, vw.GROUP_BLOCK))
        if kind in _VOLUME_KINDS or surface_mesh:
            unassigned = set(cubit.get_entities(_ENTITY.get(kind, kind))) - set(owner_of)
            if unassigned:
                raise vw.VtuError(f"Cubit {kind} {min(unassigned)} has no block assignment")
    if not cells:
        raise vw.VtuError(f"the Cubit blocks hold no elements; {_FREE_MESH}")
    for sideset in cubit.get_sideset_id_list():
        faces = [(kind, f) for kind in _FACE_KINDS for f in sorted(_members(cubit, "sideset", sideset, kind))]
        if not faces:
            raise vw.VtuError(f"sideset {sideset} has no tri or quad faces; {_FREE_MESH}")
        cells += [(kind, cubit.get_expanded_connectivity(kind, f), sideset, vw.GROUP_SIDESET)
                  for kind, f in faces]
    node_ids = {n for _, conn, _, _ in cells for n in conn}
    nodes = {n: cubit.get_nodal_coordinates(n) for n in node_ids}
    return nodes, cells


def export_vtu(path) -> dict:
    """Write the active Cubit mesh to ``path`` (.vtu) and return a summary."""
    import cubit

    nodes, cells = read_active_cells(cubit)
    return vw.write_vtu(path, nodes, cells)
