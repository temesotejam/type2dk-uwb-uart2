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
    lines += ['typedef struct {uint32_t id,interval; uint16_t anchor,peer; uint8_t init,offset;} tag_session_t;',
              'static const tag_session_t tag_sessions[] = {']
    for s in t['sessions']:
        lines.append('    {%du,%du,%du,%du,%du,%du},' % (s['session_id'], s['interval_ms'],
                     s['anchor_id'], s['anchor_address'], s['tag_initiator'], s['start_offset']))
    lines += ['};', '#define TAG_SESSION_COUNT (sizeof(tag_sessions)/sizeof(tag_sessions[0]))', '#endif', '']
    return '\n'.join(lines)


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('profile', type=Path)
    ap.add_argument('--node', choices=['A', 'B'], required=True)
    ap.add_argument('--out', type=Path, required=True)
    a = ap.parse_args()
    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_text(generate(json.loads(a.profile.read_text()), a.node))
