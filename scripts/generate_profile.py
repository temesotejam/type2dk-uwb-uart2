#!/usr/bin/env python3
"""Validate the *whole* A/B profile before producing one tag's C header."""
import argparse
import json
from pathlib import Path


def integer(v, lo, hi, name):
    if type(v) is not int or not lo <= v <= hi:
        raise ValueError(f'{name}: expected integer {lo}..{hi}')
    return v


def validate(p):
    if type(p['confirmed_against_anchors']) is not bool:
        raise ValueError('confirmed_against_anchors must be boolean')
    radio = p['radio']
    for key, lo, hi in [('channel', 5, 9), ('sfd', 0, 4), ('preamble', 9, 12),
                        ('rframe', 1, 3), ('slots', 1, 255), ('slot_duration', 1, 65535)]:
        integer(radio[key], lo, hi, key)
    if radio['channel'] not in (5, 9):
        raise ValueError('SR040 profile supports channel 5 or 9')
    if radio['rframe'] not in (1, 3):
        raise ValueError('select SP1 or SP3 explicitly')
    integer(radio.get('vendor_id', 0x0708), 0, 65535, 'vendor_id')
    iv = radio.get('static_sts_iv', [1, 2, 3, 4, 5, 6])
    if len(iv) != 6:
        raise ValueError('static_sts_iv requires 6 bytes')
    for byte in iv:
        integer(byte, 0, 255, 'static_sts_iv byte')
    session_ids, tag_addresses = set(), set()
    anchor_addresses = {}
    for index, name in enumerate(('A', 'B'), 1):
        t = p['tags'][name]
        if t['node_id'] != index:
            raise ValueError('A must be node 1; B must be node 2')
        addr = integer(t['address'], 1, 65534, 'tag address')
        if addr in tag_addresses:
            raise ValueError('A and B require distinct addresses')
        tag_addresses.add(addr)
        if not 1 <= len(t['sessions']) <= 5:
            raise ValueError('1..5 sessions per tag (SDK limit; hardware capacity still checked at init)')
        peers, anchors = set(), set()
        for s in t['sessions']:
            sid = integer(s['session_id'], 1, 0xffffffff, 'session_id')
            anchor = integer(s['anchor_id'], 1, 65534, 'anchor_id')
            peer = integer(s['anchor_address'], 1, 65534, 'anchor_address')
            if sid in session_ids or peer in peers or anchor in anchors:
                raise ValueError('duplicate session/anchor in profile')
            session_ids.add(sid); peers.add(peer); anchors.add(anchor)
            if anchor in anchor_addresses and anchor_addresses[anchor] != peer:
                raise ValueError('anchor ID maps to different addresses for A/B')
            anchor_addresses[anchor] = peer
            integer(s['interval_ms'], 200, 60000, 'interval_ms')
            integer(s['start_offset'], 0, 255, 'start_offset')
            if type(s['tag_initiator']) is not bool:
                raise ValueError('tag_initiator must be boolean')
    if tag_addresses.intersection(anchor_addresses.values()):
        raise ValueError('a tag cannot be an anchor (no A-B sessions)')
    if len(set(anchor_addresses.values())) != len(anchor_addresses):
        raise ValueError('anchor addresses must be unique')


def generate(p, node):
    validate(p)
    t, radio = p['tags'][node], p['radio']
    lines = ['/* Generated; source of truth is config JSON. */', '#ifndef TAG_PROFILE_H', '#define TAG_PROFILE_H',
             '#include <stdint.h>', f'#define TAG_NODE {t["node_id"]}',
             f'#define TAG_ADDRESS {t["address"]}u',
             f'#define TAG_PROFILE_CONFIRMED {int(p["confirmed_against_anchors"])}']
    for key, val in radio.items():
        if key in ('channel', 'sfd', 'preamble', 'rframe', 'slots', 'slot_duration'):
            lines.append(f'#define TAG_RADIO_{key.upper()} {val}u')
    lines += [f'#define TAG_RADIO_VENDOR_ID {radio.get("vendor_id", 0x0708)}u',
              'static const uint8_t tag_sts_iv[6] = {' + ','.join(map(str, radio.get('static_sts_iv', [1,2,3,4,5,6]))) + '};']
    lines += ['typedef struct {uint32_t id,interval; uint16_t anchor,peer; uint8_t init,offset;} tag_session_t;',
              'static const tag_session_t tag_sessions[] = {']
    for s in t['sessions']:
        lines.append('    {%du,%du,%du,%du,%du,%du},' % (s['session_id'], s['interval_ms'],
                     s['anchor_id'], s['anchor_address'], s['tag_initiator'], s['start_offset']))
    lines += ['};', '#define TAG_SESSION_COUNT (sizeof(tag_sessions)/sizeof(tag_sessions[0]))', '#endif', '']
    return '\n'.join(lines)


def generate_anchor(p, anchor):
    validate(p)
    # Invert the same sessions, never maintain a second address/session table.
    sessions = [(name, t, s) for name, t in p['tags'].items()
                for s in t['sessions'] if s['anchor_id'] == anchor]
    if len(sessions) != 2 or {n for n, _, _ in sessions} != {'A', 'B'}:
        raise ValueError('each anchor needs exactly one A and one B session')
    if any(s['tag_initiator'] for _, _, s in sessions):
        raise ValueError('this anchor application requires controller/initiator anchors')
    if not p['confirmed_against_anchors']:
        raise ValueError('anchor builds require a matched profile')
    header = generate(p, 'A').split('typedef struct')[0]
    header = header.replace('TAG_PROFILE_H', 'ANCHOR_PROFILE_H')
    header += f'#define ANCHOR_ID {anchor}u\n#define ANCHOR_ADDRESS {sessions[0][2]["anchor_address"]}u\n'
    header += 'typedef struct {uint32_t id,interval;uint16_t peer;uint8_t node;} anchor_session_t;\n'
    header += 'static const anchor_session_t anchor_sessions[2] = {\n'
    for _, t, s in sessions:
        header += '    {%du,%du,%du,%du},\n' % (s['session_id'], s['interval_ms'], t['address'], t['node_id'])
    return header + '};\n#endif\n'


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('profile', type=Path)
    group = ap.add_mutually_exclusive_group(required=True)
    group.add_argument('--node', choices=['A', 'B'])
    group.add_argument('--anchor', type=int)
    ap.add_argument('--out', type=Path, required=True)
    a = ap.parse_args()
    a.out.parent.mkdir(parents=True, exist_ok=True)
    p = json.loads(a.profile.read_text())
    a.out.write_text(generate_anchor(p, a.anchor) if a.anchor is not None else generate(p, a.node))
