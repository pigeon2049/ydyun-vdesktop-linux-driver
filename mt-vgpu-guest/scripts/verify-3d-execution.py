#!/usr/bin/env python3
"""
verify-3d-execution.py - Verify 3D minimal workload execution on DM2 (Universal Queue)
Inspects sysfs trial status, dmesg logs, and module parameters.
"""

import sys
import subprocess
from pathlib import Path

def run_cmd(cmd):
    try:
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=False)
        return p.returncode, p.stdout.strip(), p.stderr.strip()
    except Exception as e:
        return -1, "", str(e)

def main():
    print("=== MT VGPU DM2 3D Workload Execution Verification ===")
    
    # 1. Check mt_live_3d module
    rc, stdout, _ = run_cmd(["lsmod"])
    loaded = "mt_live_3d" in stdout
    print(f"[*] Module mt_live_3d loaded: {loaded}")
    
    res_path = Path("/sys/module/mt_live_3d/parameters/result")
    seq_path = Path("/sys/module/mt_live_3d/parameters/sequence")
    if res_path.exists() and seq_path.exists():
        res = res_path.read_text().strip()
        seq = seq_path.read_text().strip()
        print(f"[*] mt_live_3d parameter result={res}, sequence={seq}")
        if res == "0":
            print("    -> SUCCESS: Hardware executed DM2 workload and signaled completion.")
        else:
            print(f"    -> WARNING: Execution returned code {res}")
    
    # 2. Check trial file
    trial_path = Path("/sys/bus/pci/devices/0000:00:0e.0/mt_guest/trial")
    if trial_path.exists():
        content = trial_path.read_text()
        print("\n[*] Hardware Ring Status (/sys/.../mt_guest/trial):")
        for line in content.splitlines():
            if "dm=0" in line or "dm=2" in line or "guest=" in line or "result=" in line:
                print(f"    {line}")
    else:
        print("[!] Trial sysfs node not found.")

    # 3. Check kernel dmesg
    rc, dmesg_out, _ = run_cmd(["sudo", "dmesg"])
    relevant_lines = [l for l in dmesg_out.splitlines() if "mt_live_3d" in l]
    print("\n[*] Kernel Log Entries:")
    for l in relevant_lines[-10:]:
        print(f"    {l}")

    print("\n=== Verification Completed ===")
    return 0

if __name__ == "__main__":
    sys.exit(main())
