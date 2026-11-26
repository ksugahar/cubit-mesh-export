---
name: cubit-command-plugin
description: |
  Build, load and ship a native Coreform Cubit C++ command plugin: a DLL
  that adds new command verbs (for example `export <format> "file"`) to
  Cubit's command parser, so they run from the command line, a journal
  file or `cubit.cmd(...)`. Use when a Cubit feature needs in-process C++
  (mesh writers that walk the whole mesh, geometry callbacks into ACIS,
  journal-replayable verbs), when a plugin fails to load or crashes
  Cubit, or when a plugin must be rebuilt for a new Cubit release. Not
  for GUI-only buttons or dialogs: use the Python/PySide6 toolbar path
  (`cubit_toolbar_guide`) for those.
---

# cubit-command-plugin

Distilled from the cubit-mesh-export native plugin (`cubit_mesh_export.ccm`,
2026) and verified against the Coreform Cubit 2026.8 SDK. The template
under `templates/` builds and passes `smoke.ps1` on Cubit 2026.8 with
MSVC 14.44.35207, so start from it instead of from memory.

Get the files with the `cubit_scaffold_command_plugin` tool, or copy
`templates/` by hand.

## 1. Decide first: does this need C++?

Use a C++ command plugin only when at least one holds:

- The verb must be journal-replayable and parsed like a built-in
  (`play x.jou`, `cubit.cmd('export foo "a.txt"')`).
- The work must run inside Cubit's process, for example walking the full
  mesh through `MeshExportInterface` or projecting nodes back onto ACIS
  geometry once per node.

Everything else belongs outside the plugin:

- GUI menus, buttons and dialogs: in-process Python/PySide6 toolbar.
- Format conversion, solver adapters and checks: ordinary Python,
  JSON in and JSON out, outside Cubit.

The plugin binds to the Cubit, ACIS and compiler ABI. Every Cubit
release can break it, so keep its surface as small as possible.

## 2. Anatomy (four pure virtuals plus one macro)

```
MyPlugin : CubitCommandInterface      get_keys(), create_command(key)
  CUBIT_PLUGIN(MyPlugin)              exports cubit_plugin_instance() + marker
HelloCommand : CubitCommand           get_syntax(), get_syntax_help(),
                                      get_help(), execute(CubitCommandData&)
```

- `CUBIT_PLUGIN(Class)` must appear in exactly one .cpp. It exports
  `cubit_plugin_instance()` and a string `cubit_plugin_verification;debug=no`.
  The loader scans the file for that string. Without the macro, the plugin
  is skipped silently. A Debug build carries `debug=yes` instead. Release
  Cubit does not support it, and mixing CRTs crashes Cubit.
- `get_keys()` returns internal keys only. `create_command(key)` returns
  `new` commands, which Cubit owns and deletes. Return `nullptr` for an
  unknown key.
- A command object persists between calls. Keep per-call state local to
  `execute()` or reset it there.
- `execute()` returns `false` on failure. Print the reason with
  `PRINT_ERROR` (from `CubitMessage.hpp`) first.
- Plugin verbs are parsed before Cubit's legacy handlers, so a verb that
  matches a built-in command masks it. Extending `export` with a new
  keyword (`export nodecount "f"`) is fine. Do not reuse a keyword that
  Cubit already exports, such as `stl`, `abaqus` or `exodus`.

## 3. Syntax strings (modified BNF)

| Form | Meaning |
|---|---|
| `<string:label='filename',help='<filename>'>` | required quoted string |
| `<value:label='order',help='<1 or 2>'>` | int or double |
| `[overwrite]` | optional keyword; neighbouring options in any order |
| `{dimension 2\|dimension 3}` / `{a\|b:label='x'}` | choice |
| `(...)`, `...` | group, repeat |
| also | `toggle`, `location`, `axis`, `plane`, `bounding_box` |

Read values in `execute()`:

```cpp
std::string f; data.get_string("filename", f);
int order = 1; data.get_value("order", order);    // false and unchanged if absent
bool ow = data.find_keyword("overwrite");
```

The complete grammar is documented in `<Cubit>/include/cubit/CubitCommandInterface.hpp`
(the `parserSyntax` page). The official examples are in `<Cubit>/sdk_examples/`
(CommandPlugin, CGMPlugin, GUIPlugin, CubitLib).

## 4. Reading the mesh

```cpp
std::unique_ptr<CubitBaseInterface, void(*)(CubitBaseInterface*)> raw(
    CubitInterface::get_interface("MeshExport"),
    [](CubitBaseInterface* p){ if (p) CubitInterface::release_interface(p); });
auto* iface = dynamic_cast<MeshExportInterface*>(raw.get());
iface->set_use_sequential_ids(false);  // keep Cubit's ids
iface->initialize_export();
```

- Always release the interface. The RAII holder covers every early return.
- `get_coords(start, n, x, y, z, ids)` and
  `get_block_elements(start, n, block, types, handles)` return batches.
  Loop until they return 0.
- `get_block_list` also returns an internal default block that holds
  elements outside any user block. To get the user's block ids, use
  `CubitInterface::parse_cubit_list("block", "all")` and skip the rest.
- For sidesets, use `CubitInterface::parse_cubit_list("sideset","all")`,
  `get_sideset_surfaces(id)` and `get_connectivity("tri", id)`. Free-mesh
  faces from Sculpt or Exodus have no parent surface, so read them
  directly.
- Cubit passes file names as UTF-8 `std::string`. On Japanese Windows,
  `std::filesystem::path(std::string)` decodes them as cp932, so the file
  fails to open. Build the path through UTF-16 with `utf8_path.hpp`.
  `smoke.ps1` writes into a `日本語` directory to prove this.
