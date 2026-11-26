"""The packaged cubit-command-plugin skill and its scaffold tool."""
import re

import pytest

from cubit_mesh_export.mcp import server
from cubit_mesh_export.mcp.knowledge import command_plugin_skill as skill
from cubit_mesh_export.mcp.knowledge.cpp_sdk import get_cpp_sdk_documentation


def test_skill_has_frontmatter_and_names_the_verified_release():
    text = skill.get_command_plugin_skill()
    assert text.startswith("---\nname: cubit-command-plugin\n")
    assert "Cubit 2026.8" in text
    assert "_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR" in text


@pytest.mark.parametrize("topic", ["command_plugin", "plugin", "skill", "ccm"])
def test_cpp_sdk_guide_serves_the_skill(topic):
    assert get_cpp_sdk_documentation(topic) == skill.get_command_plugin_skill()


def test_cpp_sdk_guide_serves_every_template():
    text = get_cpp_sdk_documentation("templates")
    for name in skill.TEMPLATE_FILES:
        assert f"## templates/{name}" in text


def test_custom_commands_topic_uses_the_real_sdk_api():
    text = get_cpp_sdk_documentation("custom_commands")
    assert "CUBIT_PLUGIN(MyPlugin)" in text
    assert "execute(CubitCommandData& data)" in text
    assert "command_parser()" not in text and "CubitStatus execute" not in text


def test_template_implements_the_sdk_contract():
    plugin = skill.read_template("MyPlugin.cpp")
    assert len(re.findall(r"^CUBIT_PLUGIN\(MyPlugin\)", plugin, re.M)) == 1
    for header in ("HelloCommand.hpp", "ExportNodeCountCommand.hpp"):
        body = skill.read_template(header)
        for method in ("get_syntax()", "get_syntax_help()", "get_help()",
                       "execute(CubitCommandData& data)"):
            assert method in body, (header, method)
    cmake = skill.read_template("CMakeLists.txt")
    assert "find_package(Cubit REQUIRED CONFIG)" in cmake
    assert "_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR" in cmake
    assert "cubit_plugin_verification;debug=no" in skill.read_template("build.ps1")


def test_scaffold_copies_bytes_and_refuses_overwrite(tmp_path):
    target = tmp_path / "plug"
    written = skill.scaffold_command_plugin(target)
    assert {p.name for p in written} == {*skill.TEMPLATE_FILES, "SKILL.md"}
    for name in skill.TEMPLATE_FILES:
        assert (target / name).read_text(encoding="utf-8") == skill.read_template(name)
    with pytest.raises(FileExistsError):
        skill.scaffold_command_plugin(target)


def test_scaffold_tool_validates_and_writes(tmp_path):
    assert server.cubit_scaffold_command_plugin(str(tmp_path), "../x").startswith("ERROR")
    assert server.cubit_scaffold_command_plugin(str(tmp_path / "missing")).startswith("ERROR")
    out = server.cubit_scaffold_command_plugin(str(tmp_path), "demo")
    assert "Command-plugin template written to" in out
    assert (tmp_path / "demo" / "smoke.ps1").is_file()
    assert server.cubit_scaffold_command_plugin(str(tmp_path), "demo").startswith("ERROR")


def test_scaffold_tool_is_classified_as_writing():
    assert "cubit_scaffold_command_plugin" not in server._UNCLASSIFIED_TOOLS
    assert "cubit_scaffold_command_plugin" in server._WRITING_TOOLS
