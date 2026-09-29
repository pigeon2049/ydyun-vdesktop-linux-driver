#!/usr/bin/env python3
"""Inspect the cached Ghidra corpus without importing or analyzing a binary again."""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
CORPUS = ROOT / "decompiled"


def read(path):
    return json.loads(path.read_text()) if path.exists() else {}


def rows(path):
    with path.open() as stream:
        for line in stream:
            yield json.loads(line)


def inventory_problems(manifest):
    """Require the manifest to cover exactly the input SYS/DLL inventory."""
    problems = []
    items = manifest.get("files", {})
    try:
        source_dir = Path(manifest["input"])
        expected = {p.name for p in source_dir.iterdir() if p.suffix.lower() in (".sys", ".dll")}
        recorded = set(items)
        if len(expected) != manifest.get("file_count"):
            problems.append(f"Input inventory has {len(expected)} binaries; manifest says {manifest.get('file_count')}")
        missing = sorted(expected - recorded)
        unexpected = sorted(recorded - expected)
        if missing:
            problems.append("Missing binaries: " + ", ".join(missing))
        if unexpected:
            problems.append("Unexpected binaries: " + ", ".join(unexpected))
    except (KeyError, OSError) as exc:
        problems.append("Input inventory unavailable: " + str(exc))
    return problems


def status():
    manifest = read(CORPUS / "manifest.json")
    done = good = failed = 0
    for name, item in sorted(manifest.get("files", {}).items()):
        progress = read(CORPUS / name / "progress.json")
        phase = item["status"]
        if phase == "analyzing" and progress.get("phase") == "decompiling":
            phase = f"decompiling {progress['attempted']}/{progress['total_functions']}"
        if phase == "completed":
            done += 1
            good += progress.get("succeeded", 0)
            failed += progress.get("failed", 0)
        print(f"{name:22s} {phase:28s} ok={progress.get('succeeded', 0)} failed={progress.get('failed', 0)}")
    print(f"Completed {done}/{manifest.get('file_count', 0)} binaries; {good} decompiled functions; {failed} failed functions in completed binaries.")


def verify():
    manifest = read(CORPUS / "manifest.json")
    problems = []
    total = 0
    items = manifest.get("files", {})
    if not manifest.get("finished") or len(items) != manifest.get("file_count"):
        problems.append("Batch has not finished")
    problems.extend(inventory_problems(manifest))
    for name, item in sorted(items.items()):
        try:
            out = CORPUS / name
            if item["status"] != "completed":
                problems.append(name + ": not completed")
                continue
            progress = read(out / "progress.json")
            entries = list(rows(out / "functions.jsonl"))
            functions = [e for e in entries if not e.get("external")]
            externals = [e for e in entries if e.get("external")]
            failures = list(rows(out / "failures.jsonl"))
            assert len(entries) == progress["total_functions"], "function count mismatch"
            assert len(functions) == progress["attempted"], "attempt count mismatch"
            assert len(externals) == progress["external_functions"], "external count mismatch"
            assert len(failures) == progress["failed"], "failure count mismatch"
            assert sum(f["status"] == "decompiled" for f in functions) == progress["succeeded"], "success count mismatch"
            assert len({f["address"] for f in functions}) == len(functions), "duplicate function address"
            starts = {e["c_line_start"]: e for e in functions}
            count = 0
            with (out / "decompiled.c").open() as code:
                for number, line in enumerate(code, 1):
                    if number - 1 in starts:
                        e = starts[number - 1]
                        assert line.startswith("/* FUNCTION " + e["address"] + " "), "C index address mismatch"
                    count = number
            if functions:
                last = functions[-1]
                assert last["c_line_start"] + last["c_line_count"] - 1 == count, "C export truncated"
            source = Path(manifest["input"]) / name
            digest = hashlib.file_digest(source.open("rb"), "sha256").hexdigest()
            assert digest == item["sha256"], "source changed"
            assert (ROOT / "ghidra-projects" / name / "driver.gpr").is_file(), "saved Ghidra project missing"
            total += progress["succeeded"]
        except (AssertionError, KeyError, OSError, ValueError) as exc:
            problems.append(name + ": " + str(exc))
    report = {"binary_count": len(items), "successful_functions": total, "problems": problems,
              "integrity_verified": not problems}
    (ROOT / "reports/decompilation-verification.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return int(bool(problems))


def function(binary, query):
    out = CORPUS / binary
    # Binary must be a corpus member, not an arbitrary path.
    if binary not in read(CORPUS / "manifest.json").get("files", {}):
        raise SystemExit("Unknown corpus binary")
    normalized = query.lower().removeprefix("0x").lstrip("0")
    # Targeted callback recovery is exported separately from the immutable
    # original batch. Prefer corrected entries when both exports contain one.
    for supplemental in sorted(out.glob("recovered-*/functions.json")):
        for entry in read(supplemental):
            if entry.get("status") == "rejected_string_data" and entry["address"].lower().lstrip("0") == normalized:
                print(json.dumps({**entry, "supplemental_export": str(supplemental)}, ensure_ascii=False))
                print("Classified as string data; speculative pseudocode is excluded.")
                return
            if entry.get("status") != "decompiled":
                continue
            if entry.get("name") == query or entry["address"].lower().lstrip("0") == normalized:
                filename = entry.get("file", "")
                if Path(filename).name != filename or not filename.endswith(".c"):
                    raise SystemExit("Invalid supplemental corpus path")
                print(json.dumps({**entry, "supplemental_export": str(supplemental)}, ensure_ascii=False))
                print((supplemental.parent / filename).read_text(), end="")
                return
    matches = [e for e in rows(out / "functions.jsonl") if
               e["name"] == query or e["address"].lower().lstrip("0") == normalized]
    if not matches:
        raise SystemExit("Function not found")
    for entry in matches:
        print(json.dumps(entry, ensure_ascii=False))
        if "c_line_start" in entry:
            with (out / "decompiled.c").open() as code:
                begin = entry["c_line_start"] - 1
                for line in itertools.islice(code, begin, begin + entry["c_line_count"]):
                    print(line, end="")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("status")
    commands.add_parser("verify")
    f = commands.add_parser("function")
    f.add_argument("binary")
    f.add_argument("address_or_name")
    args = parser.parse_args()
    if args.command == "status":
        status()
    elif args.command == "verify":
        return verify()
    else:
        function(args.binary, args.address_or_name)
    return 0


if __name__ == "__main__":
    sys.exit(main())
