"""
Export format documentation for the Cubit Mesh Export MCP server.

Provides API reference, parameter tables, supported element types,
and usage guidance for each mesh export format.
"""

EXPORT_OVERVIEW = """
# Cubit Mesh Export - Export Functions Overview

## Two Export Paths (must produce identical results for tet meshes)

| Path | Method | Speed | Use Case |
|------|--------|-------|----------|
| **Path A** (C++) | `cubit.cmd('export netgen ...')` | Fast | APREPRO journal, GUI menu |

Path A is the recommended export path. Use `export netgen "mesh.vol" order N` for all workflows.

Named periodic export is opt-in and requires all `periodic_*` fields: name
`kt`, exact master/slave boundary labels, coordinate unit `m`, BC patch-pair
list, translation x/y/z and positive finite matching distance. BC numbers are
not FaceDescriptor or CAD surface IDs. Every point must match uniquely across
all incident patches; ambiguity or incomplete coverage fails the export.
The native writer saves group 1 / PERIODIC / kt directly and preserves curved
geometry. Consumers verify the file; they never add or repair identifications.
No centroid translation or default distance is inferred. Named periodic exports
require a fresh path; ordinary export without this request makes no periodic
readiness claim.

## Supported Formats

| Command | Format | Max Order | Notes |
|---------|--------|-----------|-------|
| `export netgen "f.vol" order N` | Netgen .vol (+ .vol.json) | 1-5 | Recommended for NGSolve |

Order 2+ uses NetgenCurver (compact_netgen BuildCurvedElements + ACIS projection).
No fallback to HighOrderMesh (removed).

## API

```python
# Netgen .vol (recommended for NGSolve FEM computation)
cubit.cmd('export netgen "mesh.vol" order 3 overwrite')
# -> produces mesh.vol + mesh.vol.json (CAD reference values)

# Netgen .vol supports order 1-5
cubit.cmd('export netgen "mesh.vol" order 5 overwrite')
```

## Companion JSON (.vol.json)

`export netgen` writes a companion JSON with CAD reference values:
```json
{
  "materials": {"iron": 5.24e-04, "air": 3.38e-03},
  "boundaries": {"surface_1": 3.14e-02, "coil": 1.26e-02},
  "edges": {"curve_1": 3.14e-01},
  "n_elements": 10359, "n_points": 2071, "order": 3
}
```

`calc_verify_vol.py` reads this JSON for per-label consistency checks.

## Common Usage Pattern

```python
import cubit
cubit.init(['cubit', '-nojournal', '-batch'])

cubit.cmd("create sphere radius 0.05")
cubit.cmd("volume 1 scheme tetmesh")
cubit.cmd("volume 1 size auto factor 5")
cubit.cmd("mesh volume 1")

# Blocks define material labels
cubit.cmd("block 1 add volume 1")
cubit.cmd('block 1 name "sphere"')

# Sidesets define boundary labels (optional)
cubit.cmd("sideset 1 add surface 1")
cubit.cmd('sideset 1 name "outer"')

# Export (surface elements extracted automatically from volume faces)
cubit.cmd('export netgen "sphere.vol" order 3 overwrite')
```

**Note**: `block 2 add tri all` is NOT needed for export netgen.
Surface elements are extracted from volume element faces automatically.

## IH (BEM) Inductance: Sideset Setup

IH (BEM) uses surface elements only (TRI). Source/sink terminal faces
must be defined as **sidesets** (not blocks) so they become boundary
labels in the .vol file.

```python
# Coil geometry
cubit.cmd("volume 1 scheme tetmesh")
cubit.cmd("mesh volume 1")
cubit.cmd("block 1 add volume 1")
cubit.cmd('block 1 name "coil"')

# Terminal faces: sidesets -> boundary labels in .vol
cubit.cmd("sideset 1 add surface 3")   # source terminal
cubit.cmd('sideset 1 name "source"')
cubit.cmd("sideset 2 add surface 5")   # sink terminal
cubit.cmd('sideset 2 name "sink"')

# Export (sidesets become boundary labels)
cubit.cmd('export netgen "coil.vol" order 2 overwrite')

# Compute (no Cubit needed):
# python calc_inductance.py --vol coil.vol --source source --sink sink
```

**Important**: Both volume .vol and surface-only .vol work for IH (BEM).
The BEM solver uses BND elements only. Volume elements are ignored.
"""

