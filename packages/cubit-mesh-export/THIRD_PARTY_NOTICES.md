# Third-party notices

The Cubit command plugin (cubit_mesh_export.ccm) incorporates a modified
Compact Netgen snapshot from Netgen 6.2.2608 (96e5682f6ea43ba77ba3bb2ae4bb4bd1791f506e) under LGPL-2.1-only.
See NETGEN-AUTHORS.txt for upstream attribution.
The corresponding source and build recipe are in src/cubit_plugin, including
compact_netgen/netgen_src. See LICENSE-NETGEN.txt and the copyright notices in
those sources. The wheel includes cubit_mesh_export_native_source.zip with the corresponding
source and relink instructions. The native payload manifest records the source
revision, source tree digest and archive SHA-256.

The Python curver (cubit_mesh_curver.pyd) uses pybind11 headers under
BSD-3-Clause. See LICENSE-PYBIND11.txt. Its Netgen and NGSolve runtime packages
are installed separately at version 6.2.2608 and provide their own notices.
Both native payloads are built from the source tree identified by the manifest.

Other declared Python dependencies and optional extras are installed
separately and retain their own license terms.
