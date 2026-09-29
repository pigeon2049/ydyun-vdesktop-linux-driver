#!/usr/bin/env python3
"""Add the Guest VPU heap-alias wrapper to a private build copy."""

from __future__ import annotations

import argparse
import json
import shutil
from pathlib import Path


WRAPPER_REL = Path("src/pvr/mtgpu_guest_vpu_heap_alias.c")
OBJECT_REL = "src/pvr/mtgpu_guest_vpu_heap_alias.o"


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("build_dir", type=Path)
	parser.add_argument("wrapper_source", type=Path)
	args = parser.parse_args()
	build_dir = args.build_dir.resolve()
	makefile = build_dir / "Makefile"
	source = args.wrapper_source.resolve()
	destination = build_dir / WRAPPER_REL
	if not makefile.is_file() or not source.is_file():
		parser.error("build directory is missing its Makefile or wrapper source")
	text = makefile.read_text()
	if OBJECT_REL in text or destination.exists():
		parser.error("Guest VPU heap-alias wrapper is already present")

	destination.parent.mkdir(parents=True, exist_ok=True)
	shutil.copyfile(source, destination)
	with makefile.open("a") as stream:
		stream.write(f"\nmtgpu-objs += {OBJECT_REL}\n")
	report = {
		"wrapper_source": str(destination),
		"object": OBJECT_REL,
		"hardware_touched": False,
	}
	(build_dir / "guest-vpu-heap-alias-preparation.json").write_text(
		json.dumps(report, indent=2) + "\n")
	print(json.dumps(report, indent=2))
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