EXPORT_CURVED = """
# Curved Mesh Export (export netgen)

```python
import tempfile
from ngsolve import Mesh
vol_path = tempfile.mktemp(suffix='.vol')
cubit.cmd(f'export netgen "{vol_path}" order 3 overwrite')
mesh = Mesh(vol_path)
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `order` | int (1-5) | Polynomial order for mesh curving |
| `overwrite` | flag | Overwrite existing file |

**Returns**: `.vol` file with embedded curvedelements section. Load with `Mesh(vol_path)`.

## How It Works

Uses NetgenCurver (compact_netgen C++ static link) with CallbackGeometry
to delegate surface projection to Cubit's ACIS kernel:

1. Reads mesh topology (nodes, elements) from Cubit blocks
2. Creates 1st order netgen.meshing.Mesh
3. Registers CallbackGeometry with ACIS surface projection
4. Calls BuildCurvedElements(order) using the CallbackGeometry
5. Refits the edge/face coefficients to the ACIS geometry (since 2.1.0)
6. Writes .vol with curvedelements section + companion .vol.json

No STEP files, no OCC geometry, no SetGeomInfo needed.

## Refit, compatibility and warnings (2.1.0)

- Step 5 fixes Netgen's odd-order stall; the export log prints the
  accepted/tried refit counts.  `CUBIT_MESH_EXPORT_GEOMETRIC_REFIT=0` keeps
  Netgen's coefficients (A/B comparison only).
- The `.vol` carries `edgesegmentsgi3` and needs NGSolve 6.2.2605 or newer.
- Pyramids need the patched Netgen 6.2.2608 runtime (apex clamp and
  base-edge blend); `check-vol` rejects them on any other runtime.  See
  `netgen_workflow_guide` ("Pyramids require the patched Netgen").
- MCP batch sessions load the plugin installed in Cubit's `plugins`
  directory; after an upgrade, `cubit-plugin-install` must have run for the
  new behaviour to appear there.

## Key Design Decision

The command exports **1st order elements** internally, then curves
them to the requested order using ACIS surface projection. Even if
blocks contain TET10, only corner nodes are used initially. High-order
nodes are placed exactly on ACIS CAD surfaces by BuildCurvedElements(order).

## Supported Elements

| Element | Nodes | Netgen Type |
|---------|-------|-------------|
| TET4 | 4 | Tet |
| HEX8 | 8 | Hex |
| WEDGE6 | 6 | Prism |
| PYRAMID5 | 5 | Pyramid |
| TRI3 | 3 | Trig |
| QUAD4 | 4 | Quad |
| EDGE2 | 2 | Segment |

## Node Ordering Conversion (Cubit -> Netgen)

| Element | Cubit Order | Netgen Order |
|---------|-------------|-------------|
| TET | [0,1,2,3] | [0,1,2,3] (same) |
| HEX | [0,1,2,3,4,5,6,7] | [0,1,5,4,3,2,6,7] |
| WEDGE | [0,1,2,3,4,5] | [0,2,1,3,5,4] |
| PYRAMID | [0,1,2,3,4] | [3,2,1,0,4] |

## Workflows

For high-order curving, see `netgen_workflow_guide()` tool.
For simple 2nd order without geometry, use `export netgen "mesh.vol" order 2`.

## Deleted Predecessors

`export_NetgenMesh()`, `export_netgen()`, `export_netgen_with_names()`,
`set_*_geominfo()`, `name_occ_faces()` are all removed.
`export netgen` replaces all of them.
"""

EXPORT_EXODUS = """
# Exodus II Export

```python
export_exodus(cubit, FileName, overwrite=True, large_model=False)
```

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `cubit` | object | required | Cubit Python interface |
| `FileName` | str | required | Output .exo file path |
| `overwrite` | bool | True | Overwrite existing file |
| `large_model` | bool | False | Use 64-bit integers for large meshes |

## How It Works (Exception to Design Philosophy)

Unlike other export functions, `export_exodus()` uses Cubit's **built-in**
`export mesh` command internally:
```python
cubit.cmd('export mesh "filename.exo" overwrite')
```

This is because Exodus II is Cubit's native format, and the built-in command
provides full fidelity including all element types, nodesets, sidesets, etc.

## large_model Parameter

| Value | Integer Size | Max Nodes/Elements | Use When |
|-------|-------------|-------------------|----------|
| `False` | 32-bit | ~2 billion | Most meshes |
| `True` | 64-bit | Unlimited | Very large meshes (>2B nodes/elements) |

```python
# Standard export
cubit.cmd('export mesh "mesh.exo" overwrite')

# Large model (64-bit integers)
cubit.cmd('export mesh "mesh.exo" overwrite large')
```

The `large` flag is passed to Cubit's `export mesh` command, which writes
the Exodus file with 64-bit integer IDs for node and element numbering.

## Supported Elements

All Cubit element types, including:
- 0D: NODE
- 1D: BAR, BAR2, BAR3
- 2D: TRI3, TRI6, TRI7, QUAD4, QUAD8, QUAD9
- 3D: TET4, TET10, TET11, HEX8, HEX20, HEX27, WEDGE6, WEDGE15, PYRAMID5, PYRAMID13

## Use Cases

- Archival of Cubit meshes with full fidelity
- Downstream solvers: Sierra, MOOSE, Albany, ARIA
- Multi-physics applications requiring block/nodeset/sideset structure
- Meshes with any element order (1st, 2nd, or higher)
"""

