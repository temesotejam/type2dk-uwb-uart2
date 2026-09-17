#!/usr/bin/env python3
"""Check CoreS3 0.1.1+ raw Tera Term logs without altering the input file."""
import argparse
import binascii
import json
from pathlib import Path


def check(data):
    result = dict(lines=0, valid=0, invalid_lines=[], missing_records=0,
                  duplicate_records=0, resets_or_backwards=0, ports={})
    previous = None
    for number, line in enumerate(data.splitlines(), 1):
        if not line:
            continue
        result['lines'] += 1
        try:
            body, crc = line.rsplit(b',log_crc=', 1)
            if len(crc) != 4 or int(crc, 16) != binascii.crc_hqx(body, 0xffff):
                raise ValueError('CRC')
            parts = body.decode('ascii').split(',')
            if parts[0] not in ('UWB_EVENT', 'DUAL_STAT'):
                raise ValueError('record')
            fields = dict(part.split('=', 1) for part in parts[1:])
            if len(fields) != len(parts)-1 or fields.get('port') not in ('A', 'B'):
                raise ValueError('fields')
            seq = int(fields['log_seq'])
            if not 0 <= seq <= 0xffffffff:
                raise ValueError('sequence')
        except (ValueError, KeyError, UnicodeError):
            result['invalid_lines'].append(number)
            continue
        result['valid'] += 1
        if previous is not None:
            delta = (seq-previous) & 0xffffffff
            if delta == 0:
                result['duplicate_records'] += 1
            elif delta < 0x80000000:
                result['missing_records'] += delta-1
            else:
                result['resets_or_backwards'] += 1
        previous = seq
        port = result['ports'].setdefault(fields['port'], dict(events=0))
        if parts[0] == 'UWB_EVENT':
            port['events'] += 1
        else:
            port.setdefault('first_stat', fields)
            port['last_stat'] = fields
    return result


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('log', type=Path)
    args = ap.parse_args()
    report = check(args.log.read_bytes())
    print(json.dumps(report, indent=2))
    raise SystemExit(1 if report['invalid_lines'] or report['missing_records'] or
                     report['duplicate_records'] or report['resets_or_backwards'] or
                     not report['valid'] else 0)
