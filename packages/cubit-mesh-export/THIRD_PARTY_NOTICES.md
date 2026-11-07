# Third-party notices

The Cubit command plugin (cubit_mesh_export.ccm) incorporates a modified
Compact Netgen snapshot by the Netgen contributors under LGPL-2.1.
The corresponding source and build recipe are in src/cubit_plugin, including
compact_netgen/netgen_src. See LICENSE-NETGEN.txt and the copyright notices in
those sources. Use the source revision identified by the release and its
native payload manifest.

The Python curver (cubit_mesh_curver.pyd) uses pybind11 headers under
BSD-3-Clause. See LICENSE-PYBIND11.txt. Its Netgen and NGSolve runtime packages
are installed separately and provide their own notices.

Other declared Python dependencies and optional extras are installed
separately and retain their own license terms.
