# Cubit Native ABI Boundary

Scope: the native payloads of `cubit-mesh-export` and every consumer of their
output.

## Supported runtime

Only one combination is supported. Anything else is rejected, not tried.

| Component | Version |
|---|---|
| Coreform Cubit | 2026.8 |
| Netgen / NGSolve | 6.2.2608 with the pyramid apex clamp (upstream 13b47c3a9) and the base-edge blend for pyramids |
| CPython | 3.12, 64-bit Windows |

## Why the native plugin exists

High-order curving needs a geometry trampoline. `BuildCurvedElements` calls a
`CallbackGeometry` projection back into Cubit's ACIS kernel once per
high-order node, so that step must run in-process in C++. It is therefore
bound to the Cubit, ACIS, Netgen and compiler ABI, and a Cubit update can make
the plugin fail to load or crash.

## Rules

1. Keep the ABI-bound native surface minimal: mesh export and the curving
   trampoline only.
   - `cubit_mesh_export.ccm` compiles a vendored compact Netgen snapshot
     (`src/cubit_plugin/compact_netgen`: 6.2.2607 with the same two pyramid
     fixes), so it never loads a runtime `nglib.dll`.
   - `cubit_mesh_curver.pyd` links the Netgen C++ ABI of the runtime it runs
     in.
2. Keep everything else out of the native plugin.
   - CAD-capture consumers and adapters for solvers that mesh from CAD run
     outside Cubit as JSON-in/JSON-out Python, bound to the capture SHA256.
   - They need no curving trampoline and carry no Cubit ABI. The target mesher
     places its own high-order nodes.
3. Limit in-Cubit Python to short capture scripts through the public `cubit`
   module.
   - A capture records the Cubit version and the API calls it used.
   - Qt exists only inside Cubit's own runtime. Ordinary Python never loads
     PySide or PyQt.
4. A move to a new Cubit or Netgen release replaces the supported version
   rather than adding one.
   - Rebuild both payloads.
   - Rerun the curved-surface regression that records high-order node
     deviation from the CAD surface.
5. For CAD routes, verify the target's reconstructed radii and surfaces against
   the capture samples instead of relying on the trampoline.
6. A mesh with pyramids is valid only on the patched Netgen runtime.
   `check-vol` probes the runtime and raises, naming the missing fix, when:
   - a unit pyramid misses its volume by more than 1e-12 (apex clamp missing);
   - the scalar and vectorised maps disagree on a curved pyramid's base
     (base-edge blend missing).

## Not yet implemented

- A small `extern "C"` function table at the native boundary.
- A load-time check of Cubit, ACIS and Netgen build identity that fails with a
  clear error instead of crashing. Until it exists, a mismatch can still crash
  Cubit.
