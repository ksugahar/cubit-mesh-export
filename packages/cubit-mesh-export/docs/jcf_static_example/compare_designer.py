"""Compare the two 25.2 static results for this example (Python and NumPy)."""
import argparse
from collections import Counter
from itertools import combinations
import json
import struct
from pathlib import Path
from xml.etree import ElementTree as ET
from zipfile import ZIP_STORED, ZipFile
import zlib

import numpy as np

TET_FACES = ((1, 2, 3), (0, 2, 3), (0, 1, 3), (0, 1, 2))
EDGE_RECORD = struct.Struct("<id")
JMAG_DEFLATE = bytes.fromhex("7f7f7e7d7e7f")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def case_mesh(path):
    with ZipFile(path) as archive:
        data = archive.read("mesh.bin")
    if data.startswith(JMAG_DEFLATE):
        stream = zlib.decompressobj(-15)
        data = stream.decompress(data[len(JMAG_DEFLATE):])
        require(stream.eof and not stream.unused_data, "Unexpected compressed mesh layout")
    require(data.startswith(b"MJ\0\0"), "Expected a JMAG binary mesh")
    version, node_count, tet_count, extra = struct.unpack_from("<4i", data, 4)
    require((version, node_count, tet_count) == (1, 1352, 6107),
            "Expected 1,352 nodes and 6,107 tetrahedra for this example")
    require(data[20:24] == b"m\0\0\0", "Expected mesh coordinates in meters")
    start = 60 + 4 * extra
    end = start + 28 * node_count
    nodes = {i: (x, y, z) for i, x, y, z in struct.iter_unpack("<i3d", data[start:end])}
    tets = {}
    for i, kind, order, part, *vertices in struct.iter_unpack("<8i", data[end:]):
        require((kind, order) == (5, 1), "Expected linear tetrahedra")
        require(part in (1, 2) and len(set(vertices)) == 4 and set(vertices) <= nodes.keys(),
                f"Invalid tetrahedron {i}")
        tets[i] = (part, tuple(vertices))
    require(len(nodes) == node_count and len(tets) == tet_count, "Invalid mesh IDs or sizes")
    return nodes, tets


def face_nodes(vertices, side):
    require(1 <= side <= 4, f"Invalid local tetrahedron face {side}")
    return tuple(sorted(vertices[j] for j in TET_FACES[side - 1]))


def planar_faces(nodes, tets):
    occurrences = Counter(face_nodes(vertices, side)
                          for _, vertices in tets.values() for side in range(1, 5))
    return {face for face, count in occurrences.items()
            if count == 1 and all(nodes[node][2] == 0 for node in face)}


def symmetry_faces(path, tets):
    with ZipFile(path) as archive:
        xml = ET.fromstring(archive.read("root.xml"))
        condition, = xml.findall("./condition_data/condition[@name='SymmetryBoundary3D']")
        group, = condition.findall("group[@type='faces']")
        key = int(group.attrib["key"])
        blocks = archive.read("entity_groups.dat").decode("ascii").split("#")
    require(int(blocks[0].split()[0]) == len(blocks) - 1, "Invalid group count")
    rows, = (row for row in (list(map(int, block.split())) for block in blocks[1:])
             if row[0] == key)
    _, kind, count, columns, _ = rows[:5]
    require((kind, count, columns) == (2, 314, 2) and len(rows[5:]) == count * columns,
            f"Expected 314 symmetry faces, found {count}")
    faces = {face_nodes(tets[i][1], side) for i, side in zip(rows[5::2], rows[6::2])}
    require(len(faces) == count, "Duplicate symmetry faces")
    return faces


def stored_step_payload(path, archive, name):
    # 25.2 may emit a mismatched ZIP CRC on this stored result member.
    # Read only this known member, without extracting files or relaxing other CRC checks.
    info = archive.getinfo(name)
    require(name == "step_1.dat" and info.compress_type == ZIP_STORED
            and info.file_size == info.compress_size and not info.flag_bits & 1,
            "Unsupported static result member")
    with path.open("rb") as stream:
        stream.seek(info.header_offset)
        header = stream.read(30)
        require(header.startswith(b"PK\x03\x04"), "Invalid ZIP local header")
        name_len, extra_len = struct.unpack_from("<HH", header, 26)
        require(stream.read(name_len) == name.encode("ascii"), "Result member name mismatch")
        stream.seek(extra_len, 1)
        data = stream.read(info.file_size)
    require(len(data) == info.file_size, "Truncated result member")
    return data


