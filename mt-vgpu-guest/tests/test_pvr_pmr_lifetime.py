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
        # is a plain mutex, so a second lock here self-deadlocks and wedges the
        # caller in uninterruptible sleep. This cost a reboot to undo once.
        self.assertEqual(
            len(re.findall(r'mutex_lock', self.mmap)), 1,
            'mmap takes file->lock more than once')
        self.assertEqual(
            len(re.findall(r'mutex_unlock', self.mmap)), 1,
            'mmap releases file->lock a different number of times than it takes it')

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
        for m in re.finditer(r'\b(vfree|kfree)\s*\(\s*pmr\b', self.text):
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


if __name__ == '__main__':
    unittest.main()