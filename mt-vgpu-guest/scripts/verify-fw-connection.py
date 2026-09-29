#!/usr/bin/env python3
"""Compare Guest transition decisions with reference instructions and modeled FW.

All device/queue interactions are recorded callbacks. This tests scheduling,
retries and state decisions, not firmware behavior or hardware compatibility.
"""
import ctypes
import errno
import json
from pathlib import Path
import subprocess
from reference_oracle import ReferenceOracle

ROOT = Path(__file__).resolve().parents[1]
DEVICE, PLATFORM, LAYOUT, IMAGE, QUEUE = 0x210000, 0x214000, 0x218000, 0x220000, 0x221000
READ = ctypes.CFUNCTYPE(ctypes.c_uint32, ctypes.c_void_p)
IDLE = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p)
WRITE = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_uint32)
VOID = ctypes.CFUNCTYPE(None, ctypes.c_void_p)
SEND = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_uint32)


class Ops(ctypes.Structure):
    _fields_ = [('firmware_state', READ), ('firmware_started', READ), ('guest_state', WRITE),
                ('notify_online', VOID), ('send_command', SEND), ('work_idle', IDLE),
                ('control_idle', IDLE), ('delay_25ms', VOID), ('gpu_normal', READ)]


class Model:
    def __init__(self, mode, accept=0, started=0, reject=None, work=0, control=0,
                 shutdown=None, send_error=0, normal=1, ready=0):
        self.mode, self.accept, self.started_at, self.reject = mode, accept, started, reject
        self.work_at, self.control_after = work, control
        self.shutdown, self.send_error = shutdown, send_error
        self.normal, self.ready_at = normal, ready
        self.tick, self.disconnected_at = 0, None
        self.events = []
        self.oracle = None

    def firmware_state(self, _=None):
        if self.tick < self.ready_at:
            state = 0
        elif self.shutdown is not None and self.tick >= self.shutdown:
            state = 1
        elif self.reject is not None and self.tick >= self.reject:
            state = 4
        else:
            state = 2 if self.mode == 'disconnect' or self.tick >= self.accept else 1
        self.events.append(('read-fw', state))
        return state

    def started(self, _=None):
        # The reference reads this field directly, not through an intercepted
        # helper. Exclude that read from both traces while comparing decisions.
        return int(self.tick >= self.started_at)

    def guest_state(self, _, state):
        self.events.append(('guest', state))

    def online(self, _=None):
        self.events.append(('online', self.tick))

    def gpu_normal(self, _=None):
        self.events.append(('gpu-normal', self.normal))
        return self.normal

    def send(self, _, opcode):
        self.events.append(('send', opcode, self.tick))
        if opcode == 0x47:
            self.disconnected_at = self.tick
        return self.send_error

    def work(self, _=None):
        result = int(self.tick >= self.work_at)
        self.events.append(('work-idle', result))
        return result

    def control(self, _=None):
        result = int(self.disconnected_at is not None and
                     self.tick >= self.disconnected_at + self.control_after)
        self.events.append(('control-idle', result))
        return result

    def delay(self, _=None):
        self.events.append(('delay', 25))
        self.tick += 1
        if self.oracle:
            self.oracle.put32(IMAGE + 4, self.started())

    def ops(self):
        return Ops(READ(self.firmware_state), READ(self.started), WRITE(self.guest_state),
                   VOID(self.online), SEND(self.send), IDLE(self.work), IDLE(self.control), VOID(self.delay),
                   READ(self.gpu_normal))

    def reference(self):
        oracle = self.oracle = ReferenceOracle()
        oracle.put64(DEVICE + 0x418, PLATFORM)
        oracle.put64(DEVICE + 0x420, LAYOUT)
        oracle.put64(LAYOUT + 0xc8, IMAGE)
        oracle.put64(LAYOUT + 0xe0, QUEUE)
        oracle.put32(IMAGE + 4, self.started())
        oracle.put64(DEVICE + 0x288, 0x2f8000)
        oracle.hooks[0x2f8000] = lambda: oracle.ret(1)
        oracle.put64(PLATFORM + 0x1cd0, 0x230000)
        oracle.uc.mem_write(0x230000, bytes([self.normal]))
        # Record the call, but let the original shared-byte load execute.
        oracle.hooks[0x140023874] = lambda: self.gpu_normal()

        def state():
            op = oracle.arg(1)
            if op == 1:
                oracle.ret(self.firmware_state())
            else:
                assert op == 3
                self.guest_state(None, oracle.arg(2) & 0xffffffff)
                oracle.ret()

        def send():
            assert oracle.arg(0) == PLATFORM and oracle.arg(1) == LAYOUT + 0xc0
            assert oracle.arg(4) == 0
            self.send(None, oracle.arg(3) & 0xffffffff)
            oracle.ret()

        def delay():
            assert oracle.arg(0) == 25000
            self.delay()
            oracle.ret()

        oracle.hooks[0x140023afc] = state
        oracle.hooks[0x14002249c] = lambda: (self.online(), oracle.ret())
        oracle.hooks[0x14000c0ac] = send
        oracle.hooks[0x140008404] = delay
        oracle.hooks[0x14000bc9c] = lambda: oracle.ret(self.work())
        oracle.hooks[0x14000bdc4] = lambda: oracle.ret(self.control())
        health = [(0x140023874, 0x140023880), (0x140130c10, 0x140130c12)]
        if self.mode == 'connect':
            return oracle.run(0x1400168b4, [DEVICE], [(0x1400168b4, 0x140016b4b)] + health) & 255
        return oracle.run(0x140016b4c, [DEVICE], [(0x140016b4c, 0x140016d0e)] + health) & 255


