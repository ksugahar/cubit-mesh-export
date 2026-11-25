from cubit_mesh_export._layout import has_python_binding
import pytest


@pytest.mark.parametrize("init_present,native_present,expected", [
    (False, False, False), (True, False, False),
    (False, True, False), (True, True, True),
])
def test_packaged_binding_requires_both_files(tmp_path, init_present, native_present, expected):
    package = tmp_path / "cubit"
    package.mkdir()
    if init_present:
        (package / "__init__.py").touch()
    if native_present:
        (package / "_cubit3.pyd").touch()
    assert has_python_binding(tmp_path) is expected


# Guards: every entry point decides the supported Cubit release from BUILD_INFO
# through the one reader in _layout; a directory name is never evidence.
import ast
from pathlib import Path

from cubit_mesh_export import _layout, install, toolbar_install
from cubit_mesh_export.mcp import session

PACKAGE = Path(_layout.__file__).parent

# (directory name, BUILD_INFO text or None, supported)
RELEASE_CASES = [
    ("Coreform Cubit 2026.8", None, False),            # name only
    ("Coreform Cubit 2026.8", "short=2026.6\n", False),  # name lies
    ("renamed-cubit", "short=2026.8\r\n", True),        # renamed, exact release
    ("renamed-cubit", "# short=2026.8\nshort = 2026.8 \n", True),
    ("renamed-cubit", "version=2026.8\n", False),       # no short field
]


def _install_root(tmp_path, name, build_info):
    root = tmp_path / name
    (root / "bin" / "plugins").mkdir(parents=True)
    (root / "plugins").mkdir()
    (root / "bin" / "cubitx.exe").touch()
    (root / "bin" / "cubit.py").touch()
    if build_info is not None:
        (root / "BUILD_INFO").write_bytes(build_info.encode("utf-8"))
    return root


@pytest.mark.parametrize("name,build_info,supported", RELEASE_CASES)
def test_all_entry_points_agree_on_the_release(tmp_path, monkeypatch, name, build_info, supported):
    root = _install_root(tmp_path, name, build_info)
    monkeypatch.delenv("CUBIT_INSTALL_DIR", raising=False)
    monkeypatch.setenv("CUBIT_PATH", str(root))
    verdicts = {
        "layout": _layout.is_supported_cubit_root(root),
        "installer": install._find_cubit_dir() == root,
        "toolbar": toolbar_install._is_supported_cubit_bin(root / "bin"),
        "session": session.find_cubit_install(str(root / "bin")) == (root / "bin").resolve(),
    }
    assert verdicts == dict.fromkeys(verdicts, supported), (name, build_info, verdicts)


def test_only_layout_reads_build_info():
    readers = []
    for path in sorted(PACKAGE.rglob("*.py")):
        tree = ast.parse(path.read_text(encoding="utf-8"))
        if any(isinstance(node, ast.Constant) and node.value == "BUILD_INFO"
               for node in ast.walk(tree)):
            readers.append(path.relative_to(PACKAGE).as_posix())
    assert readers == ["_layout.py"], readers
