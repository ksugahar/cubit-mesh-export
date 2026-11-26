"""Configured SDK identity must replace stale manifest metadata or fail closed."""
import importlib.util
import json
import subprocess
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location(
    "sdk_provenance_test",
    ROOT / "packages/cubit-mesh-export/src/cubit_mesh_export/_native_provenance.py")
provenance = importlib.util.module_from_spec(spec)
spec.loader.exec_module(provenance)


@pytest.fixture
def candidate(tmp_path, monkeypatch):
    package = tmp_path / "package"
    package.mkdir()
    (package / "native_payloads.json").write_text(json.dumps({
        "payloads": {name: {"cubit_version": "stale"}
                     for name in provenance.REQUIRED_PAYLOADS}}))
    for name in provenance.REQUIRED_PAYLOADS:
        (package / name).write_bytes(b"candidate")
    sdk = tmp_path / "arbitrary-sdk-location"
    (sdk / "cmake").mkdir(parents=True)
    for build in ("build-ccm",):
        (tmp_path / "src/cubit_plugin" / build).mkdir(parents=True)
    (tmp_path / "src/cubit_plugin/.gitignore").write_text("build-*/\n", encoding="utf-8")
    for name in ("tools/find_cubit.ps1", *provenance.ARCHIVE_NOTICES):
        (tmp_path / name).parent.mkdir(parents=True, exist_ok=True)
        (tmp_path / name).write_text(name + "\n", encoding="utf-8")
    subprocess.run(["git", "init", "-q", str(tmp_path)], check=True)
    subprocess.run(["git", "-C", str(tmp_path), "add", "-A"], check=True)
    monkeypatch.setattr(provenance, "_source_commit", lambda root: "test")
    return tmp_path, package, sdk


def configure(root, sdk, cache_type="PATH"):
    for build in ("build-ccm",):
        (root / "src/cubit_plugin" / build / "CMakeCache.txt").write_text(
            f"Cubit_DIR:{cache_type}={sdk / 'cmake'}\n", encoding="utf-8")


@pytest.mark.parametrize("sdk_version", ["2025.12", "2026.8"])
@pytest.mark.parametrize("cache_type", ["PATH", "UNINITIALIZED"])
def test_record_replaces_stale_sdk_from_build_info(candidate, sdk_version, cache_type):
    root, package, sdk = candidate
    configure(root, sdk, cache_type)
    (sdk / "BUILD_INFO").write_text(f"short={sdk_version}\n", encoding="utf-8")
    manifest = provenance.record_manifest(root, package)
    assert {p["cubit_version"] for p in manifest["payloads"].values()} == {sdk_version}


@pytest.mark.parametrize("build", ["build-ccm"])
@pytest.mark.parametrize("missing", ["cache", "entry", "empty-entry", "build-info", "short", "empty-short"])
def test_missing_sdk_identity_fails_without_rewriting_manifest(candidate, build, missing):
    root, package, sdk = candidate
    configure(root, sdk)
    info = sdk / "BUILD_INFO"
    info.write_text("short=2026.8\n", encoding="utf-8")
    cache = root / "src/cubit_plugin" / build / "CMakeCache.txt"
    if missing == "cache":
        cache.unlink()
    elif missing in ("entry", "empty-entry"):
        cache.write_text("" if missing == "entry" else "Cubit_DIR:PATH=\n")
    elif missing == "build-info":
        info.unlink()
    else:
        info.write_text("other=value\n" if missing == "short" else "short=\nother=value\n")
    before = (package / "native_payloads.json").read_bytes()
    expected = cache if missing in ("cache", "entry", "empty-entry") else info
    with pytest.raises(ValueError) as error:
        provenance.record_manifest(root, package)
    assert str(expected) in str(error.value)
    assert (package / "native_payloads.json").read_bytes() == before


def _run_git(root, *args):
    subprocess.run(["git", "-C", str(root), "-c", "user.name=t", "-c", "user.email=t@t",
                    *args], check=True, capture_output=True, text=True)


@pytest.fixture
def native_repo(tmp_path):
    """A checkout whose native source is committed, with a bare remote."""
    remote, root = tmp_path / "remote.git", tmp_path / "checkout"
    subprocess.run(["git", "init", "--bare", "-q", str(remote)], check=True)
    root.mkdir()
    _run_git(root, "init", "-q", "-b", "main")
    plugin = root / "src/cubit_plugin"
    plugin.mkdir(parents=True)
    (plugin / "a.cpp").write_text("int a;\n", encoding="utf-8")
    _run_git(root, "add", ".")
    _run_git(root, "commit", "-q", "-m", "native")
    _run_git(root, "remote", "add", "origin", str(remote))
    return root, plugin


def test_record_names_only_a_clean_pushed_native_commit(native_repo):
    """Guards: provenance never names a dirty tree or an unpublished commit."""
    root, plugin = native_repo
    with pytest.raises(RuntimeError, match="not on any remote-tracking branch"):
        provenance._source_commit(root)
    _run_git(root, "push", "-q", "origin", "main")
    _run_git(root, "fetch", "-q", "origin")
    head = subprocess.run(["git", "-C", str(root), "rev-parse", "HEAD"],
                          check=True, capture_output=True, text=True).stdout.strip()
    assert provenance._source_commit(root) == head
    (plugin / "a.cpp").write_text("int a = 1;\n", encoding="utf-8")
    with pytest.raises(RuntimeError, match="differ from the committed tree"):
        provenance._source_commit(root)
    _run_git(root, "checkout", "--", ".")
    (plugin / "new.hpp").write_text("// untracked\n", encoding="utf-8")
    with pytest.raises(RuntimeError, match="new.hpp"):
        provenance._source_commit(root)
