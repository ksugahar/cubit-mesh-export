"""Source contract for the independently owned native Cubit plugin."""
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[3]
PLUGIN = ROOT / "src" / "cubit_plugin"


def test_native_plugin_registers_only_maintained_commands_and_product_owned_identity():
    registry = (PLUGIN / "CubitMeshExportPlugin.cpp").read_text(encoding="utf-8")

    keys = re.findall(r'keys\.push_back\("(\w+)"\)', registry)
    assert keys == ["ExportNetgenCommand"], keys
    for retired in ("Gmsh", "Nastran", "Vtk", "Femeem", "Meg", "ModelCapture"):
        assert not (PLUGIN / f"Export{retired}Command.cpp").exists(), retired
        assert not (PLUGIN / f"Export{retired}Command.hpp").exists(), retired
    assert "CubitMeshExportPlugin" in registry
    assert "CoilCommand" not in registry
    assert not (PLUGIN / "CoilCommand.cpp").exists()
    assert not (PLUGIN / "CoilCommand.hpp").exists()


def test_native_sources_use_product_owned_helpers():
    cmake = (PLUGIN / "CMakeLists.txt").read_text(encoding="utf-8")
    message_filter = (PLUGIN / "LearnEditionMessageFilter.hpp").read_text(
        encoding="utf-8"
    )

    assert "project(CubitMeshExportPlugin)" in cmake
    assert "namespace cubit_mesh_export" in message_filter
    assert "RADIA_NETGEN_" not in cmake

    native_source = "\n".join(
        path.read_text(encoding="utf-8", errors="replace")
        for pattern in ("*.cpp", "*.hpp")
        for path in PLUGIN.glob(pattern)
    )
    assert "RADIA_" not in native_source
    assert "radia_kelvin_config" not in native_source
