"""Build and solve a fresh Designer 25.2 reference for the static example."""
import argparse
from copy import deepcopy
import csv
import json
import subprocess
import sys
import uuid
from pathlib import Path
from xml.etree import ElementTree as ET
from zipfile import ZIP_STORED, ZipFile

sys.path.append(str(Path(sys.executable).resolve().parents[1]))
from jmag.designer import designer

from compare_designer import case_mesh, planar_faces


def designer_pids():
    rows = subprocess.check_output(
        ["tasklist", "/FI", "IMAGENAME eq designer.exe", "/FO", "CSV", "/NH"],
        text=True, errors="replace",
    )
    return {int(row[1]) for row in csv.reader(rows.splitlines())
            if len(row) > 1 and row[0].lower() == "designer.exe"}


def mesh_only(source, output):
    # Use the already-exported mesh bytes. Write neutral part names and no loads.
    with ZipFile(source) as archive:
        mesh = archive.read("mesh.bin")
        root = ET.fromstring(archive.read("root.xml"))
    # Preserve the solver's unit and mesh controls, but give Designer two
    # neutral parts and remove every physical condition and entity group.
    root.remove(root.find("group_data"))
    conditions = root.find("condition_data")
    for condition in list(conditions):
        if condition.get("name") not in {"Control", "Step"}:
            conditions.remove(condition)
    materials = root.find("material_data")
    air = materials.find("material[@name='TestAir']")
    neutral_coil = deepcopy(air)
    neutral_coil.set("name", "TestCoil")
    neutral_coil.find("parameter[@name='material_id']").text = "2"
    materials.remove(materials.find("material[@name='TestCoil']"))
    materials.append(neutral_coil)
    with ZipFile(output, "x", compression=ZIP_STORED) as archive:
        archive.writestr("mesh.bin", mesh)
        archive.writestr("root.xml", ET.tostring(root, encoding="utf-8", xml_declaration=True))


def configure(study, model, library, settings, temporary_materials):
    for part, material in settings["block_materials"].items():
        data = settings["material_library"][material]
        name = "jcf_example_" + uuid.uuid4().hex
        custom = library.CreateCustomMaterial(name)
        temporary_materials.append(name)
        custom.SetValue("MagneticType", 13)  # Linear constant permeability
        custom.SetValue("MagneticPermOFF_RelativeReal", data["mu_r"])
        resistivity = data.get("resistivity")
        if resistivity is not None:
            custom.SetValue("Electric_ConductivityType", 1)
            custom.SetValue("Electric_ResistivityValueType", 0)
            custom.SetValue("Electric_ConstResistivity", resistivity)
        study.SetMaterialByName(part, name)
        assigned = study.GetMaterial(part)
        assigned.SetValue("UserConductivityType", 2 if resistivity is not None else 1)
        if resistivity is not None:
            assigned.SetValue("UserResistivityValue", resistivity)

    coil, = settings["coils"]
    current = study.CreateCondition("Current", "Reference current")
    for part in coil["blocks"]:
        current.AddPart(part)
    current.SetValue("Current", coil["current"])
    current.SetValue("Turn", coil["turns"])
    current.SetValue("DirectionType", 1)  # Direction and origin
    current.SetXYZPoint("Direction", *coil["direction"])
    current.SetXYZPoint("Origin", *coil["origin"])

    # Designer groups the 314 triangles into three planar model faces.
    # Probe the air frame, air in the bore, and coil leg, in model units (mm).
    selection = model.CreateSelection()
    face_ids = set()
    for x, y in [(-8.0, 0.0), (0.0, 0.0), (4.0, 0.0)]:
        selection.Clear()
        selection.SelectFaceByPosition(x, y, 0.0)
        bottom = []
        for i in range(selection.NumFaces()):
            face = selection.FaceID(i)
            normal = model.GetFaceNormalVector(face)
            if normal.GetZ() < -0.5:
                bottom.append(face)
        if len(bottom) != 1:
            raise RuntimeError(f"Expected one bottom face at ({x}, {y}), got {bottom}")
        face_ids.add(bottom[0])
    if len(face_ids) != 3:
        raise RuntimeError(f"Expected three distinct bottom faces, got {face_ids}")
    selection.Clear()
    for face in sorted(face_ids):
        selection.SelectFace(face)
    study.CreateCondition("SymmetryBoundary", "Reference symmetry").AddSelected(selection)


def run(source, settings_path, output):
    nodes, tets = case_mesh(source)
    if len(planar_faces(nodes, tets)) != 314:
        raise ValueError("Expected the 314-face bottom boundary of the documented model")
    settings = json.loads(settings_path.read_text(encoding="utf-8"))
    if settings["unit"] != "mm" or settings["boundaries"] != {"TestSym": "symmetry"}:
        raise ValueError("Use the example settings in mm")
    output.mkdir()
    mesh = output / "reference_mesh.jcf"
    project = output / "reference.jproj"
    mesh_only(source, mesh)

    existing = designer_pids()
    app = designer.CreateApplication()
    pid = designer.GetApplicationPid(app)
    if not pid or pid in existing or pid not in designer_pids():
        raise RuntimeError("Could not verify ownership of a new Designer instance")
    temporary_materials = []
    library = None
    try:
        app.Hide()
        app.NewProject("JCF static reference")
        print("Loading mesh", flush=True)
        app.LoadMesh(str(mesh))
        print("Mesh loaded", flush=True)
        model = app.GetModel(0)
        study = model.GetStudy(0)
        if study.GetScriptTypeName() != "Static" or study.NumConditions() != 0:
            raise RuntimeError("Expected a fresh static study without conditions")
        library = app.GetMaterialLibrary()
        configure(study, model, library, settings, temporary_materials)
        print("Conditions configured", flush=True)
        app.SaveAs(str(project))
        study.Run()
        if not study.HasResult():
            raise RuntimeError("Designer did not produce a result")
        app.Save()
    finally:
        try:
            for name in temporary_materials:
                library.DeleteCustomMaterial(name)
        finally:
            app.Quit()
    result, = project.with_suffix(".jfiles").rglob("Designer.jplot")
    if not result.with_suffix(".jcf").is_file():
        raise RuntimeError("Designer solver input is missing")
    print(result.parent)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("jcf", type=Path)
    parser.add_argument("settings", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    run(args.jcf.resolve(), args.settings.resolve(), args.output.resolve())
