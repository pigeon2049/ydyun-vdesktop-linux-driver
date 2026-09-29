#!/usr/bin/env python3
"""Reconcile the Host MPC/OSID firmware-heap formula with saved Guest records.

This tool only reads files under reports/. It never accesses PCI or hardware.
Without --mpc-card-base, that base is inferred from the reported firmware
segment and must not be mistaken for an independently measured Host value.
"""
import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEAP_SIZE = 8 * 1024 * 1024
MPC_OVERHEAD = 10 * 1024 * 1024
FW_PREMAP_DEVICE_BASE = 0xE1C0000000


def reconcile(device_info, upload, *, osid_start, osid_count, mpc_id,
              mpc_card_base=None, guest_osid=None):
    if device_info.get("version") != 2:
        raise ValueError("expected the captured Windows v2 device-info layout")
    osid = device_info.get("osid") if guest_osid is None else guest_osid
    if not isinstance(osid, int) or not (osid_start <= osid < osid_start + osid_count):
        raise ValueError("Guest OSID is outside the supplied Host OSID range")
    if osid_start < 1 or osid_count < 1 or mpc_id < 0:
        raise ValueError("invalid OSID/MPC topology")

    fw_segments = [segment for segment in device_info.get("segments", [])
                   if int(segment["flags"], 0) & 0x4]
    if len(fw_segments) != 1:
        raise ValueError("expected exactly one firmware segment (flag 0x4)")
    segment = fw_segments[0]
    segment_offset = int(segment["bar2_offset"], 0)
    segment_gpu_address = int(segment["address"], 0)
    segment_size = int(segment["size_bytes"])
    captured_osid = device_info.get("osid")
    if not isinstance(captured_osid, int) or not (
            osid_start <= captured_osid < osid_start + osid_count):
        raise ValueError("captured Windows reference OSID is outside the supplied Host OSID range")

    stride = osid_count * HEAP_SIZE + MPC_OVERHEAD
    osid_slot = (osid + 1 - osid_start) * HEAP_SIZE
    captured_osid_slot = (captured_osid + 1 - osid_start) * HEAP_SIZE
    if mpc_card_base is None:
        # Treat the first heap of this Guest's firmware segment as the OSID slot.
        # This is a cross-check against the recorded upload, not a direct Host read.
        mpc_card_base = segment_offset - mpc_id * stride - osid_slot
        base_source = "inferred from firmware segment offset; not independently measured"
    else:
        base_source = "provided by caller"

    slot = mpc_card_base + mpc_id * stride + osid_slot
    # RGXRegisterDevice constructs the Linux FW_PREMAPn blueprints from this
    # device-address base, stepping by RGX_FW_HEAP_SHIFT (23). This is a
    # different address domain from the Host BAR2 aperture offset above.
    premap_device_address = FW_PREMAP_DEVICE_BASE + osid * HEAP_SIZE
    # Compare only the OSID-relative component. The Host BAR2 slot and PVR
    # premap address have different, not-yet-reconciled address-space bases.
    relative_osid_delta = (osid - osid_start) * HEAP_SIZE
    host_slot_relative_delta = slot - (mpc_card_base + mpc_id * stride + HEAP_SIZE)
    premap_relative_delta = premap_device_address - (
        FW_PREMAP_DEVICE_BASE + osid_start * HEAP_SIZE)
    # The Linux Host publishes a GPU/card physical address in fw_heap_base.
    # Infer the card base from the captured Windows OSID segment, then apply
    # the Host's static MPC/OSID formula for the selected Guest OSID.
    inferred_gpu_card_base = (
        segment_gpu_address - mpc_id * stride - captured_osid_slot
    )
    fw_heap_gpu_address_candidate = (
        inferred_gpu_card_base + mpc_id * stride + osid_slot
    )
    fw_heap_gpu_delta_from_reference = fw_heap_gpu_address_candidate - segment_gpu_address
    host_slot_delta_from_reference = (osid - captured_osid) * HEAP_SIZE
    if slot < 0 or slot + HEAP_SIZE > segment_offset + segment_size:
        raise ValueError("computed 8 MiB OSID slot falls outside the firmware segment")
    if slot < segment_offset:
        raise ValueError("computed OSID slot begins before the firmware segment")

    upload_matches = None
    if upload is not None:
        upload_offset = int(upload["firmware_bar_offset"], 0)
        upload_gpu_address = int(upload["firmware_gpu_pa"], 0)
        upload_size = int(upload["firmware_bytes"])
        if upload_offset != slot:
            raise ValueError("saved firmware upload offset differs from computed OSID slot")
        if upload_gpu_address != segment_gpu_address:
            raise ValueError("saved upload GPU address differs from firmware segment base")
        if upload_size != HEAP_SIZE:
            raise ValueError("saved firmware allocation is not exactly 8 MiB")
        upload_matches = True

    return {
        "guest_osid": osid,
        "device_info_osid": device_info.get("osid"),
        "guest_osid_source": "device info" if guest_osid is None else "caller override",
        "captured_windows_reference_osid": captured_osid,
        "osid_start_assumption": osid_start,
        "osid_count_assumption_including_host": osid_count,
        "mpc_id_assumption": mpc_id,
        "mpc_stride_bytes": stride,
        "osid_heap_slot_bytes": osid_slot,
        "mpc_card_base_bytes": hex(mpc_card_base),
        "mpc_card_base_source": base_source,
        "computed_heap_bar2_offset": hex(slot),
        "linux_fw_premap_device_address": hex(premap_device_address),
        "linux_fw_premap_address_provenance": "core HeapCfgBlueprintInit base plus OSID * 8 MiB; static, not a runtime BAR2 translation",
        "osid_relative_delta_from_first_guest_bytes": hex(relative_osid_delta),
        "host_bar2_slot_relative_osid_delta_bytes": hex(host_slot_relative_delta),
        "linux_fw_premap_relative_osid_delta_bytes": hex(premap_relative_delta),
        "host_and_premap_relative_osid_delta_match": (
            host_slot_relative_delta == premap_relative_delta == relative_osid_delta
        ),
        "premap_to_bar2_translation_proven": False,
        "firmware_segment_bar2_offset": hex(segment_offset),
        "firmware_segment_gpu_address": hex(segment_gpu_address),
        "firmware_segment_size_bytes": segment_size,
        "inferred_gpu_mem_card_base": hex(inferred_gpu_card_base),
        "gpu_mem_card_base_source": "inferred from captured Windows OSID GPU address and the Linux Host MPC/OSID formula; not independently measured",
        "linux_guest_fw_main_scardbase_candidate": hex(fw_heap_gpu_address_candidate),
        "fw_main_scardbase_candidate_source": "inferred GPU card base plus Linux Host MPC/OSID slot formula; not read from the Linux OSID 7 info page",
        "fw_main_scardbase_delta_from_windows_reference_bytes": hex(fw_heap_gpu_delta_from_reference),
        "fw_main_scardbase_delta_matches_host_bar2_slot_delta": (
            fw_heap_gpu_delta_from_reference == host_slot_delta_from_reference
        ),
        "fw_main_scardbase_matches_captured_windows_segment": (
            osid == captured_osid and fw_heap_gpu_address_candidate == segment_gpu_address
        ),
        "saved_upload_matches_slot": upload_matches,
        "hardware_accessed": False,
        "pvz_provider_proven": False,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device-info", type=Path,
                        default=ROOT / "reports/device-info.json")
    parser.add_argument("--upload", type=Path,
                        default=ROOT / "reports/firmware-upload-validation.json")
    parser.add_argument("--osid-start", type=int, default=4,
                        help="Host topology assumption (default: QY1 SR-IOV example)")
    parser.add_argument("--osid-count", type=int, default=5,
                        help="Host topology assumption, including OSID 0")
    parser.add_argument("--mpc-id", type=int, default=0,
                        help="MPC topology assumption")
    parser.add_argument("--guest-osid", type=int,
                        help="override the captured OSID to calculate another guest slot")
    parser.add_argument("--no-upload-check", action="store_true",
                        help="do not compare an upload record belonging to another OSID")
    parser.add_argument("--mpc-card-base", type=lambda s: int(s, 0),
                        help="Host BAR2 card base; omit to infer it from the captured segment")
    args = parser.parse_args()
    try:
        device_info = json.loads(args.device_info.read_text())
        if args.guest_osid is not None and args.guest_osid != device_info.get("osid") \
                and not args.no_upload_check:
            raise ValueError("an OSID override needs --no-upload-check; the saved upload is for the captured OSID")
        if args.guest_osid is not None and args.guest_osid != device_info.get("osid") \
                and args.mpc_card_base is None:
            raise ValueError("an OSID override needs --mpc-card-base; the captured segment belongs to another OSID")
        upload = None if args.no_upload_check else json.loads(args.upload.read_text())
        result = reconcile(device_info, upload,
                           osid_start=args.osid_start,
                           osid_count=args.osid_count,
                           mpc_id=args.mpc_id,
                           mpc_card_base=args.mpc_card_base,
                           guest_osid=args.guest_osid)
    except (OSError, KeyError, TypeError, ValueError, json.JSONDecodeError) as exc:
        parser.error(str(exc))
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
