#!/usr/bin/env python3
"""Shared test helpers: repo-root path resolution.

r402: before this module, ~75 test files each repeated
``Path(__file__).resolve().parents[2]`` (two spellings:
``.parents[2]`` and ``.parent.parent.parent``) to locate the
mt-vgpu-guest repo root. Centralizing the computation here means the next
directory move touches one file instead of 75 (r400 had to bulk-edit 53
files for exactly this reason).

Import pattern -- works both under ``python -m unittest discover`` and
when a test file is executed directly::

    import sys
    from pathlib import Path
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
    from tests.helpers import get_repo_root

    ROOT = get_repo_root()
"""

from pathlib import Path


def get_repo_root() -> Path:
    """Absolute path of the mt-vgpu-guest repository root.

    Derived from this file's own location (``tests/helpers.py``), so the
    result is independent of the importing test's directory depth -- a
    test move from ``tests/`` to ``tests/pvr/`` needs no path fixups.
    """
    return Path(__file__).resolve().parents[1]


def get_tests_dir() -> Path:
    """Absolute path of the ``tests/`` directory."""
    return get_repo_root() / "tests"


def get_kernel_dir() -> Path:
    """Absolute path of the ``kernel/`` directory (kernel headers)."""
    return get_repo_root() / "kernel"


def get_scripts_dir() -> Path:
    """Absolute path of the ``scripts/`` directory."""
    return get_repo_root() / "scripts"


def get_reports_dir() -> Path:
    """Absolute path of the ``reports/`` directory."""
    return get_repo_root() / "reports"


def get_build_dir() -> Path:
    """Absolute path of the ``build/`` directory (build artifacts)."""
    return get_repo_root() / "build"


def get_kernel_header(name: str) -> Path:
    """Absolute path of a kernel header, e.g. ``get_kernel_header("mt_pvr_wire.h")``."""
    return get_kernel_dir() / name
