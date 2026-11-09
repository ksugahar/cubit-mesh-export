"""Contracts for the exporter's single LF .vol writer.

Guards: every committed .vol fixture is LF text, and no exporter source writes a
mesh to a path except through ``save_native_mesh``. Netgen's .vol.gz reader keeps
a CR, so a CRLF .vol turns stored region names into ``"body\r"``.
Assumes: plugin sources live in ``src/cubit_plugin``; the vendored Netgen
snapshot (``compact_netgen``) owns ``Mesh::Save`` itself and is excluded.
"""
import gzip
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
PLUGIN = ROOT / "src" / "cubit_plugin"
WRITER = PLUGIN / "NativeVolWriter.hpp"
SAVE_CALL = re.compile(r"(?:->|\.)Save\s*\(\s*([^()]*(?:\([^()]*\))?[^()]*)\)")


def _exporter_sources():
    for path in sorted(PLUGIN.rglob("*")):
        relative = path.relative_to(PLUGIN).parts
        if path.suffix in {".cpp", ".hpp", ".h"} and relative[0] != "compact_netgen" \
                and not relative[0].startswith("build"):
            yield path


def _serialized_streams(text):
    """In-memory streams, and file streams opened in binary mode."""
    return set(re.findall(r"std::ostringstream\s+(\w+)\s*;", text)) | set(
        re.findall(r"std::ofstream\s+(\w+)\s*\([^;]*std::ios::binary[^;]*\)\s*;", text))


def test_only_the_native_writer_saves_a_mesh_to_a_path():
    offenders = []
    for path in _exporter_sources():
        if path == WRITER:
            continue
        text = path.read_text(encoding="utf-8")
        streams = _serialized_streams(text)
        for match in SAVE_CALL.finditer(text):
            line_start = text.rfind("\n", 0, match.start()) + 1
            if text[line_start:match.start()].lstrip().startswith("//"):
                continue
            argument = match.group(1).strip()
            if argument not in streams:
                line = text.count("\n", 0, match.start()) + 1
                offenders.append(f"{path.relative_to(ROOT).as_posix()}:{line}: Save({argument})")
    assert offenders == [], offenders


def test_native_writer_serializes_in_binary_and_rejects_carriage_returns():
    text = WRITER.read_text(encoding="utf-8")
    assert "std::ios::binary" in text
    assert r"find('\r')" in text


def _tracked_vol_fixtures():
    listed = subprocess.run(
        ["git", "-C", str(ROOT), "ls-files", "*.vol", "*.vol.gz"],
        check=True, capture_output=True, text=True).stdout.split()
    return [ROOT / name for name in listed]


def test_committed_vol_fixtures_are_lf_text():
    fixtures = _tracked_vol_fixtures()
    assert fixtures, "no committed .vol fixture found"
    counts = {}
    for path in fixtures:
        data = gzip.open(path).read() if path.suffix == ".gz" else path.read_bytes()
        counts[path.relative_to(ROOT).as_posix()] = data.count(b"\r")
    assert counts == {name: 0 for name in counts}, counts
