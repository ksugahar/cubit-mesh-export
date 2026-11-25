"""Content-addressed provenance for cubit-mesh-export native payloads."""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
import re
import subprocess
import zipfile
from importlib import metadata
from pathlib import Path


def _layout():
    """The package's single BUILD_INFO reader, loaded by path (this also runs as a script)."""
    import importlib.util
    spec = importlib.util.spec_from_file_location(
        "_cubit_mesh_export_layout", Path(__file__).with_name("_layout.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


SCHEMA ="cubit-mesh-export.native-payloads.v2"
HASH_FORMAT = "sha256-path-nul-lf-content-nul-v1"
REQUIRED_PAYLOADS = ("cubit_mesh_export.ccm", "cubit_mesh_curver.pyd")
SOURCE_SUFFIXES = {
    ".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx",
    ".cmake", ".txt", ".ps1",
}

def _is_build_tree(directory: Path) -> bool:
    """A CMake binary directory, whatever it is named, is generated output."""
    return (directory / "CMakeCache.txt").is_file() or (directory / "CMakeFiles").is_dir()


def native_source_files(repo_root: Path) -> list[Path]:
    """Return the deterministic set of files that can affect native builds."""

    repo_root = repo_root.resolve()
    plugin_root = repo_root / "src" / "cubit_plugin"
    files = []
    for directory, subdirectories, names in os.walk(plugin_root):
        here = Path(directory)
        subdirectories[:] = sorted(
            name for name in subdirectories
            if name != "__pycache__" and not _is_build_tree(here / name))
        files.extend(here / name for name in names
                     if Path(name).suffix.lower() in SOURCE_SUFFIXES)
    shared_discovery = repo_root / "tools" / "find_cubit.ps1"
    if shared_discovery.is_file():
        files.append(shared_discovery)
    return sorted(files, key=lambda path: path.relative_to(repo_root).as_posix())


def _canonical(content: bytes) -> bytes:
    # Git may check text files out as LF or CRLF on different Windows hosts.
    # Line endings do not change the compiled program, so hash and archive a
    # canonical LF representation to keep provenance portable.
    return content.replace(b"\r\n", b"\n").replace(b"\r", b"\n")


def _is_native_input(relative: str) -> bool:
    return (relative == "tools/find_cubit.ps1" or (
        relative.startswith("src/cubit_plugin/")
        and Path(relative).suffix.lower() in SOURCE_SUFFIXES))


def _digest_entries(entries) -> tuple[str, int]:
    """Digest (relative posix path, bytes) pairs in path order."""
    digest = hashlib.sha256()
    digest.update((HASH_FORMAT + "\0").encode("ascii"))
    count = 0
    for relative, content in sorted(entries):
        digest.update(relative.encode("utf-8"))
        digest.update(b"\0")
        digest.update(_canonical(content))
        digest.update(b"\0")
        count += 1
    return digest.hexdigest(), count


def native_source_digest(repo_root: Path) -> tuple[str, int]:
    """Hash normalized paths and bytes, never filesystem timestamps."""

    repo_root = repo_root.resolve()
    return _digest_entries(
        (path.relative_to(repo_root).as_posix(), path.read_bytes())
        for path in native_source_files(repo_root))


ARCHIVE_NAME = "cubit_mesh_export_native_source.zip"
ARCHIVE_IDENTITY = "native-source.json"
ARCHIVE_NOTICES = (
    "LICENSE",
    "packages/cubit-mesh-export/LICENSE-NETGEN.txt",
    "packages/cubit-mesh-export/LICENSE-PYBIND11.txt",
    "packages/cubit-mesh-export/NETGEN-AUTHORS.txt",
    "packages/cubit-mesh-export/NOTICE",
    "packages/cubit-mesh-export/THIRD_PARTY_NOTICES.md",
)
REBUILD_TEXT = """# Rebuild the Cubit command plugin

Use your own Coreform Cubit 2026.8 installation and its SDK. Set up a Visual
Studio 2022 x64 developer environment with MSVC 14.44.35207, CMake and Ninja.
Run these commands from the extracted archive. Replace the example Cubit
path with the directory containing your installed CubitConfig.cmake.

    cmake -S src/cubit_plugin -B build-ccm -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCubit_DIR=C:/Program Files/Coreform Cubit 2026.8/cmake"
    cmake --build build-ccm --target cubit_mesh_export_ccm --parallel 2

The result is build-ccm/cubit_mesh_export.ccm. To relink with changes, modify
the Compact Netgen sources in src/cubit_plugin/compact_netgen and repeat the
commands with a fresh build directory. Use an isolated Cubit plugin directory
to load and test your modified plugin.
"""


def _archive_identity(source: dict) -> dict:
    return {
        "source_commit": source["commit"],
        "native_tree_sha256": source["tree_sha256"],
        "native_input_count": source["file_count"],
    }


def _archive_members(repo_root: Path) -> list[tuple[str, bytes]]:
    """Every tracked file of the plugin tree (not only compiled inputs), plus notices."""
    tracked = _git(repo_root, "ls-files", "-z", "--", "src/cubit_plugin").split("\0")
    members = [name for name in tracked if name] + ["tools/find_cubit.ps1", *ARCHIVE_NOTICES]
    return [(name, (repo_root / name).read_bytes()) for name in sorted(set(members))]


def build_source_archive(repo_root: Path, source: dict) -> bytes:
    """The corresponding-source ZIP, byte-reproducible for one source tree."""
    entries = _archive_members(repo_root.resolve()) + [
        ("REBUILD.md", REBUILD_TEXT.encode("utf-8")),
        (ARCHIVE_IDENTITY, (json.dumps(_archive_identity(source), indent=2) + "\n").encode()),
    ]
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, content in sorted(entries):
            info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            archive.writestr(info, content if b"\0" in content else _canonical(content))
    return buffer.getvalue()


def source_archive_errors(data: bytes | None, source: dict) -> list[str]:
    """The archive is the recorded build source: hash, identity and inputs."""
    if data is None:
        return [f"corresponding source archive {ARCHIVE_NAME} is missing"]
    if hashlib.sha256(data).hexdigest() != source.get("archive_sha256"):
        return [f"{ARCHIVE_NAME} SHA-256 differs from native_payloads.json"]
    errors = []
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        try:
            identity = json.loads(archive.read(ARCHIVE_IDENTITY))
        except KeyError:
            return [f"{ARCHIVE_NAME} has no {ARCHIVE_IDENTITY}"]
        if identity != _archive_identity(source):
            errors.append(f"{ARCHIVE_NAME} identifies a different build: {identity}")
        digest, count = _digest_entries(
            (name, archive.read(name)) for name in archive.namelist()
            if _is_native_input(name))
    if (digest, count) != (source.get("tree_sha256"), source.get("file_count")):
        errors.append(
            f"{ARCHIVE_NAME} native inputs differ from the recorded tree: "
            f"actual={digest}/{count} recorded={source.get('tree_sha256')}/{source.get('file_count')}")
    return errors


def _file_evidence(path: Path) -> tuple[str, int]:
    data = path.read_bytes()
    return hashlib.sha256(data).hexdigest(), len(data)


NATIVE_SOURCE_PATHS = ("src/cubit_plugin", "tools/find_cubit.ps1")


def _git(repo_root: Path, *args: str) -> str:
    return subprocess.run(["git", "-C", str(repo_root), *args],
                          check=True, capture_output=True, text=True).stdout


def _source_commit(repo_root: Path) -> str:
    """The published commit the native payloads were built from.

    Evidence must name source that anyone can check out: the native tree has to
    be clean (no edits, no untracked files) and its last commit has to be on a
    remote-tracking branch. Commit and push first, then build and record.
    """
    dirty = _git(repo_root, "status", "--porcelain", "--untracked-files=all",
                 "--", *NATIVE_SOURCE_PATHS)
    if dirty.strip():
        raise RuntimeError(
            "native sources differ from the committed tree; commit and push "
            "them, rebuild, then record:\n" + dirty.rstrip())
    commit = _git(repo_root, "log", "-1", "--format=%H", "--",
                  *NATIVE_SOURCE_PATHS).strip()
    if len(commit) != 40:
        raise RuntimeError("could not resolve the native source commit")
    if not _git(repo_root, "branch", "-r", "--contains", commit).strip():
        raise RuntimeError(
            f"native source commit {commit} is not on any remote-tracking "
            "branch; push it before recording")
    return commit


def verify_manifest(repo_root: Path | None, package_dir: Path) -> list[str]:
    """Verify payloads; omit source comparison only for source-free sdists."""

    manifest_path = package_dir / "native_payloads.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    errors: list[str] = []
    if manifest.get("schema") != SCHEMA:
        errors.append(f"manifest schema must be {SCHEMA}")

    source = manifest.get("source", {})
    if source.get("hash_format") != HASH_FORMAT:
        errors.append(f"source hash_format must be {HASH_FORMAT}")
    if repo_root is not None:
        actual_source_hash, actual_file_count = native_source_digest(repo_root)
        if source.get("tree_sha256") != actual_source_hash:
            errors.append(
                "native source content differs from manifest: "
                f"actual={actual_source_hash} recorded={source.get('tree_sha256')}"
            )
        if source.get("file_count") != actual_file_count:
            errors.append(
                "native source file count differs from manifest: "
                f"actual={actual_file_count} recorded={source.get('file_count')}"
            )
    if not source.get("commit"):
        errors.append("native source commit is not recorded")
    archive = package_dir / ARCHIVE_NAME
    errors.extend(source_archive_errors(
        archive.read_bytes() if archive.is_file() else None, source))

    payloads = manifest.get("payloads", {})
    for name in REQUIRED_PAYLOADS:
        path = package_dir / name
        expected = payloads.get(name)
        if not isinstance(expected, dict):
            errors.append(f"manifest has no evidence for {name}")
            continue
        if not path.is_file():
            errors.append(f"required payload is missing: {path}")
            continue
        actual_hash, actual_size = _file_evidence(path)
        if expected.get("sha256") != actual_hash:
            errors.append(
                f"{name} SHA-256 differs: actual={actual_hash} "
                f"recorded={expected.get('sha256')}"
            )
        if expected.get("size") != actual_size:
            errors.append(
                f"{name} size differs: actual={actual_size} "
                f"recorded={expected.get('size')}"
            )
    return errors


def record_manifest(repo_root: Path, package_dir: Path) -> dict:
    """Record current native source and payload evidence after a real build."""

    manifest_path = package_dir / "native_payloads.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    source_hash, file_count = native_source_digest(repo_root)
    manifest["schema"] = SCHEMA
    manifest["source"] = {
        "hash_format": HASH_FORMAT,
        "tree_sha256": source_hash,
        "file_count": file_count,
        "commit": _source_commit(repo_root),
    }
    payloads = manifest.setdefault("payloads", {})
    for name in REQUIRED_PAYLOADS:
        path = package_dir / name
        if not path.is_file():
            raise FileNotFoundError(f"required payload is missing: {path}")
        sha256, size = _file_evidence(path)
        payload = payloads.setdefault(name, {})
        payload["sha256"] = sha256
        payload["size"] = size
        payload.setdefault("platform", "win_amd64")
        build_dir = "build-pyd" if name.endswith(".pyd") else "build-ccm"
        cache_path = repo_root / "src/cubit_plugin" / build_dir / "CMakeCache.txt"
        if not cache_path.is_file():
            raise ValueError(f"Missing Cubit SDK cache: {cache_path}")
        cache = cache_path.read_text(encoding="utf-8")
        sdk = re.search(r"^Cubit_DIR:[^=]+=([^\r\n]*)$", cache, re.MULTILINE)
        if not sdk or not sdk[1].strip():
            raise ValueError(f"Missing Cubit_DIR entry in {cache_path}")
        sdk_root = Path(sdk[1].strip()).parent
        layout = _layout()
        version = layout.cubit_release(sdk_root)
        if not version:
            raise ValueError(
                f"Missing Cubit SDK release: {layout.build_info_path(sdk_root)} has no short= value")
        payload["cubit_version"] = version
        configs = sorted((repo_root / "src/cubit_plugin" / build_dir / "CMakeFiles").glob(
            "*/CMakeCXXCompiler.cmake"), key=lambda item: item.stat().st_mtime)
        if configs:
            config = configs[-1].read_text(encoding="utf-8")
            compiler = re.search(r'set\(CMAKE_CXX_COMPILER_ID "([^"]+)"\)', config)
            version = re.search(r'set\(CMAKE_CXX_COMPILER_VERSION "([^"]+)"\)', config)
            if compiler and version:
                payload["toolchain"] = f"{compiler[1]} {version[1]} / CMake / Ninja"
            else:
                raise ValueError(f"Missing compiler identity in {configs[-1]}")
        else:
            payload["toolchain"] = "unrecorded (no CMake compiler metadata)"
        if name.endswith(".pyd"):
            payload["asset_name"] = f"cubit_mesh_curver-{sha256}.pyd"
            payload.setdefault("python_abi", "cp312")
            # The curver links the external Netgen C++ ABI; record the
            # version of the environment that is recording the build.
            payload["netgen_version"] = metadata.version("netgen-mesher")
    # Pack the archive last and write nothing until every check has passed.
    archive = build_source_archive(repo_root, manifest["source"])
    manifest["source"]["archive_sha256"] = hashlib.sha256(archive).hexdigest()
    archive_errors = source_archive_errors(archive, manifest["source"])
    if archive_errors:
        raise RuntimeError("; ".join(archive_errors))
    (package_dir / ARCHIVE_NAME).write_bytes(archive)
    manifest_path.write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=("verify", "record", "check-source"))
    parser.add_argument("--repo-root", type=Path, required=True)
    parser.add_argument("--package-dir", type=Path)
    args = parser.parse_args()
    if args.action == "check-source":
        try:
            commit = _source_commit(args.repo_root)
        except RuntimeError as error:
            print(f"ERROR: {error}")
            return 1
        print(f"native source commit {commit} is clean and pushed")
        return 0
    if args.package_dir is None:
        parser.error(f"{args.action} requires --package-dir")
    if args.action == "record":
        manifest = record_manifest(args.repo_root, args.package_dir)
        print(
            "recorded native provenance: "
            f"source={manifest['source']['tree_sha256']}"
        )
        return 0
    errors = verify_manifest(args.repo_root, args.package_dir)
    if errors:
        for error in errors:
            print(f"ERROR: {error}")
        return 1
    print("native source and payload provenance OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
