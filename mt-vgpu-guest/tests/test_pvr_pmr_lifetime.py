#!/usr/bin/env python3
"""Gate the PMR lifetime rules in the Stage-B bridge module.

The use-after-free this guards against was invisible to every existing test:
the bug needs an mmap racing a bridge command on the same fd, which no
offline test was driving. The rules are checked against the source text
instead, so the shape of the fix cannot silently regress.

Rules enforced:

  1. A PMR must be reference counted, and pvr_pmr_new() must start it at 1 --
     the reference the file->pmrs list itself owns.
  2. Any function that uses a PMR pointer outside file->lock must take a
     reference under the lock and release it on every exit path.
  3. pvr_mmap() is the only such function today, and it must have exactly one
     lock and one unlock (the deadlock lesson: pvr_bridge_dispatch() already
     holds file->lock, so taking it again self-deadlocks).
  4. pvr_pmr_put() must unlink and delegate to the unref helper; it must not
     call vfree()/kfree() itself.
"""
import re
import unittest
from pathlib import Path

SOURCE = Path(__file__).resolve().parents[1] / 'kernel/recovery/mt_pvr_bridge.c'


def strip_comments(text):
    """Remove C comments.

    Necessary, not cosmetic: the function bodies below are heavily commented,
    and prose like "an earlier version did" or "return -EINVAL" inside a
    comment would otherwise be counted as code. That mistake produced two
    false failures here on the first run.
    """
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


def function_body(name, text):
    """Return the source of the named function, brace-balanced."""
    start = re.search(r'^(?:static\s+)?\w[\w\s\*]*\b' + re.escape(name) +
                      r'\s*\([^;]*?\)\s*\{', text, re.M)
    if not start:
        raise AssertionError(f'{name} not found in {SOURCE}')
    depth = 0
    i = text.index('{', start.start())
    for j in range(i, len(text)):
        if text[j] == '{':
            depth += 1
        elif text[j] == '}':
            depth -= 1
            if depth == 0:
                return text[i:j + 1]
    raise AssertionError(f'{name} is not brace-balanced in {SOURCE}')


