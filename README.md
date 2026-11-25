# cubit-mesh-export

Export high-order curved meshes from Coreform Cubit directly to Netgen/NGSolve
`.vol`, the only output format.
See the [package documentation](packages/cubit-mesh-export/README.md).

Build the Python wheel with `python -m build --wheel packages/cubit-mesh-export`.
The build verifies the native source and payload manifest. It requires the
manifest-pinned native curver beside the tracked command plugin. The release
workflow fetches that asset from this repository's `binaries` release and checks
its SHA-256 and size. The native build entry is `src/cubit_plugin/cubit_build.ps1`.

Releases retain the existing PyPI project name and use
`cubit-mesh-export-v<version>` tags and the protected `pypi` environment.

Run source checks from this checkout with `python -m pytest`. The release
workflow runs its explicit dependency and platform checks in an isolated
runner. To exercise the offscreen Qt dialogs, run
`python tools/audit_pyside6_only.py --licensed-smoke` on the licensed Cubit 2026.8 host.
The separate GUI cold-start check requires the explicit
`CUBIT_MESH_EXPORT_RUN_GUI_TESTS=1` opt-in.