def edge_potential(path, edges):
    with ZipFile(path) as archive:
        require([name for name in archive.namelist() if name.startswith("step_") and name.endswith(".xml")]
                == ["step_1.xml"], "Expected exactly one static result step")
        xml = ET.fromstring(archive.read("step_1.xml"))
        metadata = [{p.attrib["name"]: p.text for p in row.findall("parameter")}
                    for row in xml.iter("result")]
        edge, = (row for row in metadata if row["code"] == "11006")
        require(int(edge["num_values"]) == 1, "Expected scalar edge integrals")
        name = edge["data_file"]
        rows = [row for row in metadata if row["data_file"] == name]
        origin = min(int(row["start_offset"]) for row in rows)
        extent = max(int(row["start_offset"]) + int(row["size"]) * (4 + 8 * int(row["num_values"]))
                     for row in rows)
        data = stored_step_payload(path, archive, name)
    require(len(data) == extent - origin, "Result metadata and payload sizes differ")
    start = int(edge["start_offset"]) - origin
    stop = start + int(edge["size"]) * EDGE_RECORD.size
    records = dict(EDGE_RECORD.iter_unpack(data[start:stop]))
    require(len(records) == int(edge["size"]) == 7565 and all(1 <= i <= len(edges) for i in records)
            and np.isfinite(list(records.values())).all(), "Invalid edge-potential records")
    return {edges[i - 1]: value for i, value in records.items()}


def curl(nodes, tets, potential):
    # Whitney edge basis curl is 2 grad(lambda_i) x grad(lambda_j).
    # Edge directions follow increasing node IDs. Constrained edges store zero implicitly.
    result = []
    for _, (_, vertices) in sorted(tets.items()):
        ids = sorted(vertices)
        xyz = np.array([nodes[i] for i in ids])
        gradients = np.linalg.inv(np.column_stack((np.ones(4), xyz)))[1:, :].T
        result.append(sum(
            2 * potential.get((ids[i], ids[j]), 0.0) * np.cross(gradients[i], gradients[j])
            for i, j in combinations(range(4), 2)
        ))
    return np.array(result)


def compare(direct, designer):
    dn, dt = case_mesh(direct)
    rn, rt = case_mesh(designer)
    require(dt == rt and dn.keys() == rn.keys(), "Node or element IDs, connectivity, or parts differ")
    direct_plot = direct.with_suffix(".jplot")
    designer_plot = designer.with_suffix(".jplot")
    require(case_mesh(direct_plot) == (dn, dt) and case_mesh(designer_plot) == (rn, rt),
            "A result mesh does not match its solver input")
    node_ids = sorted(dn)
    left = np.array([dn[i] for i in node_ids])
    right = np.array([rn[i] for i in node_ids])
    require(np.isfinite(left).all() and np.isfinite(right).all(), "Invalid node coordinates")
    geometry_delta = float(np.max(np.abs(left - right)))
    require(geometry_delta <= 32 * np.finfo(float).eps * np.max(np.abs(left)),
            f"Mesh coordinates differ by {geometry_delta} m")
    expected = planar_faces(dn, dt)
    require(len(expected) == 314 and symmetry_faces(direct, dt) == symmetry_faces(designer, rt) == expected,
            "Symmetry face sets differ from the 314 z=0 exterior triangles")
    edges = sorted({pair for _, vertices in dt.values() for pair in combinations(sorted(vertices), 2)})
    a = edge_potential(direct_plot, edges)
    b = edge_potential(designer_plot, edges)
    require(a.keys() == b.keys(), "Constrained edge sets differ")
    direct_b, designer_b = curl(dn, dt, a), curl(rn, rt, b)
    norm = np.linalg.norm(designer_b)
    require(norm > 0 and np.isfinite(direct_b).all() and np.isfinite(designer_b).all(),
            "Expected finite fields and nonzero reference norm")
    delta = direct_b - designer_b
    return {
        "nodes": len(dn), "tetrahedra": len(dt), "symmetry_faces": len(expected),
        "coordinate_max_difference_m": geometry_delta,
        "B_max_vector_difference_T": float(np.max(np.linalg.norm(delta, axis=1))),
        "B_relative_L2": float(np.linalg.norm(delta) / norm),
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("direct_jcf", type=Path)
    parser.add_argument("designer_jcf", type=Path)
    args = parser.parse_args()
    print(json.dumps(compare(args.direct_jcf, args.designer_jcf), indent=2))
