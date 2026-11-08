# Reproduce the static JCF example

This synthetic case uses a rectangular ring coil in an air box. Cubit builds
and meshes the two volumes from scratch. The JSON assigns materials to
`TestAir` and `TestCoil`, applies 1.5 A with 10 turns to the coil, and applies
symmetry to the exterior `TestSym` sideset on z = 0. Coordinates in the JSON
are in mm.

Use Windows with Cubit 2026.8, 64-bit CPython 3.12, and JMAG ExecSolver 25.2
with a license for 3D static magnetic analysis. Run the commands in a fresh
working directory containing [model.jou](model.jou) and
[settings.json](settings.json).

## 1. Build the mesh and export JCF

Install the released exporter and its dependencies into your Python
environment. The wheel pins the Netgen dependency.

```powershell
python -m pip install "cubit-mesh-export==2.3.1"
$cubitBin = 'C:\Program Files\Coreform Cubit 2026.8\bin'
$acisBin = Join-Path (Split-Path $cubitBin) 'acis\code\bin'
$env:Path = "$acisBin;$env:Path"
& (Join-Path $cubitBin 'cubitx.exe') -batch -nographics -nojournal -noinitfile -workingdir (Get-Location).Path -input model.jou
```

The journal saves the meshed model as `model.cub5`. Save this Python snippet
as `export_jcf.py` in the same directory. Adjust the Cubit path if needed.

```python
import json
import os
from pathlib import Path
import sys

cubit_bin = r"C:\Program Files\Coreform Cubit 2026.8\bin"
cubit_dlls = os.add_dll_directory(cubit_bin)
sys.path.insert(0, cubit_bin)

import cubit
from cubit_mesh_export.jmag import export_jcf

cubit.init(["cubit", "-nographics", "-batch", "-nojournal", "-noinitfile"])
if not cubit.cmd('open "model.cub5"'):
    raise RuntimeError("Cubit could not open model.cub5")

settings = json.loads(Path("settings.json").read_text(encoding="utf-8"))
result = export_jcf("model.jcf", settings)
print(result)
```

Run `python export_jcf.py`. In the verified run, the JCF contained 1,352
nodes, 6,107 tetrahedra and 314 symmetry faces. Mesh counts can vary if a
different Cubit build generates a different mesh.

## 2. Solve with JMAG ExecSolver 25.2

Run this in a dedicated PowerShell session. Adjust the installation path if
needed. Keep the JCF's serial settings for this example.

```powershell
$env:InsDir = 'C:\Program Files\JMAG-Designer25.2'
$env:WorkDir = (Get-Location).Path
$env:OMP_NUM_THREADS = '1'
$env:JMAG_PARALLEL_TYPE = $null
& (Join-Path $env:InsDir 'ExecSolver.exe') (Resolve-Path 'model.jcf').Path
if ($LASTEXITCODE -ne 0) {
    throw "ExecSolver failed with exit code $LASTEXITCODE. See model.jlog."
}
```

Check `model.jlog` for the completed static step and `model.jplot` for the
saved result. The `CURRENT_of_Current_Control` channel in `jmonitor` should
report 1.5 A.

Follow the [Designer API comparison](designer-comparison.md) to reproduce the static reference solve and compare the 314 symmetry faces and magnetic flux density across all 6,107 elements.
