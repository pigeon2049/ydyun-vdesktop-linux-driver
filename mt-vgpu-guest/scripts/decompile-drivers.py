#!/usr/bin/env python3
"""Persistent, hash-keyed Ghidra corpus export of every SYS/DLL in the reference package."""
import argparse
import concurrent.futures
from datetime import datetime, timezone
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import threading

ROOT = Path(__file__).resolve().parents[1]
LOCK = threading.Lock()


def now():
    return datetime.now(timezone.utc).isoformat()


def save(path, data):
    temp = path.with_suffix(path.suffix + ".tmp")
    temp.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n")
    temp.replace(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, default=Path("/opt/MTT-driver-only"))
    parser.add_argument("--ghidra", type=Path, default=ROOT / "tools/ghidra_12.1.4_PUBLIC")
    parser.add_argument("--workers", type=int, choices=[1, 2], default=2)
    parser.add_argument("--only", nargs="+", help="Exact filenames for a targeted run")
    args = parser.parse_args()
    headless = args.ghidra.resolve() / "support/analyzeHeadless"
    if not headless.is_file():
        parser.error("Ghidra analyzeHeadless not found")
    corpus = ROOT / "decompiled"
    corpus.mkdir(exist_ok=True)
    lockfile = (corpus / ".runner.lock").open("w")
    try:
        fcntl.flock(lockfile, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        parser.error("Another corpus export is running")
    inventory = [p for p in args.input.resolve().iterdir() if p.suffix.lower() in (".sys", ".dll")]
    inventory.sort(key=lambda p: p.name.lower())
    files = inventory
    if args.only:
        files = [p for p in files if p.name in args.only]
        if set(args.only) != {p.name for p in files}:
            parser.error("Unknown --only filename")
    priority = {"mtkm64.sys": 0, "mtdispkm64.sys": 1, "mtvpukm64.sys": 2}
    files.sort(key=lambda p: (priority.get(p.name, 3), p.stat().st_size))
    exporter_hash = hashlib.sha256((ROOT / "scripts/ExportDriverCorpus.java").read_bytes()).hexdigest()
    manifest_path = corpus / "manifest.json"
    previous_manifest = json.loads(manifest_path.read_text()) if manifest_path.exists() else {}
    manifest = {"started": now(), "ghidra": str(args.ghidra.resolve()),
                "input": str(args.input.resolve()), "file_count": len(inventory), "files": {}}
    # A targeted export updates one member of the persistent corpus. Seed the
    # manifest from the previous batch and each file's durable metadata so a
    # --only run cannot make the other completed binaries disappear.
    previous_files = previous_manifest.get("files", {}) if previous_manifest.get("input") == manifest["input"] else {}
    for path in inventory:
        name = path.name
        digest = hashlib.file_digest(path.open("rb"), "sha256").hexdigest()
        saved = previous_files.get(name)
        metadata_path = corpus / name / "metadata.json"
        metadata = json.loads(metadata_path.read_text()) if metadata_path.exists() else None
        if not saved or saved.get("sha256") != digest:
            saved = metadata if metadata and metadata.get("sha256") == digest else None
        manifest["files"][name] = saved or {
            "sha256": digest, "source_size": path.stat().st_size,
            "status": "pending", "started": now()
        }
    def update(name, state):
        with LOCK:
            manifest["files"][name] = state
            save(manifest_path, manifest)
            print(now(), name, state["status"], flush=True)
    def process(path):
        name = path.name
        digest = hashlib.file_digest(path.open("rb"), "sha256").hexdigest()
        output = corpus / name
        output.mkdir(exist_ok=True)
        metadata = output / "metadata.json"
        state = {"sha256": digest, "source_size": path.stat().st_size,
                 "exporter_sha256": exporter_hash, "status": "preparing", "started": now()}
        reuse_analysis = False
        if metadata.exists():
            previous = json.loads(metadata.read_text())
            if previous.get("sha256") == digest and previous.get("status") == "completed":
                if previous.get("exporter_sha256") == exporter_hash:
                    update(name, previous)
                    return
                reuse_analysis = (ROOT / "ghidra-projects" / name / "driver.gpr").exists()
            if previous.get("sha256") != digest:
                raise RuntimeError(f"{name}: source hash changed; preserve old corpus before replacing it")
        save(metadata, state)
        update(name, state.copy())
        with (output / "static-tools.log").open("w") as log:
            for command, destination in [
                (["objdump", "-d", "-M", "intel", str(path)], "disassembly.txt"),
                (["objdump", "-x", str(path)], "pe-headers-imports-exports.txt"),
                (["strings", "-a", "-t", "x", str(path)], "strings-ascii.txt"),
                (["strings", "-a", "-e", "l", "-t", "x", str(path)], "strings-utf16.txt"),
            ]:
                with (output / destination).open("w") as stream:
                    subprocess.run(command, stdout=stream, stderr=log, check=True)
        projects = ROOT / "ghidra-projects" / name
        projects.mkdir(parents=True, exist_ok=True)
        load_args = ["-process", name, "-noanalysis"] if reuse_analysis else ["-import", str(path), "-overwrite"]
        command = [str(headless), str(projects), "driver", *load_args,
                   "-scriptPath", str(ROOT / "scripts"), "-postScript", "ExportDriverCorpus.java", str(output),
                   "-max-cpu", "2", "-log", str(output / "analysis.log"),
                   "-scriptlog", str(output / "export.log")]
        state.update(status="analyzing", command=command)
        save(metadata, state)
        update(name, state.copy())
        with (output / "headless.log").open("w") as log:
            result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT,
                                    stdin=subprocess.DEVNULL, cwd=ROOT)
        progress_path = output / "progress.json"
        progress = json.loads(progress_path.read_text()) if progress_path.exists() else {}
        state.update(returncode=result.returncode, finished=now(), export=progress)
        state["status"] = "completed" if result.returncode == 0 and progress.get("phase") == "completed" else "failed"
        save(metadata, state)
        update(name, state.copy())
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as pool:
        futures = {pool.submit(process, p): p for p in files}
        for future in concurrent.futures.as_completed(futures):
            try:
                future.result()
            except Exception as exc:
                update(futures[future].name, {"status": "failed", "exception": str(exc), "finished": now()})
    manifest["finished"] = now()
    save(manifest_path, manifest)
    return int(any(item["status"] != "completed" for item in manifest["files"].values()))


if __name__ == "__main__":
    raise SystemExit(main())