- Check `overwrite` yourself. Check the stream state after `close()`.

## 5. Build (Windows)

`templates/build.ps1` does all of this and fails on anything else:

- Release only. Cubit is Release, and Debug/Release CRT mixing crashes it.
- Use a pinned MSVC toolset (default 14.44.35207), selected through
  `vcvars64.bat -vcvars_ver=` and checked against `cl.exe`. There is no
  silent fallback.
- Use the CMake and Ninja that ship with VS2022. Pass
  `-DCubit_DIR=<Cubit>/cmake`, then call `find_package(Cubit REQUIRED CONFIG)`
  and link `cubiti cubit_util`. Read the release from `<Cubit>/BUILD_INFO`
  (`short=`), never from the directory name.
- Define `_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR`. Cubit loads its own
  older `MSVCP140.dll`. With MSVC 14.40 or later, a constexpr-constructed
  `std::mutex` crashes in `lock()` against that runtime. This hits any
  code that uses `std::mutex`, including vendored libraries.
- Use `/Zi` and `/DEBUG:FULL` in Release, plus
  `/PDBALTPATH:<name>.pdb`, so crash dumps can be symbolicated without
  leaking the build path.
- If the plugin needs geometry, link `cubit_geom`. In 2026.8 this is a
  split target: `find_package(CubitCGM)` plus an alias to
  `cubit_cgmcore`. Delay-load it (`/DELAYLOAD:cubit_geom.dll` plus
  `delayimp`) so the plugin loads before the geometry engine.
- Large vendored sources (for example a Netgen subset) need `/bigobj`.
  Use `/utf-8` for non-ASCII comments.
- "Could NOT find CubitGeom" during configure is harmless for a plugin
  that does not use geometry.

## 6. Loading

- Cubit loads plugins only at startup, from `CUBIT_PLUGIN_DIR` or from
  the install's plugin directory. In 2026.8 that directory is
  `<install>/plugins`, beside `bin/` (`bin/cubitx.exe` exists). Older
  releases use `bin/plugins`.
- In 2026.8 the loader scans `*.dll`. Older releases also took `.ccm`
  files. A package can keep `.ccm` as its canonical name and copy it as
  `.dll` when deploying.
- Headless runs: 2026.8 uses `cubitx.exe -nographics -batch -nojournal
  -noinitfile driver.jou` with `CUBIT_PLUGIN_DIR=<staged dir>`. Older
  releases use `coreform_cubit.com ... -commandplugindir <dir>`.
  - Close stdin. Under an MCP stdio server, stdin is the JSON-RPC pipe,
    and Cubit waits on it.
  - `-noinitfile` keeps the user's init file out of the run.
- Python started with `cubit.init([...])` (the in-process daemon) does
  not load C++ command plugins. Run plugin verbs through the console
  launcher. cubit-mcp's `session.run_headless_journal(commands,
  command_plugin_directory=dir)` does the staging for you.
- Test a rebuilt plugin from a staged copy, never by overwriting the
  deployed DLL. Another user's Cubit may have it open, and Windows locks
  loaded DLLs.
- Rename hazard: when a plugin file is renamed, delete the old file.
  Otherwise Cubit loads both and registers the verbs twice.
- A stale machine-wide `CUBIT_PLUGIN_DIR` can make Cubit exit with code 2
  and report `-commandplugindir` and its value as files it could not
  open. Fix the variable. Do not accept exit code 2 as success.

## 7. Verify

1. `build.ps1`: checks the marker (`debug=no`) inside the DLL and prints
   its SHA256.
2. `smoke.ps1`: stages the DLL, meshes a brick, and runs both verbs.
   Expected output: `SMOKE OK exit=0 hello=2 MYPLUGIN_EXPORT nodes=125
   blocks=1 elements=64` (Cubit 2026.8, brick x 1, size 0.25).
3. For a real writer, also read its output back with an independent
   reader (for example `check-vol` for Netgen `.vol`). A file that
   exists is not proof that the export is correct.

## 8. Ship

- Build only from a clean commit that has been pushed. Record the
  commit, the toolset, the Cubit `BUILD_INFO` and the SHA256 of each
  binary next to the payload.
- The installer copies the payload, compares SHA256 values after
  copying, deletes files with retired names, and offers a
  `--verify-only` mode.
- Support exactly one Cubit release. Moving to a new release replaces
  the supported version (rebuild and rerun the regression); it does not
  add a second one.
- Debug logging: write to a per-user file (for example
  `C:\temp\<plugin>_%USERNAME%.log`). A shared file at `C:\` is not
  writable for non-admin users.

## Failure table

| Symptom | Cause |
|---|---|
| Verb unknown, nothing in log | `CUBIT_PLUGIN` missing, wrong directory, `.ccm` on 2026.8, Cubit not restarted, or the run used `cubit.init` Python |
| Plugin missing or crashing from a Debug build | marker says `debug=yes`; Debug CRT against Release Cubit |
| Crash in `std::mutex::lock` | built with MSVC >= 14.40 without `_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR` |
| Crash on load after a Cubit update | ABI drift: rebuild against the new SDK |
| Verbs registered twice | an old renamed plugin file is still in the plugin directory |
| Built-in command stops working | the plugin syntax masks it; rename the keyword |
| Headless run hangs | stdin left open under an MCP or pipe parent |
| Exit code 2 with `-commandplugindir` "files" | stale machine-wide `CUBIT_PLUGIN_DIR` |
| Open fails only on Japanese paths | path built from a UTF-8 `std::string` without the UTF-16 conversion |