class PmrLifetime(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.raw = SOURCE.read_text()
        # Code only. Checking the commented text produced two false failures,
        # and a gate that cries wolf gets ignored.
        cls.text = strip_comments(cls.raw)
        cls.pmr_struct = re.search(r'struct mt_pvr_pmr\s*\{(.*?)\n\};',
                                   cls.text, re.S).group(1)
        cls.mmap = function_body('pvr_mmap', cls.text)
        cls.pmr_put = function_body('pvr_pmr_put', cls.text)
        cls.pmr_new = function_body('pvr_pmr_new', cls.text)
        cls.unref = function_body('pvr_pmr_unref', cls.text)

    def test_pmr_is_reference_counted(self):
        self.assertIn('refcount', self.pmr_struct,
                      'mt_pvr_pmr has no reference count, so a PMR cannot '
                      'outlive the bridge command that freed it')

    def test_new_pmr_starts_at_one(self):
        # The list owns one reference. Anything else either leaks every PMR or
        # frees one the list still points at.
        self.assertRegex(self.pmr_new, r'refcount\s*=\s*1\b')

    def test_mmap_takes_a_reference_under_the_lock(self):
        # Increment must appear between the lock and the unlock, otherwise the
        # PMR can still be freed in the gap.
        lock = self.mmap.index('mutex_lock')
        inc = self.mmap.index('refcount++')
        unlock = self.mmap.index('mutex_unlock')
        self.assertLess(lock, inc, 'mmap must lock before bumping refcount')
        self.assertLess(inc, unlock,
                        'refcount must be taken while still under the lock, '
                        'otherwise pvr_pmr_put() can free the PMR first')

    def test_mmap_releases_the_reference_on_every_exit(self):
        # Every early return taken *after a reference was actually acquired*
        # would leak it. The `return -ENOENT` on the lookup-failure path is
        # fine: no reference exists there, pmr is NULL.
        tail = self.mmap[self.mmap.index('mutex_unlock'):]
        # The lookup-failure return is legitimate only because it is guarded by
        # `if (!pmr)`: no reference was taken on that path. Every other return
        # after the unlock must be the single shared exit.
        null_guard = re.search(r'if\s*\(\s*!pmr\s*\)\s*\{(.*?)\}', tail, re.S)
        self.assertIsNotNone(
            null_guard, 'the `if (!pmr)` lookup-failure guard is gone')
        self.assertIn('return -ENOENT', null_guard.group(1),
                      'the -ENOENT return must stay inside the !pmr guard, '
                      'where no reference has been taken')
        outside = tail.replace(null_guard.group(0), '')
        self.assertEqual(
            len(re.findall(r'\breturn\b[^;]*;', outside)), 1,
            'mmap returns more than once outside the !pmr guard; every early '
            'return leaks the reference')
        self.assertIn('out:', self.mmap,
                      'mmap has no shared exit to release the reference from')
        self.assertIn('pvr_pmr_unref(pmr)', self.mmap,
                      'mmap never gives the reference back')
        # The reference must be given up *after* the mapping is built.
        self.assertLess(self.mmap.index('pvr_pmr_unref(pmr)'),
                        self.mmap.rindex('return ret'),
                        'mmap releases the PMR before finishing the mapping')

    def test_mmap_does_not_relock(self):
        # pvr_bridge_dispatch() holds file->lock across the whole call and it
        # is a plain mutex, so holding it twice here self-deadlocks and wedges
        # the caller in uninterruptible sleep. This cost a reboot to undo once.
        # Sequential take/release pairs are fine (entry lookup, then the exit
        # unref that can return an arena segment); nesting is what kills.
        locks = [m.start() for m in re.finditer(r'mutex_lock', self.mmap)]
        unlocks = [m.start() for m in re.finditer(r'mutex_unlock', self.mmap)]
        self.assertEqual(len(locks), len(unlocks),
                         'mmap takes and releases file->lock unevenly')
        for lock, unlock in zip(locks, unlocks):
            self.assertLess(lock, unlock, 'unlock before lock in mmap')
        for first, second in zip(locks, locks[1:]):
            between = self.mmap[first:second]
            self.assertIn('mutex_unlock', between,
                          'mmap holds file->lock across a second lock')

    def test_pmr_put_unlinks_then_delegates(self):
        # It must not free the PMR itself: the mmap reference may still be
        # outstanding, and freeing here is exactly the use-after-free.
        self.assertIn('list_del', self.pmr_put,
                      'pvr_pmr_put must unlink the PMR from file->pmrs')
        self.assertIn('pvr_pmr_unref', self.pmr_put,
                      'pvr_pmr_put must delegate the release to pvr_pmr_unref')
        for direct in ('vfree', 'kfree'):
            self.assertNotIn(
                direct, self.pmr_put,
                f'pvr_pmr_put calls {direct}() directly, so an outstanding '
                'mmap reference would be freed out from under its user')

    def test_unref_frees_only_at_zero(self):
        self.assertRegex(self.unref, r'--\s*pmr->refcount')
        self.assertRegex(self.unref, r'if\s*\(\s*--\s*pmr->refcount\s*\)')
        self.assertIn('WARN_ON_ONCE', self.unref,
                      'dropping a reference that was never taken should warn')
        for direct in ('vfree', 'kfree'):
            self.assertIn(direct, self.unref,
                          f'pvr_pmr_unref must own the {direct}()')

    def test_every_free_outside_the_error_paths_goes_through_unref(self):
        # A direct free is only legitimate in pvr_pmr_new()'s own failure
        # paths, before the PMR is published. Anywhere else it can free a PMR
        # that still has an outstanding mmap reference -- the use-after-free.
        # The only two functions allowed to free a PMR directly: the unref
        # helper that owns the release, and pvr_pmr_new's own failure paths
        # (which run before the PMR is ever published).
        allowed_bodies = [self.unref, self.pmr_new]
        allowed = []
        for body in allowed_bodies:
            start = self.text.index(body)
            allowed.append((start, start + len(body)))

        stray = []
        # Only the PMR itself counts: kfree(pmr->dma_addrs) frees the DMA
        # address array owned solely by the PMR (allocated in register,
        # freed in release), which is safe and unrelated to the object
        # lifetime. The lookahead rejects pmr->field and pmr->field[i].
        for m in re.finditer(r'\b(vfree|kfree)\s*\(\s*pmr(?![\w>-])',
                             self.text):
            if not any(lo <= m.start() <= hi for lo, hi in allowed):
                stray.append(self.text[:m.start()].count('\n') + 1)
        self.assertEqual(
            stray, [],
            'kfree(pmr)/vfree(pmr) outside pvr_pmr_unref() and '
            'pvr_pmr_new() at lines %s; every other release must go through '
            'pvr_pmr_unref() or it can free a PMR that still has an '
            'outstanding mmap reference' % stray)

    def test_release_hwperf_is_the_only_put_caller(self):
        # 0x86:0x5 is the command that hands a PMR back; it is what makes the
        # race reachable, so keep it named and routed.
        self.assertIn('pvr_pmr_put(file', self.text,
                      'nothing releases PMRs any more; 0x86:0x5 must call '
                      'pvr_pmr_put()')
        self.assertRegex(self.text,
                         r'case\s+0x5:.*\n.*pvr_cmd_hwperf_release',
                         '0x86:0x5 is no longer routed to pvr_cmd_hwperf_release')


class PmrImportRouting(unittest.TestCase):
    """0x6:0x3 and 0x6:0x6 have different OUT sizes and handlers."""

    @classmethod
    def setUpClass(cls):
        cls.text = strip_comments(SOURCE.read_text())
        cls.make_import = function_body('pvr_cmd_pmr_make_import', cls.text)
        cls.probe = strip_comments(
            (SOURCE.parents[2] / 'probe' / 'pvr_node_probe.c').read_text())

    def test_make_import_has_its_own_handler(self):
        self.assertRegex(
            self.text,
            r'case\s+0x3:.*\n.*pvr_cmd_pmr_make_import',
            '0x6:0x3 is not routed to its own handler')
        self.assertRegex(
            self.text,
            r'case\s+0x4:.*\n.*pvr_cmd_pmr_unmake_import',
            '0x6:0x4 is not routed to its own handler')
        self.assertRegex(
            self.text,
            r'case\s+0x6:.*\n.*pvr_cmd_pmr_import',
            '0x6:0x6 is not routed to its own handler')
        self.assertIn('mt_pvr_make_import_in', self.make_import)
        self.assertIn('mt_pvr_make_import_out', self.make_import)
        self.assertIn('pvr_pmr_find(file', self.make_import)
        self.assertIn('out.ext_mem = pmr->handle', self.make_import)

    def test_probe_covers_make_import(self):
        self.assertRegex(
            self.probe,
            r'bridge\(fd,\s*0x6,\s*0x3,\s*&make_in',
            'the node probe does not exercise 0x6:0x3')
        self.assertIn('make_out.ext_mem != sync_out.sync_pmr', self.probe,
                      'the node probe does not check the exported handle')
        self.assertRegex(
            self.probe,
            r'bridge\(fd,\s*0x6,\s*0x4,\s*&unmake_in',
            'the node probe does not exercise 0x6:0x4')


class ZsBufferRouting(unittest.TestCase):
    """0x82:0x2/0x82:0x3 mint and retire a dedicated object kind."""

    @classmethod
    def setUpClass(cls):
        cls.text = strip_comments(SOURCE.read_text())
        cls.probe = strip_comments(
            (SOURCE.parents[2] / 'probe' / 'pvr_node_probe.c').read_text())

    def test_zs_handlers_use_their_own_kind(self):
        # Sharing a kind with render/compute/kicksync contexts would let a
        # destroy for one retire an object of another.
        for func in ('pvr_cmd_zs_create', 'pvr_cmd_zs_destroy'):
            body = function_body(func, self.text)
            self.assertIn('MT_PVR_KIND_ZSBUFFER', body,
                          f'{func} does not use the ZSBUFFER kind')
        self.assertRegex(
            self.text,
            r'case\s+0x2:.*\n.*pvr_cmd_zs_create',
            '0x82:0x2 is not routed to its own handler')
        self.assertRegex(
            self.text,
            r'case\s+0x3:.*\n.*pvr_cmd_zs_destroy',
            '0x82:0x3 is not routed to its own handler')

    def test_probe_round_trips_a_zsbuffer(self):
        self.assertRegex(
            self.probe,
            r'bridge\(fd,\s*0x82,\s*0x2,\s*&zs_in',
            'the node probe does not exercise 0x82:0x2')
        self.assertRegex(
            self.probe,
            r'bridge\(fd,\s*0x82,\s*0x3,\s*&zsd_in',
            'the node probe does not exercise 0x82:0x3')


class ReservationLifecycle(unittest.TestCase):
    """Reservations carry VA ranges; mappings bind PMRs into them.

    The UMD reserves a VA range and then maps a PMR into it. The bridge used
    to accept both blindly, so a page-table bind built later would have no
    range to program and no way to know which mappings are live. These pin the
    validation without changing anything on the wire.
    """

    @classmethod
    def setUpClass(cls):
        cls.text = strip_comments(SOURCE.read_text())
        cls.reserve = function_body('pvr_cmd_pmr_reserve', cls.text)
        cls.map = function_body('pvr_cmd_pmr_map', cls.text)
        cls.unmap = function_body('pvr_cmd_unmap_pmr', cls.text)
        cls.unreserve = function_body('pvr_cmd_unreserve_range', cls.text)

    def test_reserve_validates_and_stores_the_range(self):
        self.assertIn('pvr_reservation_find', self.text,
                      'no reservation lookup helper exists')
        self.assertRegex(self.reserve, r'obj->arg0\s*=\s*in\.address',
                         'reserve does not store the VA')
        self.assertRegex(self.reserve, r'obj->arg1\s*=\s*in\.length',
                         'reserve does not store the length')
        self.assertIn('-EEXIST', self.reserve,
                      'overlapping reservations are not rejected')
        self.assertIn('-EINVAL', self.reserve,
                      'zero-length or overflowing ranges are not rejected')

    def test_map_validates_reservation_and_fit(self):
        self.assertIn('pvr_reservation_find(file, in.reservation)', self.map,
                      'map does not resolve the reservation')
        self.assertIn('-ENOSPC', self.map,
                      'an oversized PMR is not rejected against its range')
        self.assertIn('pmr->mapped++', self.map,
                      'map does not count live mappings')
        # The OUT value stays the PMR handle: the UMD passes it back to
        # UnmapPMR, so changing it would break the contract.
        self.assertIn('out.mapping = pmr->handle', self.map)

    def test_unmap_requires_a_live_mapping(self):
        self.assertIn('-ENOENT', self.unmap,
                      'unmapping an unmapped PMR succeeds silently')
        self.assertIn('pmr->mapped_reservation = 0', self.unmap,
                      'unmap does not release the reservation link')

    def test_unreserve_refuses_live_mappings(self):
        self.assertIn('-EBUSY', self.unreserve,
                      'unreserve drops a range the mappings still reference')
        self.assertIn('list_del(&obj->link)', self.unreserve,
                      'unreserve does not retire the reservation')


class BindingLedger(unittest.TestCase):
    """Map/unmap maintains the page-table input ledger (S4-3 step 2, SW).

    Each live MapPMR appends exactly one {va, bytes, pmr, reservation};
    unmap removes it; release drains it. A future mt_gpu_vm bind consumes
    this ledger, so it must be overlap-free (guaranteed by reservation
    checks), bounded, and empty at file teardown.
    """

    @classmethod
    def setUpClass(cls):
        cls.text = strip_comments(SOURCE.read_text())
        cls.map = function_body('pvr_cmd_pmr_map', cls.text)
        cls.unmap = function_body('pvr_cmd_unmap_pmr', cls.text)

    def test_map_appends_exactly_one_entry(self):
        self.assertIn('list_add_tail(&binding->link, &file->bindings)', self.map,
                      'map does not ledger the programmed range')
        self.assertIn('binding->va = res->arg0', self.map)
        self.assertIn('binding->bytes = pmr->bytes', self.map)
        self.assertIn('MT_PVR_MAX_BINDINGS', self.map,
                      'the ledger grows without bound')

    def test_double_map_is_refused(self):
        # A second live map of one PMR would double-program its VA later.
        self.assertIn('if (pmr->mapped)', self.map)
        self.assertIn('-EBUSY', self.map)

    def test_unmap_removes_the_entry(self):
        self.assertIn('list_for_each_entry_safe(b, btmp', self.unmap,
                      'unmap does not drain the ledger')
        self.assertIn("if (b->pmr == pmr->handle)", self.unmap)

    def test_release_drains_the_ledger(self):
        release = function_body('pvr_file_release', self.text)
        self.assertIn('&file->bindings', release,
                      'file teardown leaks ledger entries')
        self.assertIn('INIT_LIST_HEAD(&file->bindings)', self.text,
                      'the ledger list is never initialized')


class SessionHandoff(unittest.TestCase):
    """S4-3 bridge side of DMA registration (no symbol machinery).

    Cross-module symbol_get() does not resolve on this kernel (verified
    empirically, even for printk), so acquisition uses only proven
    primitives: PCI lookup, driver-name check, drvdata, try_module_get.
    The owner ref is stored per-PMR and balanced by exactly one module_put;
    releasing against the current owner instead would imbalance a module
    that changed in between.
    """

    @classmethod
    def setUpClass(cls):
        cls.text = strip_comments(SOURCE.read_text())
        cls.register = function_body('pvr_pmr_dma_register', cls.text)
        cls.release = function_body('pvr_pmr_dma_release', cls.text)
        cls.acquire = function_body('pvr_session_acquire', cls.text)
        cls.map = function_body('pvr_cmd_pmr_map', cls.text)
        cls.unmap = function_body('pvr_cmd_unmap_pmr', cls.text)
        cls.file_release = function_body('pvr_file_release', cls.text)
        cls.pmr_put = function_body('pvr_pmr_put', cls.text)

    def test_no_symbol_machinery(self):
        for name in ('symbol_get', 'symbol_put', 'ops->'):
            self.assertNotIn(name, self.text,
                             f'{name} must not appear: symbol resolution '
                             'does not work on this kernel')

    def test_acquire_validates_binding_and_liveness(self):
        self.assertIn('device_lock', self.acquire)
        self.assertIn('device_unlock', self.acquire)
        self.assertIn('mutex_lock(&g->trial_lock)', self.acquire)
        self.assertIn('mutex_unlock(&g->trial_lock)', self.acquire)
        self.assertRegex(self.acquire,
                         r'strcmp\(pdev->driver->name,\s*"mt_guest_probe"\)')
        self.assertIn('pci_get_drvdata(pdev)', self.acquire)
        self.assertIn('g->trial.pinned', self.acquire)
        self.assertIn('g->trial.connected', self.acquire)
        self.assertIn('try_module_get', self.acquire)

    def test_dead_trial_degrades_to_no_session(self):
        # A drvdata pointer alone is not a live session. On a pinned but
        # disconnected trial, acquisition must drop both the owner ref and
        # the pointer rather than returning a stale/ineligible session.
        self.assertRegex(
            self.acquire,
            r'if\s*\(!g->trial\.pinned\s*\|\|\s*'
            r'!g->trial\.connected\)\s*\{[^}]*module_put\(owner\);'
            r'[^}]*g\s*=\s*NULL;',
        )

    def test_successful_mapping_starts_with_zero_result(self):
        map_loop = self.register.index('for (i = 0; i < npages; i++)',
                                       self.register.index('pmr->dma_addrs = kcalloc'))
        self.assertLess(self.register.rindex('ret = 0;', 0, map_loop), map_loop,
                        'the all-pages-success path must initialize ret')
        self.assertIn('pmr->dma_pdev = pdev', self.register,
                      'PMR must retain the exact device used for mapping')
        self.assertNotIn('TEMP', self.register)
        self.assertNotIn('ZZZZ', self.register)

    def test_gpu_pa_uses_guest_window_translation_not_dma_iova(self):
        self.assertIn('mt_system_address_init', self.register)
        self.assertIn('mt_system_page_address', self.register)
        self.assertIn('page_to_phys(pages[i])', self.register)
        self.assertIn('pmr->dma_addrs[i].dma_addr = addr', self.register)
        self.assertIn('pmr->dma_addrs[i].gpu_pa = gpu_pa', self.register)
        self.assertIn('dma_unmap_page(&pdev->dev, pmr->dma_addrs[i].dma_addr',
                      self.release)

    def test_owner_pairs_exactly(self):
        # register() stores the acquired owner; release() puts exactly that
        # pointer, never the current one.
        self.assertIn('pvr_session_acquire(&pmr->dma_owner)', self.register)
        self.assertIn('module_put(pmr->dma_owner)', self.release)
        self.assertIn('pmr->dma_owner = NULL', self.release)

    def test_aligned_pmr_map_builds_unpublished_gpu_vm_plan(self):
        plan = function_body('pvr_gpu_vm_bind', self.text)
        self.assertIn('IS_ALIGNED(binding->va, PAGE_SIZE)', plan,
                      'fallback path lost its alignment guard')
        self.assertIn('IS_ALIGNED(pmr->bytes, PAGE_SIZE)', plan)
        self.assertIn('mt_gpu_vm_bind_many(&file->gpu_vm, cover, npages)',
                      plan)
        self.assertIn('binding->gpu_bound = true', plan)
        self.assertIn('gpu_first', plan)
        self.assertIn('gpu_npages', plan)
        self.assertIn('gpu_plan', self.text)
        self.assertNotIn('mt_vm_vram_upload', plan)
        self.assertIn('pvr_gpu_vm_bind(file, pmr, binding)', self.map)
        self.assertIn('pvr_gpu_vm_unbind(file, binding)', self.unmap)

    def test_pmr_and_file_teardown_drop_vm_plan_before_backing(self):
        self.assertIn('if (pmr->mapped)', self.pmr_put,
                      'a mapped PMR must survive until VM unbind')
        self.assertLess(self.file_release.index('pvr_gpu_vm_unbind'),
                        self.file_release.index('pvr_gpu_vm_destroy'))
        self.assertLess(self.file_release.index('pvr_gpu_vm_destroy'),
                        self.file_release.index('pvr_pmr_unref'))

    def test_pages_cross_as_struct_page(self):
        # virt_to_page() is invalid on vmalloc addresses and the bridge
        # backs PMRs with vzalloc; the page itself crosses, never an
        # address round trip.
        self.assertIn('pages[i] = vmalloc_to_page(', self.register,
                      'bridge must pass struct page * from vmalloc_to_page')
        self.assertNotIn('page_address(pages[i])', self.register,
                         'page_address round trip breaks on vmalloc pages')

    def test_map_failures_unwind(self):
        self.assertIn('dma_unmap_page', self.register,
                      'partial mapping failures leak DMA mappings')

    def test_release_unmaps_original_device(self):
        self.assertIn('if (pmr->dma_addrs && pdev)', self.release)
        self.assertIn('dma_unmap_page', self.release)
        self.assertIn('pmr->dma_pdev', self.release,
                      'release must unmap against the exact device used to map')
        self.assertIn('kfree(pmr->dma_addrs)', self.release)
        self.assertIn('kfree(pmr->gpu_pages)', self.release)

    def test_registration_serializes_with_trial_teardown(self):
        self.assertIn('mutex_lock(&g->trial_lock)', self.register)
        self.assertIn('mutex_unlock(&g->trial_lock)', self.register)
        self.assertIn('g->trial.pinned', self.register)
        self.assertIn('g->trial.connected', self.register)

    def test_map_path_ignores_registration_failure(self):
        # Registration or page-plan failure degrades to the unchanged PMR
        # mapping result; the later submission/page-table handoff is separate.
        self.assertIn('ret = pvr_pmr_dma_register(file, pmr)', self.map)
        self.assertIn('binding->gpu_result = ret ? ret : pvr_gpu_vm_bind',
                      self.map)
        self.assertIn('out.mapping = pmr->handle', self.map)
        self.assertIn('return pvr_out(cmd, &out, sizeof(out))', self.map)

    def test_aligned_map_builds_only_a_cpu_vm_plan(self):
        plan = function_body('pvr_gpu_vm_bind', self.text)
        self.assertIn('IS_ALIGNED(binding->va, PAGE_SIZE)', plan)
        self.assertIn('IS_ALIGNED(pmr->bytes, PAGE_SIZE)', plan)
        self.assertIn('mt_gpu_vm_bind_many(&file->gpu_vm, cover, npages)',
                      plan)
        self.assertNotIn('upload', plan)
        self.assertIn('binding->gpu_bound = true', plan)
        self.assertIn('gpu_pa', plan)

    def test_cover_set_binds_per_page_and_unbinds_the_set(self):
        # Arena-backed ranges (aligned or not) bind one 4 KiB entry per
        # cover page against the file-level arena facade; unbind walks the
        # same recorded set. A cover page already live elsewhere refuses
        # the whole bind inside bind_many (-EEXIST) instead of aliasing.
        plan = function_body('pvr_gpu_vm_bind', self.text)
        self.assertIn('&file->arena_bo', plan)
        self.assertIn('.bytes = PAGE_SIZE', plan)
        self.assertIn('binding->gpu_first = first', plan)
        self.assertIn('binding->gpu_npages = npages', plan)
        unbind = function_body('pvr_gpu_vm_unbind', self.text)
        self.assertIn('binding->gpu_first', unbind)
        self.assertIn('binding->gpu_npages', unbind)
        self.assertIn('PAGE_SIZE', unbind)

    def test_byte_tight_fallback_still_degrades_from_gpu_plan(self):
        # Private-vzalloc (non-arena) PMRs keep the old rule: unaligned
        # VA/bytes degrade, because their pages cannot be shared. Arena
        # PMRs take the cover path above instead.
        plan = function_body('pvr_gpu_vm_bind', self.text)
        self.assertIn('-EOPNOTSUPP', plan)
        self.assertIn('binding->gpu_result', self.map)
        self.assertIn('out.mapping = pmr->handle', self.map)

    def test_unmap_and_file_close_retire_cpu_plan_before_pmr(self):
        self.assertIn('pvr_gpu_vm_unbind(file, binding)', self.unmap)
        self.assertLess(self.text.index('pvr_gpu_vm_unbind(file, binding)',
                                        self.text.index('static void pvr_file_release')),
                        self.text.index('pvr_gpu_vm_destroy(file)',
                                        self.text.index('static void pvr_file_release')))

    def test_flags_are_recorded_for_the_translator(self):
        self.assertIn('pmr->alloc_flags = in.flags', self.text)
        self.assertIn('binding->map_flags = in.map_flags', self.text)
        self.assertIn('u32 alloc_flags', self.text)
        self.assertIn('u32 map_flags', self.text)

    def test_unref_releases_dma_before_freeing(self):
        unref = function_body('pvr_pmr_unref', self.text)
        body = unref[unref.index('if (--pmr->refcount)'):]
        self.assertLess(body.index('pvr_pmr_dma_release(pmr)'),
                        body.index('vfree(pmr->host)'),
                        'DMA must be released before the backing is freed')


class MemAllocFlags(unittest.TestCase):
    """Decode the live map/alloc flags against the vendor bit definitions.

    Rung8 values: 0x333 (statics), 0x1233 (the two big heap PMRs), 0x303
    (small GPU-only PMRs). The bridge records them; it does not enforce
    them yet. Bits read from the in-tree 2.7.1 pvrsrv_memallocflags.h so a
    header refresh that renumbers bits fails here instead of silently
    reclassifying memory.
    """
    HEADER = (Path(__file__).resolve().parents[1] / 'src' / 'mtgpu-2.7.1-6.12'
              / 'inc' / 'pvr' / 'include' / 'pvrsrv_memallocflags.h')

    @classmethod
    def setUpClass(cls):
        cls.text = cls.HEADER.read_text()

    def bit(self, name):
        import re
        m = re.search(r'#define\s+%s\s+\(1ULL<<(\d+)\)' % name, self.text)
        self.assertIsNotNone(m, '%s bit moved' % name)
        return 1 << int(m.group(1))

    def test_gpu_rw_in_all_observed(self):
        gpu_r = self.bit('PVRSRV_MEMALLOCFLAG_GPU_READABLE')
        gpu_w = self.bit('PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE')
        for flags in (0x333, 0x1233, 0x303):
            self.assertTrue(flags & gpu_r and flags & gpu_w,
                            '%#x lost GPU R/W' % flags)

    def test_cpu_access_absent_from_gpu_only(self):
        cpu_r = self.bit('PVRSRV_MEMALLOCFLAG_CPU_READABLE')
        cpu_w = self.bit('PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE')
        self.assertTrue(0x333 & cpu_r and 0x333 & cpu_w)
        self.assertTrue(0x1233 & cpu_r and 0x1233 & cpu_w)
        self.assertFalse(0x303 & (cpu_r | cpu_w))

    def test_cpu_coherent_only_on_big_heap_pmrs(self):
        coherent = 2 << 11  # PVRSRV_MEMALLOCFLAG_CPU_CACHE_COHERENT
        self.assertTrue(0x1233 & coherent)
        self.assertFalse(0x333 & coherent)
        self.assertFalse(0x303 & coherent)


class ArenaBacking(unittest.TestCase):
    """Per-file PMR backing arena (r55 answer to byte-tight PMRs).

    PMR bytes live at arena_base + arena_offset so VA-neighbor ranges can
    share physical pages once the plan binds per-page cover sets. The
    private-vzalloc fallback preserves exact old behavior when the arena
    cannot fit a request; use sites (mmap/DMA/plan) only ever read
    pmr->host, so they are unchanged.
    """

    @classmethod
    def setUpClass(cls):
        cls.text = strip_comments(SOURCE.read_text())
        cls.new = function_body('pvr_pmr_new', cls.text)
        cls.unref = function_body('pvr_pmr_unref', cls.text)
        cls.release = function_body('pvr_file_release', cls.text)
        cls.mmap = function_body('pvr_mmap', cls.text)

    def test_arena_structs_exist(self):
        for token in ('arena_base', 'arena_free', 'arena_high_water',
                      'arena_fallbacks', 'arena_offset', 'arena_pages',
                      'MT_PVR_ARENA_BYTES', 'MT_PVR_ARENA_PAGES'):
            self.assertIn(token, self.text)

    def test_new_prefers_arena_and_zeroes_reused_slots(self):
        self.assertIn('pvr_arena_ensure', self.new)
        self.assertIn('pvr_arena_alloc', self.new)
        # Reused runs still hold the previous owner's bytes, unlike vzalloc.
        self.assertIn('memset(pmr->host, 0', self.new)
        # host points into the arena either way; use sites are untouched.
        self.assertIn('(u8 *)file->arena_base', self.new)

    def test_giant_request_cannot_truncate_into_arena(self):
        # The fit check runs on the full-precision count before the u32 cast.
        self.assertIn('want <= MT_PVR_ARENA_PAGES', self.new)

    def test_fallback_preserves_old_behavior_and_is_counted(self):
        self.assertIn('vzalloc(bytes ? bytes : 1)', self.new)
        self.assertIn('arena_fallbacks++', self.new)

    def test_unref_returns_segment_or_vfrees(self):
        self.assertIn('pvr_arena_free(pmr->file', self.unref)
        self.assertIn('vfree(pmr->host)', self.unref)

    def test_failed_alloc_returns_its_segment(self):
        self.assertIn('pvr_arena_free(file, pmr->arena_offset',
                      self.new)

    def test_mmap_out_holds_lock_for_unref(self):
        tail = self.mmap[self.mmap.index('out:'):]
        self.assertLess(tail.index('mutex_lock(&file->lock)'),
                        tail.index('pvr_pmr_unref(pmr)'))
        self.assertIn('mutex_unlock(&file->lock)', tail)

    def test_release_drains_arena_after_pmrs_with_summary(self):
        self.assertLess(self.release.index('pvr_pmr_unref(pmr)'),
                        self.release.index('vfree(file->arena_base)'))
        self.assertIn('arena close:', self.release)
        self.assertIn('arena_fallbacks', self.release)

    def test_alloc_splits_and_free_coalesces(self):
        alloc = function_body('pvr_arena_alloc', self.text)
        free = function_body('pvr_arena_free', self.text)
        self.assertIn('list_replace', alloc)
        self.assertIn('arena_high_water', alloc)
        self.assertIn('list_del', free)


if __name__ == '__main__':
    unittest.main()
