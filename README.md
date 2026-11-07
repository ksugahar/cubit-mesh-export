# cubit-mesh-export

Standalone mesh and CAD export tools for Coreform Cubit. See the
[package documentation](packages/cubit-mesh-export/README.md) for use.

Build the Python wheel with `python -m build --wheel packages/cubit-mesh-export`.
The build verifies the native source and payload manifest. It requires the
manifest-pinned native curver beside the tracked command plugin. The release
workflow fetches that asset from this repository's `binaries` release and checks
its SHA-256 and size. The native build entry is `src/cubit_plugin/cubit_build.ps1`.

Releases retain the existing PyPI project name and use
`cubit-mesh-export-v<version>` tags and the protected `pypi` environment.

Run source checks from this checkout with `python -m pytest`. The release
workflow runs its explicit dependency and platform checks in an isolated
runner. The Cubit menu contract can also be exercised with
`python validation_test/panels/test_cubit_export_menu.py` in an environment
with PySide6. The cold-start check requires the explicit
`CUBIT_MESH_EXPORT_RUN_GUI_TESTS=1` opt-in and a licensed Cubit setup.