def main():
    library = ROOT / 'build/firmware/connection.so'
    library.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-O2', '-shared', '-fPIC',
                    str(ROOT / 'tests/fw_connection_wrapper.c'), '-o', str(library)], check=True)
    lib = ctypes.CDLL(str(library))
    lib.connect_fw.argtypes = lib.disconnect_fw.argtypes = [ctypes.POINTER(Ops), ctypes.c_void_p]
    profiles = []
    for accept in (0, 1, 99, 100, 101, 200, 301, 399, 400):
        profiles.append({'mode': 'connect', 'accept': accept, 'started': accept})
    profiles += [{'mode': 'connect', 'accept': 0, 'started': t} for t in (3, 101, 400)]
    profiles += [{'mode': 'connect', 'accept': 999, 'reject': t} for t in (0, 101, 399)]
    profiles += [{'mode': 'disconnect', 'work': w, 'control': c}
                 for w, c in ((0, 0), (1, 0), (400, 0), (401, 0), (402, 0),
                               (0, 1), (0, 399), (0, 400), (2, 3))]
    profiles += [{'mode': 'disconnect', 'work': w, 'control': c, 'shutdown': s}
                 for w, c, s in ((5, 0, 0), (0, 10, 3), (0, 0, 0))]
    profiles += [{'mode': 'connect', 'normal': 0, 'ready': r, 'accept': r + 1,
                  'started': r + 1} for r in (0, 1, 399, 400, 401)]
    profiles += [{'mode': 'connect', 'normal': 0, 'accept': 999},
                 {'mode': 'disconnect', 'normal': 0},
                 {'mode': 'connect', 'normal': 255},
                 {'mode': 'disconnect', 'normal': 255}]
    results = []
    for profile in profiles:
        original, candidate = Model(**profile), Model(**profile)
        success = original.reference()
        ops = candidate.ops()
        result = getattr(lib, profile['mode'] + '_fw')(ctypes.byref(ops), None)
        assert (result == 0) == bool(success), (profile, success, result)
        assert original.events == candidate.events, profile
        if profile.get('normal') == 0 and (profile['mode'] == 'disconnect' or profile.get('ready', 0) >= 400):
            assert not any(e[0] in ('send', 'guest') for e in candidate.events)
        results.append({'profile': profile, 'result': result, 'waits': candidate.tick,
                        'sends': [e[1:] for e in candidate.events if e[0] == 'send']})
    # Linux additionally reports failed queue submission, rather than treating
    # a void Windows send return as success. Never publish Guest ACTIVE/OFF.
    for mode in ('connect', 'disconnect'):
        model = Model(mode, send_error=-errno.EAGAIN)
        ops = model.ops()
        assert getattr(lib, mode + '_fw')(ctypes.byref(ops), None) == -errno.EAGAIN
        assert ('guest', 2) not in model.events and ('guest', 0) not in model.events
    report = {'reference_sha256': original.oracle.pe.sha256, 'hardware_written': False,
              'transition_cases_passed': len(profiles), 'send_errors_propagated': 2,
              'traces_match_reference': True, 'cases': results,
              'modeled_helpers': ['firmware state/started timeline', 'queue status',
                                  'command submission', 'MMIO notification', '25 ms delay',
                                  'health callback present and returns true; original shared-byte getter'],
              'firmware_connection_verified': False}
    (ROOT / 'reports/firmware-connection-validation.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: v for k, v in report.items() if k != 'cases'}, indent=2))


if __name__ == '__main__':
    main()
