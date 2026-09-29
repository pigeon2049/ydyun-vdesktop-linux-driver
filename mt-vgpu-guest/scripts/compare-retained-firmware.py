#!/usr/bin/env python3
"""Read saved 8 MiB firmware snapshots; never access a device or acknowledge events."""
import argparse
import hashlib
import json
from pathlib import Path
import struct


def decode(blob):
    if len(blob) != 0x800000:
        raise ValueError('Expected an exact 8 MiB firmware snapshot')
    result = {'bytes': len(blob), 'sha256': hashlib.sha256(blob).hexdigest(),
              'started': struct.unpack_from('<I', blob, 4)[0], 'queues': []}
    for dm in range(6):
        base = 0x6ddd0 + dm * 0x2e30
        for ring in range(3):
            head, tail = struct.unpack_from('<I4xI', blob, base + 0x2e00 + ring * 16)
            item = {'dm': dm, 'ring': ring, 'head': head, 'tail': tail,
                    'valid_cursors': head < 64 and tail < 64}
            if item['valid_cursors']:
                item['pending'] = (head - tail) & 63
                if ring == 2:
                    item['pending_events'] = [list(struct.unpack_from(
                        '<6I', blob, base + 0x2800 + ((tail + i) & 63) * 24))
                        for i in range(item['pending'])]
            result['queues'].append(item)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('snapshot', type=Path)
    parser.add_argument('--compare', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    current = args.snapshot.read_bytes()
    result = decode(current)
    result['snapshot'] = str(args.snapshot.resolve())
    if args.compare:
        prior = args.compare.read_bytes()
        old = decode(prior)
        result.update(comparison=str(args.compare.resolve()),
                      comparison_sha256=old['sha256'], identical=current == prior,
                      changed_dwords=sum(current[i:i+4] != prior[i:i+4]
                                         for i in range(0, len(current), 4)))
    result['hardware_access'] = False
    text = json.dumps(result, indent=2) + '\n'
    if args.output:
        args.output.write_text(text)
    print(text, end='')


if __name__ == '__main__':
    main()
