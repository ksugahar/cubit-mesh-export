"""
cubit-mesh-export: Cubit plugin binaries and mesh consistency checking.

The Cubit plugin (cubit_mesh_export.ccm) provides the only mesh export command:
  export netgen "model.vol" order N   -- High-order curved .vol

Consistency checking (does NOT require Cubit):
  check-vol model.vol --strict-labels
  from cubit_mesh_export.check import check_consistency  # API

cubit-plugin-install deploys plugin binaries to Cubit.
"""

# Consumer construction/physics adapters remain separate from this package.
__version__ = "3.1.0"

__all__ = ["__version__"]