EXPORT_COMPARISON = """
# Export Format Comparison

Netgen `.vol` is the only mesh output of cubit-mesh-export.

| Use Case | Command | Why |
|----------|---------|-----|
| NGSolve FEM (any order) | `export netgen "f.vol" order N` | Order 1-5 via ACIS CallbackGeometry |
| Cubit-native archival | `export mesh "f.exo"` (Cubit built-in) | Exodus II, not a cubit-mesh-export command |

The checked `.vol` is the only solver input; run `check-vol` before analysis.
"""


EXPORT_DECISION_GUIDE = """
# Export Format Decision Guide

## "I want to use NGSolve / Netgen for FEM"

-> Use `export netgen` (recommended for any order):
  ```python
  import tempfile
  from ngsolve import Mesh
  cubit.cmd("block 1 add volume all")
  vol_path = tempfile.mktemp(suffix='.vol')
  cubit.cmd(f'export netgen "{vol_path}" order 3 overwrite')
  mesh = Mesh(vol_path)
  ```
  Works for ANY geometry shape. No STEP files, no OCC, no SetGeomInfo.

## "I need multi-physics (MOOSE, Sierra)"

-> Use `export_exodus()`
- Supports all element types and orders
- Full block definitions preserved
- Large model support: `export_exodus(cubit, "mesh.exo", large_model=True)`
- Cubit-native format with highest fidelity

## Performance & Feature Summary

| Format | Max Order | 2D | 3D | HO Method |
|--------|-----------|----|----|-----------|
| netgen (.vol) | 5 | No | Yes | NetgenCurver + ACIS |

## Troubleshooting: High-Order Export

### "Interrupt Detected" on AddPoint (NetgenCurver crash)

**Cause**: ABI mismatch between ccm plugin and nglib.dll at runtime.
The ccm was built against one version of Netgen, but a different nglib.dll
is loaded from the plugins/ directory.

**Fix**: Rebuild ccm with **compact_netgen** (static link, no nglib.dll dependency).
`cubit-plugin-install` deletes stale `plugins/nglib.dll`, `plugins/ngcore.dll`
and the retired `cubit_mesh_curver*.pyd`; the `.ccm` is the only native payload.
```bash
cmake ...  # netgen sources in-repo (compact_netgen/netgen_src/), no external path needed
```

## Cubit GUI Menu Structure

```
Menu bar: File Edit View Display Tools Export Help
                                       (menu-bar action, PySide6 dialogs)
Export:
  Export Netgen (.vol)...
```

- **Export**: the single action opens the Netgen dialog and runs `export netgen`.
- Solver orchestration is outside the Cubit plugin. Downstream applications
  consume exported meshes through their own process boundary.
  uses explicit `cubit.cmd(...)` export commands and is not a Cubit menu action
- Settings saved to `AppData/Roaming/cubit-mesh-export/export_settings.json`
"""


