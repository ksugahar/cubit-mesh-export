# Reproduce the Designer API comparison

First complete the [Cubit and ExecSolver example](README.md). Keep
`model.jcf`, `model.jplot`, and the matching [settings.json](settings.json)
in the same directory. The comparison uses the documented 1,352-node,
6,107-tetrahedron mesh. Copy [run_designer.py](run_designer.py) and
[compare_designer.py](compare_designer.py) to your working directory.

Use JMAG-Designer 25.2 on Windows with a license for Designer and 3D static
magnetic analysis. The first command uses JMAG's bundled Python and the
installed Designer API. The comparison uses ordinary Python with NumPy
(already installed with cubit-mesh-export).

```powershell
$jmag = 'C:\Program Files\JMAG-Designer25.2'
$env:OMP_NUM_THREADS = '1'
$env:JMAG_PARALLEL_TYPE = $null
& (Join-Path $jmag 'python3.12\python.exe') run_designer.py model.jcf settings.json designer-reference
if ($LASTEXITCODE -ne 0) { throw 'The Designer reference did not finish.' }

$ref = @(Get-ChildItem designer-reference -Recurse -Filter Designer.jcf)
if ($ref.Count -ne 1) { throw 'Expected one Designer solver input.' }
python compare_designer.py model.jcf $ref[0].FullName
```

The first script starts a separate Designer instance and saves a new
project in `designer-reference`. It loads only the mesh and part names from
the JCF, applies the materials and 1.5 A / 10-turn excitation from
`settings.json` through the API, and selects the three bottom surfaces
using coordinates in mm. Designer applies its default symmetry type and
runs the static study. Keep the generated `Designer.jcf` and
`Designer.jplot` together.

The comparison checks that the input and result meshes correspond, then
checks the **same set of 314 exterior triangles** at z = 0 in the direct
and Designer solver inputs. It reconstructs **B** as the curl of the
saved edge vector potential on each linear tetrahedron and reports the
maximum vector difference in tesla and the relative L2 difference over
all **6,107 elements**. The reader is specific to the 25.2 files used in
this example. The [2.3.1 release notes](https://github.com/ksugahar/cubit-mesh-export/releases/tag/cubit-mesh-export-v2.3.1)
record a maximum difference of 9.28e-18 T and a relative L2 difference of
1.87e-15.

See the official [JMAG scripting documentation](https://www.jmag-international.com/products/jmag-designer/customization/scripts/)
for the Designer API.
