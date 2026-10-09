#!/usr/bin/env python3
"""Guard the opt-in system-DMA source path in the retained TQX experiment."""
import unittest
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.helpers import get_repo_root

ROOT = get_repo_root()
SOURCE = ROOT / 'kernel/recovery/mt_live_tqx.c'


class LiveTqxDmaSource(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.text = SOURCE.read_text()

    def test_dma_path_is_explicitly_opt_in(self):
        self.assertIn('static bool dma_source;', self.text)
        self.assertIn('module_param(dma_source, bool, 0400)', self.text)
        self.assertIn('dma_source && i == 1', self.text)

    def test_maps_page_list_through_the_session_device(self):
        self.assertIn('dma_map_page(&device->dev', self.text)
        self.assertIn('dma_mapping_error(&device->dev, addr)', self.text)
        self.assertIn('dma_source_dma_addrs[i] = addr', self.text)
        self.assertIn('mt_system_page_address(&dma_source_address', self.text)
        self.assertIn('dma_source_memory.page_pa[i] = gpu_pa', self.text)
        self.assertIn('.page_pa = dma_source_memory.page_pa', self.text)
        self.assertIn('try_module_get(owner)', self.text)
        self.assertIn('dma_source_memory.cpu = vzalloc(bytes)', self.text)
        self.assertIn('IS_ALIGNED((unsigned long)dma_source_memory.cpu, PAGE_SIZE)',
                      self.text)

    def test_failed_preparation_unmaps_partial_dma(self):
        create = self.text.index('static int dma_source_bo_create')
        fail = self.text.index('fail:', create)
        ret = self.text.index('return ret;', fail)
        self.assertIn('while (dma_source_mapped_pages)', self.text[fail:ret])
        self.assertIn('dma_unmap_page(&device->dev', self.text[fail:ret])

    def test_readback_syncs_before_cpu_inspection(self):
        sync = self.text.index('dma_sync_single_for_cpu(&device->dev')
        source_read = self.text.index('read_bo(&ordinary[1]', sync)
        compare = self.text.index('source_difference(observed)', source_read)
        self.assertLess(sync, source_read)
        self.assertLess(source_read, compare)

    def test_release_unmaps_dma_iovas_not_gpu_pas(self):
        release = self.text[self.text.index('static void dma_source_release'):]
        release = release[:release.index('\n}\n')]
        self.assertIn('dma_unmap_page(&device->dev,', release)
        self.assertIn('dma_source_dma_addrs[--dma_source_mapped_pages]', release)
        self.assertNotIn('dma_source_memory.page_pa[--dma_source_mapped_pages]',
                         release)
        self.assertIn('kvfree(dma_source_dma_addrs)', release)
        self.assertIn('dma_source_dma_addrs = NULL', release)

    def test_readback_reports_destination_and_source_separately(self):
        self.assertIn('destination_verified', self.text)
        self.assertIn('source_verified', self.text)
        self.assertIn('destination_mismatch_offset', self.text)
        self.assertIn('source_mismatch_offset', self.text)
        self.assertIn('dma_cpu_first_word', self.text)


if __name__ == '__main__':
    unittest.main()
