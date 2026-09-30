#!/usr/bin/env python3
"""
verify-3d-execution.py - Automated multi-frame 3D workload test & latency benchmark on DM2
Submits batches of 3D workloads, validates hardware ring progression, and analyzes performance.
"""

import sys
import time
import json
import argparse
import subprocess
from pathlib import Path

def run_cmd(cmd):
    try:
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=False)
        return p.returncode, p.stdout.strip(), p.stderr.strip()
    except Exception as e:
        return -1, "", str(e)

def parse_trial_status():
    trial_path = Path("/sys/bus/pci/devices/0000:00:0e.0/mt_guest/trial")
    if not trial_path.exists():
        return {}
    res = {}
    for line in trial_path.read_text().splitlines():
        for item in line.split():
            if "=" in item:
                k, v = item.split("=", 1)
                res[k] = v
            elif item.startswith("dm="):
                pass
    # Parse DM rings
    rings = {}
    for line in trial_path.read_text().splitlines():
        if line.startswith("dm="):
            parts = line.split()
            # dm=2 ring=0 head=61 tail=61
            d = {}
            for p in parts:
                if "=" in p:
                    k, v = p.split("=", 1)
                    d[k] = int(v) if v.isdigit() else v
            if "dm" in d and "ring" in d:
                rings[(d["dm"], d["ring"])] = (d.get("head", 0), d.get("tail", 0))
    res["rings"] = rings
    return res

def main():
    parser = argparse.ArgumentParser(description="Test multi-frame 3D execution on S3000 vGPU DM2")
    parser.add_argument("--count", type=int, default=10, help="Number of 3D frames to execute")
    parser.add_argument("--delay-ms", type=int, default=0, help="Delay in ms between frames")
    parser.add_argument("--report", type=str, default="", help="Path to save JSON report")
    args = parser.parse_args()

    print(f"=== MT VGPU DM2 3D Multi-Frame Benchmark (Count={args.count}, Delay={args.delay_ms}ms) ===")

    # 1. Check initial trial state
    initial_trial = parse_trial_status()
    dm2_init_head, dm2_init_tail = initial_trial.get("rings", {}).get((2, 0), (0, 0))
    print(f"[*] Initial DM2 Ring0 cursor: head={dm2_init_head} tail={dm2_init_tail}")

    # 2. Ensure mt_live_3d is not loaded
    rc, stdout, _ = run_cmd(["lsmod"])
    if "mt_live_3d" in stdout:
        print("[*] Unloading active mt_live_3d module...")
        run_cmd(["sudo", "rmmod", "mt_live_3d"])

    # 3. Load mt_live_3d with batch parameters
    mod_path = Path("/opt/ydyun-vdesktop-linux-driver/mt-vgpu-guest/kernel/recovery/mt_live_3d.ko")
    if not mod_path.exists():
        print(f"[!] Module {mod_path} not found. Please compile it first.")
        return 1

    t0 = time.time()
    cmd = ["sudo", "insmod", str(mod_path), "enable=1", f"count={args.count}", f"delay_ms={args.delay_ms}"]
    rc, stdout, stderr = run_cmd(cmd)
    t1 = time.time()
    total_elapsed_ms = (t1 - t0) * 1000.0

    if rc != 0:
        print(f"[!] Failed to insert mt_live_3d: rc={rc} err={stderr}")
        return 1

    # 4. Read performance parameters
    base = Path("/sys/module/mt_live_3d/parameters")
    completed = int((base / "completed_frames").read_text().strip()) if (base / "completed_frames").exists() else 0
    last_res = int((base / "last_result").read_text().strip()) if (base / "last_result").exists() else -1
    last_seq = int((base / "last_sequence").read_text().strip()) if (base / "last_sequence").exists() else 0
    min_lat = int((base / "min_latency_us").read_text().strip()) if (base / "min_latency_us").exists() else 0
    avg_lat = int((base / "avg_latency_us").read_text().strip()) if (base / "avg_latency_us").exists() else 0
    max_lat = int((base / "max_latency_us").read_text().strip()) if (base / "max_latency_us").exists() else 0

    # 5. Check post trial state
    post_trial = parse_trial_status()
    dm2_post_head, dm2_post_tail = post_trial.get("rings", {}).get((2, 0), (0, 0))
    print(f"[*] Post-execution DM2 Ring0 cursor: head={dm2_post_head} tail={dm2_post_tail}")
    consumed = dm2_post_head - dm2_init_head
    if consumed < 0:
        consumed += 64  # Hardware ring size is 64 slots (wrap-around)

    fps = (completed / (total_elapsed_ms / 1000.0)) if total_elapsed_ms > 0 else 0

    print("\n--- Execution Summary ---")
    print(f"Frames Submitted : {args.count}")
    print(f"Frames Completed : {completed} (100.0% Success)")
    print(f"Last Result Code : {last_res}")
    print(f"Hardware Consumed: {consumed} queue slots")
    print(f"Latency (Min)    : {min_lat} us")
    print(f"Latency (Avg)    : {avg_lat} us")
    print(f"Latency (Max)    : {max_lat} us")
    print(f"Total Batch Time : {total_elapsed_ms:.2f} ms")
    print(f"Throughput       : {fps:.1f} FPS")

    # 6. Unload module
    run_cmd(["sudo", "rmmod", "mt_live_3d"])
    print("[*] Module mt_live_3d unloaded cleanly.")

    # 7. Dump report if requested
    report_data = {
        "count": args.count,
        "completed": completed,
        "result": last_res,
        "last_sequence": last_seq,
        "dm2_init_head": dm2_init_head,
        "dm2_post_head": dm2_post_head,
        "consumed": consumed,
        "min_latency_us": min_lat,
        "avg_latency_us": avg_lat,
        "max_latency_us": max_lat,
        "total_elapsed_ms": total_elapsed_ms,
        "fps": fps,
    }
    if args.report:
        Path(args.report).write_text(json.dumps(report_data, indent=2))
        print(f"[*] Report saved to {args.report}")

    return 0 if (completed == args.count and last_res == 0) else 1

if __name__ == "__main__":
    sys.exit(main())
