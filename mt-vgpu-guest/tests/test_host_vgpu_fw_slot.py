import importlib.util
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "host_vgpu_fw_slot", ROOT / "scripts/verify-host-vgpu-fw-slot.py")
host_vgpu_fw_slot = importlib.util.module_from_spec(spec)
spec.loader.exec_module(host_vgpu_fw_slot)
fw_heap_patch_spec = importlib.util.spec_from_file_location(
    "patch_guest_fw_heap_base", ROOT / "scripts/patch-guest-fw-heap-base.py")
patch_guest_fw_heap_base = importlib.util.module_from_spec(fw_heap_patch_spec)
fw_heap_patch_spec.loader.exec_module(patch_guest_fw_heap_base)


class HostVgpuFirmwareSlotTests(unittest.TestCase):
    def setUp(self):
        self.asm = (ROOT / "reports/host-2.3-core.asm").read_text()

    def test_host_sets_eight_mib_record_and_maps_bar2_slot(self):
        result = host_vgpu_fw_slot.verify(self.asm)
        self.assertEqual(result["private_record_table_allocation_size"], "0xf0")
        self.assertEqual(result["private_record_offset"].split()[0], "0x30")
        self.assertEqual(result["private_record_size"], "0x800000")
        self.assertTrue(result["private_record_marked_valid"])
        self.assertEqual(result["host_ring_init_maps_each_slot_size"], "0x800000")
        self.assertEqual(result["host_ring_init_uses_pci_bar"], 2)
        self.assertTrue(result["host_translation_walks_five_records"])
        self.assertEqual(result["linux_host_vgpu_info_page_version"], 1)
        self.assertEqual(result["linux_host_vgpu_info_page_magic"], "0xaa557491")
        self.assertEqual(result["linux_host_fw_heap_base_offset"], "0x848")
        self.assertEqual(result["linux_host_fw_heap_size_offset"], "0x850")
        self.assertEqual(result["guest_info_page_fw_heap_base_offset"], "0x848")
        self.assertIn("vgpu_calculate_vpu_mem_size", result["linux_host_vvpu_segment_size_source"])
        self.assertIn("vgpu_calculate_vpu_mem_size", result["linux_host_vm_bar2_actual_size_source"])
        self.assertTrue(result["linux_host_vvpu_segment_size_is_size_field"])
        self.assertTrue(result["guest_vpu_flag_uses_vvpu_size_as_fw_heap_card_base"])
        self.assertEqual(result["candidate_guest_fw_heap_base_offset"], "0x848")
        self.assertEqual(result["candidate_guest_fw_heap_base_patch_site"],
                         "mtgpu_platform_data_vz_init+0x8d, after VPU range calculations")
        self.assertTrue(result["guest_vgpu_info_pointer_loaded_before_fw_heap_card_base_copy"])
        self.assertTrue(result["guest_vvpu_size_remains_used_for_vpu_range_math"])
        self.assertEqual(result["linux_host_vvpu_segment_count_offset"], "0x440")
        self.assertEqual(result["guest_info_page_fw_heap_size_offset"], "0x850")
        self.assertEqual(result["guest_vgpu_vvpu_segment_size_offset"].split()[0], "0x438")
        self.assertTrue(result["guest_fw_heap_size_cancels_from_fw_heap_card_base_expression"])
        self.assertTrue(result["guest_vz_fw_heap_card_base_uses_conditional_segment_value"])
        self.assertTrue(result["guest_fixup_reads_fw_heap_size_before_writing_vz_fw_heap_card_base"])
        self.assertIn("fw_heap_card_base", result["platform_data_fw_heap_card_base_offset"])
        self.assertTrue(result["guest_vz_fw_heap_card_base_feeds_phys_heap_config_scardbase"])
        self.assertEqual(result["pvr_phys_heap_config_scard_base_offset"], "0x20")
        self.assertIn("FW_MAIN", result["pvr_phys_heap_config_usage"])
        self.assertEqual(result["pvr_fw_main_phys_heap_size"], "0x800000")
        self.assertEqual(result["pvr_fw_config_usage"],
                         "FW_CONFIG (PHYS_HEAP_USAGE_FW_CONFIG=0x100)")
        self.assertTrue(result["pvr_fw_config_declared_as_subheap_of_fw_main"])
        self.assertTrue(result["pvr_fw_config_routes_to_config_child_heap"])
        self.assertFalse(result["linux_fw_premap_to_host_bar2_translation_proven"])

    def test_rejects_non_v1_linux_host_info_page(self):
        body = host_vgpu_fw_slot.function_body(self.asm, "vgpu_access_pci_bar1_region")
        changed_body = re.sub(
            r"(c7 40 04 01 00 00 00\s+mov\s+DWORD PTR \[rax\+0x4\],)0x1",
            r"\g<1>0x2", body, count=1)
        self.assertNotEqual(body, changed_body)
        changed = self.asm.replace(body, changed_body, 1)
        with self.assertRaisesRegex(ValueError, "Linux Host version-1 vGPU info-page"):
            host_vgpu_fw_slot.verify(changed)

    def test_platform_field_offset_is_derived_from_header(self):
        header = host_vgpu_fw_slot.PLATFORM_HEADER.read_text()
        self.assertEqual(host_vgpu_fw_slot.platform_data_fw_heap_card_base_offset(header), 0x50)

    def test_fw_config_is_declared_as_a_subheap_of_fw_main(self):
        usage_header = host_vgpu_fw_slot.PHYS_HEAP_HEADER.read_text()
        id_header = host_vgpu_fw_slot.PHYS_HEAP_ID_HEADER.read_text()
        fw_utils_header = host_vgpu_fw_slot.FW_UTILS_HEADER.read_text()
        result = host_vgpu_fw_slot.phys_heap_fw_config_relationship(
            usage_header, id_header, fw_utils_header)
        self.assertEqual(result, {
            "heap_index": 8,
            "usage_value": 0x100,
            "routes_to_config_heap": True,
        })
        changed = id_header.replace("subheap of FW_MAIN", "independent heap", 1)
        with self.assertRaisesRegex(ValueError, "subheap of FW_MAIN"):
            host_vgpu_fw_slot.phys_heap_fw_config_relationship(
                usage_header, changed, fw_utils_header)
        changed_utils = fw_utils_header.replace(
            "psDevInfo->psFirmwareConfigHeap", "psDevInfo->psFirmwareMainHeap", 1)
        with self.assertRaisesRegex(ValueError, "psFirmwareConfigHeap"):
            host_vgpu_fw_slot.phys_heap_fw_config_relationship(
                usage_header, id_header, changed_utils)

    def test_rejects_missing_per_vgpu_slot_store(self):
        changed = re.sub(r"mov\s+QWORD PTR \[rsi\+0x30\],rdx",
                         "mov QWORD PTR [rsi+0x38],rdx", self.asm, count=1)
        with self.assertRaisesRegex(ValueError, "per-vGPU firmware record"):
            host_vgpu_fw_slot.verify(changed)

    def test_rejects_non_eight_mib_bar2_mapping(self):
        changed = re.sub(r"mov\s+esi,0x800000", "mov esi,0x400000",
                         self.asm, count=1)
        with self.assertRaisesRegex(ValueError, "BAR2 firmware-slot initialization"):
            host_vgpu_fw_slot.verify(changed)

    def test_fw_heap_base_patch_changes_only_the_late_guest_publication_load(self):
        original = b"prefix" + patch_guest_fw_heap_base.OLD + b"suffix"
        patched = patch_guest_fw_heap_base.patch_instruction(original, len(b"prefix"))
        self.assertEqual(
            patched,
            b"prefix" + patch_guest_fw_heap_base.NEW + b"suffix",
        )
        self.assertEqual(len(patch_guest_fw_heap_base.NEW), 8)
        self.assertEqual(patch_guest_fw_heap_base.FUNCTION, "mtgpu_platform_data_vz_init")
        self.assertEqual(patch_guest_fw_heap_base.FUNCTION_OFFSET, 0x8D)
        self.assertEqual(patch_guest_fw_heap_base.GUEST_MODE_BRANCH_OFFSET, 0x63)
        self.assertEqual(patch_guest_fw_heap_base.INFO_PAGE_POINTER_LOAD_OFFSET, 0x81)
        self.assertEqual(patch_guest_fw_heap_base.INFO_PAGE_POINTER_STORE_OFFSET, 0x89)
        with self.assertRaisesRegex(SystemExit, "already patched"):
            patch_guest_fw_heap_base.patch_instruction(patched, len(b"prefix"))

    def test_fw_heap_base_patch_requires_guest_branch_and_info_page_pointer(self):
        image = bytearray(0x100)
        image[patch_guest_fw_heap_base.GUEST_MODE_BRANCH_OFFSET:
              patch_guest_fw_heap_base.GUEST_MODE_BRANCH_OFFSET +
              len(patch_guest_fw_heap_base.GUEST_MODE_BRANCH)] = \
            patch_guest_fw_heap_base.GUEST_MODE_BRANCH
        image[patch_guest_fw_heap_base.INFO_PAGE_POINTER_LOAD_OFFSET:
              patch_guest_fw_heap_base.INFO_PAGE_POINTER_LOAD_OFFSET +
              len(patch_guest_fw_heap_base.INFO_PAGE_POINTER_LOAD)] = \
            patch_guest_fw_heap_base.INFO_PAGE_POINTER_LOAD
        image[patch_guest_fw_heap_base.INFO_PAGE_POINTER_STORE_OFFSET:
              patch_guest_fw_heap_base.INFO_PAGE_POINTER_STORE_OFFSET +
              len(patch_guest_fw_heap_base.INFO_PAGE_POINTER_STORE)] = \
            patch_guest_fw_heap_base.INFO_PAGE_POINTER_STORE
        patch_guest_fw_heap_base.validate_guest_publication_context(bytes(image), 0)

        image[patch_guest_fw_heap_base.INFO_PAGE_POINTER_LOAD_OFFSET] ^= 1
        with self.assertRaisesRegex(SystemExit, "information-page pointer load changed"):
            patch_guest_fw_heap_base.validate_guest_publication_context(bytes(image), 0)

    def test_fw_heap_base_patch_refuses_changed_guest_mode_branch(self):
        image = bytearray(0x100)
        image[patch_guest_fw_heap_base.GUEST_MODE_BRANCH_OFFSET] = 0x75
        with self.assertRaisesRegex(SystemExit, "Guest-mode branch changed"):
            patch_guest_fw_heap_base.validate_guest_publication_context(bytes(image), 0)

    def test_fw_heap_base_patch_refuses_unknown_instruction(self):
        with self.assertRaisesRegex(SystemExit, "unexpected instruction bytes"):
            patch_guest_fw_heap_base.patch_instruction(b"not the expected load", 0)

    def test_fw_heap_base_patch_preserves_the_vvpu_size_load(self):
        size_load = patch_guest_fw_heap_base.SIZE_LOAD
        publication_load = patch_guest_fw_heap_base.OLD
        original = b"head" + size_load + b"middle" + publication_load + b"tail"
        target = len(b"head") + len(size_load) + len(b"middle")
        patched = patch_guest_fw_heap_base.patch_instruction(original, target)
        self.assertEqual(patched[:len(b"head") + len(size_load)],
                         original[:len(b"head") + len(size_load)])
        self.assertIn(size_load, patched)
        self.assertIn(patch_guest_fw_heap_base.NEW, patched)


if __name__ == "__main__":
    unittest.main()