P_CONVERGENCE_KNOWLEDGE = """
# p-Convergence Testing and Known Issues

## What is p-Convergence?

When exporting a curved mesh at increasing polynomial orders (p=1..5),
the volume and area errors vs CAD geometry should decrease monotonically.
Each order should gain ~2-3 digits of accuracy.

Example (Cylinder Tet, p=1..5):
```
Order  Nodes  Volume error [%]   Area error [%]
  1       56   -2.15e+00          -3.41e+00
  2      300   -4.82e-02          -1.03e-01
  3      938   -2.11e-04          -7.28e-04
  4     2051   -8.24e-07          -3.15e-06
  5     3720   -2.67e-09          -1.42e-08
```

Only models with CURVED geometry show p-convergence. Planar models
(brick, webcut without curves) are exact at order 1.

## Verified Test Models (p-convergence passes)

| Model | Elements | Geometry | Element Types |
|-------|----------|----------|---------------|
| Cylinder Tet | 142 | Cylindrical surface | Tet |
| Cylinder Tet+BL | 199 | Cylindrical + BL | Tet + Wedge |
| Acorn (cyl+sphere) | 1935 | Sphere + cylinder | Tet + Wedge |
| Wedge Only | 144 | Cylindrical | Wedge |
| Cylinder Hex webcut | 96 | Cylindrical with cuts | Hex |
| Hex Only (planar) | 56 | Planar (exact at p=1) | Hex |
| Tet+Hex+Pyramid | 12 | Planar (exact at p=1) | Mixed |

## Known Issue: ACIS Loft Surface (05_loft)

**Problem**: Loft surfaces (rectangle to circle) in ACIS have
`closest_point_trimmed()` that overshoots the actual surface.

**Symptom**: Area error ~0.6% that does NOT converge with p-refinement.
Volume converges slowly but does converge.

**Workaround**: None. This is an ACIS/Cubit kernel limitation.
Volume convergence is acceptable. Use this geometry for volume-only tests.

## Known Behavior: Wedge/BL Volume = 0 in .vol.json

When boundary layer (BL) creates wedge elements, the .vol.json companion
file may report `0.000000e+00` volume for the BL material. This is because
Cubit's `RefVolume::measure()` returns the ACIS volume of the parent body,
and BL-generated thin layers may not have an independent ACIS volume.

This is NOT a bug. The mesh itself has correct wedge geometry.
NGSolve `Integrate(CF(1), mesh)` will return the correct volume.

## Known Issue: Hex BL on Cylinder (03) Export Failure

Hexahedral boundary layer on a cylinder with `scheme map` may fail
to export via `export netgen`. The combination of mapped hex meshing
with boundary layers on curved surfaces can produce elements that
NetgenCurver cannot process.

**Workaround**: Use tet meshing with BL (creates wedge prisms instead).

## BND Integrate Area Mismatch

`Integrate(CF(1), mesh, BND)` integrates ALL boundary surface elements,
including internal block-to-block interfaces. The .vol.json reports only
EXTERNAL surface areas from Cubit's ACIS geometry.

To get per-boundary area matching .vol.json, use per-label integration:
```python
for bnd_name, cad_area in cad["boundaries"].items():
    ng_area = Integrate(CF(1), mesh, BND,
                        definedon=mesh.Boundaries(bnd_name))
    error = (ng_area - cad_area) / cad_area * 100
```

Do NOT compare `Integrate(CF(1), mesh, BND)` (total) against
`sum(cad["boundaries"].values())` -- the total BND includes internal faces.

## .vol.json Companion File

Every `export netgen` produces a companion .vol.json:
```json
{
  "materials": {"sphere": 5.236e-04},
  "boundaries": {"surface_1": 3.142e-02},
  "edges": {"curve_1": 3.142e-01},
  "n_elements": 10359, "n_points": 2071, "order": 3
}
```

Use for:
- Automated p-convergence regression tests
- Standalone consistency checks without Cubit (`check-vol model.vol`; the
  sibling sidecar is auto-discovered when present)
- Strict application label checks (`check-vol model.vol --contract labels.json
  --strict-labels --report-json run/vol_check.json`)

Run the checker after export and before solver/Simulink initialization.
Material constants are validated by the application DesignSpec/configuration,
not inferred from `.vol` label names.
"""


def get_export_documentation(format: str = "all") -> str:
	"""Return export documentation by format name."""
	topics = {
		"overview": EXPORT_OVERVIEW,
		"curved": EXPORT_CURVED,
		"netgen": EXPORT_CURVED,  # Alias: old name redirects to export netgen
		"exodus": EXPORT_EXODUS,
		"comparison": EXPORT_COMPARISON,
		"decision_guide": EXPORT_DECISION_GUIDE,
		"p_convergence": P_CONVERGENCE_KNOWLEDGE,
	}

	format = format.lower().strip()
	if format == "all":
		return EXPORT_OVERVIEW + "\n\n" + EXPORT_COMPARISON
	elif format in topics:
		return topics[format]
	else:
		return (
			f"Unknown format: '{format}'. "
			f"Available: all, {', '.join(topics.keys())}"
		)
