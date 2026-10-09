# r436: RgnHeader fill-1 live -- firmware still 5s timeout, fill value not root cause

> Round r436 (2026-10-09). Highest-risk round. First live validation after r434
> (RgnHeader fill corrected to per-dword 0x00000001), executed after 6th cold reboot.

## Conclusion

RgnHeader BO created and bound normally (va=0x7c000000, per-dword fill 1),
TA Header +0x10 correctly points at RgnHeader, but firmware still produces no
completion event within 5s (-ETIMEDOUT). The r433/r434 fill correction
(0xFF -> 1) is NOT the timeout root cause; +0x28/+0x30 or other Header fields
remain missing.

## Live procedure

1. Cold reboot confirmed: 6th cold reboot, uptime 1min, neither mt_guest_probe
   nor mt_pvr_bridge loaded, tree clean (HEAD=dc77713, r434 code in tree).
2. Dual-gate test build: MT_TA_REAL_PACKET=1 + MT_TA_READBACK_DEBUG=1
   (static_asserts temporarily neutralized to ==1; userspace n_entries 1->0
   temporary; all reverted after); make kernel W=1 zero warnings.
3. Pre-live T1-T5: check-offline 554 Python + 1491 C all green (1 skipped);
   tests.misc.test_pre_live_safety 3/3 OK.
4. Trial rebuild (two steps):
   - First insmod (runtime_context=0): trial completed cleanly
     (restored=1 pinned=0) -> bridge 0x82:0x12 returned -ENODEV.
     Root cause: pvr_session_acquire() requires trial.pinned AND
     trial.connected; a clean trial does not satisfy it.
   - Unloaded bridge+probe (ref 0, safe_rmmod.sh passed normally), reloaded
     probe with runtime_context=1: firmware trial connect=0 disconnect=-61
     restored=0 pinned=1 result=0; sysfs connected=1 pinned=1, Guest/FW 2/2
     (matches r432 session state).
5. Bridge load: dual-gate mt_pvr_bridge.ko insmod, /dev/dri/renderD128 ready.
6. mt-ta-readback full chain:
   - connect (bvnc=0x23000406600017) -> render context handle=0x1000
     (0x82:0x12 succeeded this time)
   - 11 BOs + 12th target BO (va=0x7b000000) +
     13th RgnHeader BO (va=0x7c000000 bytes=4096, per-dword fill 1,
     dmesg confirms binding)  <- r434 fix
   - 0xFD submit (Header-only: buf+0x10=0x7c000000, n_entries=0)
     -> fence allocated -> dma_fence_wait_timeout(5s) returned 0 -> -ETIMEDOUT
     (userspace line 161 REQUIRE(0), errno=110).

## Key comparison

| round | TA Header +0x10      | RgnHeader fill | result |
|-------|----------------------|----------------|--------|
| r414  | 0 (all-zero)         | --             | OK 219us (no-work fast path) |
| r425  | 0x7b000000 (pixel BO)| --             | TIMEOUT |
| r432  | 0x7c000000 (RgnHeader)| 0xFF/dword    | TIMEOUT |
| r436  | 0x7c000000 (RgnHeader)| 0x01/dword    | TIMEOUT |

The r433/r434 fill correction is falsified as root cause by live evidence:
0xFF -> 1 did not change timeout behavior.

## Analysis

r430's three-hop chain (+0x10 = RgnHeader VA) routes correctly (BO bound,
submission path works, fence allocated); r434's fill now matches UMD behavior
(per-dword 1, MEASURED r433), but firmware still does not complete.
Remaining gaps (r433 order):

1. +0x28/+0x30 (UNKNOWN, zeroed) -- chain traced to local_5b0+0x68/+0x80,
   terminal value unknown, MLIST VA leading candidate (INFERRED);
2. RgnHeader per-dword semantics UNKNOWN (firmware-private);
3. or firmware needs MLIST or other structures (MLIST not in kick path,
   MEASURED r433).

RgnHeader INFERRED -> still not MEASURED: this round is falsifying evidence
(fill value not root cause), not a negation of RgnHeader semantics.

## Teardown

- Pending TA fence holds bridge ref=1 -> scripts/safe_rmmod.sh correctly
  refused (no -f, red line honored).
- Bridge still loaded (dual-gate test build), probe ref=1 (bridge dependency)
  untouched; dmesg zero WARN/BUG/Oops (boot-time CPU notices only).
- Awaiting user 7th cold reboot (same as r406/r418/r421/r425/r432).
- Source reverted (kernel/mt_ta_real.h + userspace/mt-ta-readback.c restored
  to committed state); default-gate rebuild W=1 zero warnings; tree clean.

## Gates

- make -C mt-vgpu-guest check-offline: 554 Python + 1491 C all green
- tests.misc.test_pre_live_safety: 3/3 OK
- make kernel W=1: zero warnings (dual-gate test build + reverted default build)

## Deliverables

- This report reports/r436-rgnheader-fill1-live-timeout.md
- Evidence build/traces/r436/dmesg-r436.txt (0600)
- reports/README.md main table +1 line
- MEMORY.md top-insert r436 (r434 archived per section 4)
- PROGRESS-SNAPSHOT.md section 12 append r436

## Honest boundaries

- RgnHeader semantics still INFERRED; falsifying evidence only.
- T2 pixel readback still open (firmware never completes, nothing to read).
- Zero production code behavior change; dual gates default off.
- Single-shot test; stability/repeatability untested.
- Next MUST be offline: +0x28/+0x30 semantics (MLIST VA candidate).
  No more live probing without offline basis
  (r380/r418/r421/r425/r432/r436 lesson).
- Zero rmmod -f, zero self-reboot; not chained to next round.
