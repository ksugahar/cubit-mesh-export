# cubit-mesh-export

Export high-order curved meshes from Coreform Cubit directly to Netgen/NGSolve
`.vol`. The standalone exporter also writes Nastran BDF (orders 1 and 2),
Gmsh MSH 4.2 for visualization and post-processing, and ELF/MAGIC MEG.
See the [package documentation](packages/cubit-mesh-export/README.md) and the
[JMAG JCF settings JSON guide (Japanese)](packages/cubit-mesh-export/docs/jcf_settings_ja.md).

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
