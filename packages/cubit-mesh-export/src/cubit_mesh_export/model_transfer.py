"""Send the active Cubit model to another program through a separately installed handler.

This package ships no transfer implementation. A handler package registers one entry point
in the group ``cubit_mesh_export.model_transfer``; the entry point name is the target (for
example ``femtet``) and its object is a callable ``transfer(cubit) -> dict`` that does the
whole transfer and returns a JSON-serializable summary. Installing that package is the opt-in.

Without exactly one handler for a target, :func:`transfer` raises
:class:`TransferUnavailable` and nothing else is tried.
"""
from __future__ import annotations

import json
from importlib.metadata import entry_points

GROUP = "cubit_mesh_export.model_transfer"


class TransferUnavailable(RuntimeError):
    pass


def handlers(target: str):
    """Installed entry points for ``target``."""
    return [ep for ep in entry_points(group=GROUP) if ep.name == target]


def _the_handler(target: str):
    found = handlers(target)
    if not found:
        raise TransferUnavailable(
            f"No handler is installed for {target!r}. Model transfer needs a separately "
            f"installed package that registers entry point {target!r} in group {GROUP!r}.")
    if len(found) > 1:
        raise TransferUnavailable(
            f"{len(found)} handlers are installed for {target!r} "
            f"({', '.join(sorted(ep.value for ep in found))}); keep exactly one.")
    return found[0]


def describe(target: str) -> str:
    """The handler that :func:`transfer` would call, or raise :class:`TransferUnavailable`."""
    return _the_handler(target).value


def transfer(target: str, cubit) -> dict:
    """Run the one installed handler for ``target`` on ``cubit`` and return its summary."""
    result = _the_handler(target).load()(cubit)
    if not isinstance(result, dict):
        raise TypeError(f"the {target!r} handler returned {type(result).__name__}, expected dict")
    json.dumps(result)   # the summary must be JSON; a TypeError here names the bad value
    return result
