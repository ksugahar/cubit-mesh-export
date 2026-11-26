# Third-party notices

The Cubit command plugin (cubit_mesh_export.ccm) incorporates a modified
Compact Netgen snapshot from Netgen 6.2.2608 (96e5682f6ea43ba77ba3bb2ae4bb4bd1791f506e) under LGPL-2.1-only.
See NETGEN-AUTHORS.txt for upstream attribution.
The corresponding source and build recipe are in src/cubit_plugin, including
compact_netgen/netgen_src, which keeps only the upstream files the plugin
compiles and their header closure. See LICENSE-NETGEN.txt and the copyright notices in
those sources. The wheel includes cubit_mesh_export_native_source.zip with the corresponding
source and relink instructions. The native payload manifest records the source
revision, source tree digest and archive SHA-256.

The check-vol reader uses the Netgen and NGSolve runtime packages, installed
separately at version 6.2.2608 with their own notices. The native payload is
built from the source tree identified by the manifest.

Other declared Python dependencies and optional extras are installed
separately and retain their own license terms.
