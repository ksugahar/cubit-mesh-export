# Transfer a Webcut model from Cubit to COMSOL

Use MPHTXT to analyze a conforming TETRA10 mesh from Cubit in COMSOL.
The workflow begins with COMSOL’s mesh import.

## Quadratic mesh

In Cubit, put the tetrahedra in volume blocks and set each block's element
type to TETRA10. Use a separate block for each region to preserve its interface.
Create triangular sidesets for the boundaries you want to select in COMSOL.
Choose **Export → COMSOL (.mphtxt)** and specify the Cubit model length unit,
or call this from Cubit's Python:

```python
from cubit_mesh_export.comsol import export_mphtxt

export_mphtxt("model.mphtxt", meters_per_unit=0.001)  # Cubit coordinates in mm
```

The exporter writes coordinates in metres, preserves the existing quadratic
nodes and writes a JSON file that maps Cubit blocks and sidesets to geometric
entity indices in the MPHTXT. In an empty 3D component, import the file in a
Mesh sequence using an Import feature. The mesh route uses COMSOL's native mesh
format. See [COMSOL's supported formats](https://www.comsol.com/support/learning-center/article/supported-file-formats-76161).

The tested COMSOL import case is a straight two-region Webcut. The JSON is used
to restore named selections. Curved meshes still require COMSOL import validation.

Read back the imported domain and boundary IDs before creating physics
selections. Use the manifest and the imported face connectivity to resolve
the IDs. Check that the interface has elements from both domains and that both
tetrahedra use the same six face nodes. Select the fixed and loaded ends by the
verified boundary IDs. Use quadratic geometry for a curved structural or
thermal comparison and retain the same material values, loads and sample
locations in both models.
