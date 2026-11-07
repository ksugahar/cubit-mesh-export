# Third-party notices

The Cubit command plugin (cubit_mesh_export.ccm) incorporates a modified
Compact Netgen snapshot from Netgen 6.2.2607 (9642315f7a930701df33c917c537a51a4f3b46cd) under LGPL-2.1-only.
See NETGEN-AUTHORS.txt for upstream attribution.
The corresponding source and build recipe are in src/cubit_plugin, including
compact_netgen/netgen_src. See LICENSE-NETGEN.txt and the copyright notices in
those sources. Use the source revision identified by the release and its
native payload manifest.

The Python curver (cubit_mesh_curver.pyd) uses pybind11 headers under
BSD-3-Clause. See LICENSE-PYBIND11.txt. Its Netgen and NGSolve runtime packages
are installed separately at version 6.2.2608 and provide their own notices.
Both native payloads are built from the source tree identified by the manifest.

Other declared Python dependencies and optional extras are installed
separately and retain their own license terms.

## Derived source

`packages/cubit-mesh-export/src/cubit_mesh_export/jcf_writer.py` is derived from the
owner's static solver-input builder, distributed separately under the MIT License:

> Copyright (c) 2026 Kengo Sugahara
>
> Permission is hereby granted, free of charge, to any person obtaining a copy of this
> software and associated documentation files (the "Software"), to deal in the Software
> without restriction, including without limitation the rights to use, copy, modify, merge,
> publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
> to whom the Software is furnished to do so, subject to the following conditions:
>
> The above copyright notice and this permission notice shall be included in all copies or
> substantial portions of the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
> INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
> PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
> FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
> OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
> DEALINGS IN THE SOFTWARE.
