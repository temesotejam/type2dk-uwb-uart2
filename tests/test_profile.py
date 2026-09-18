import copy
import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('profile', ROOT/'scripts/generate_profile.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


class ProfileTests(unittest.TestCase):
    def setUp(self):
        self.p = json.loads((ROOT/'config/example_3bp.json').read_text())

    def test_example_is_unconfirmed(self):
        self.assertIn('#define TAG_PROFILE_CONFIRMED 0', m.generate(self.p, 'A'))
        self.assertIn('#define TAG_NODE 2', m.generate(self.p, 'B'))

    def test_invalid_profiles(self):
        mutations = [
            lambda p: p['tags']['B'].update(address=p['tags']['A']['address']),
            lambda p: p['tags']['B']['sessions'][0].update(session_id=p['tags']['A']['sessions'][0]['session_id']),
            lambda p: p['tags']['A']['sessions'][0].update(anchor_address=p['tags']['B']['address']),
            lambda p: p['tags']['A']['sessions'][0].update(interval_ms=-1),
            lambda p: p['tags']['B']['sessions'][0].update(anchor_address=0x3333),
            lambda p: p['radio'].update(channel=6),
        ]
        for mutate in mutations:
            p = copy.deepcopy(self.p);mutate(p)
            with self.assertRaises(ValueError):
                m.validate(p)

    def test_matching_anchor_endpoints(self):
        import re
        p = json.loads((ROOT/'config/dual_3bp.json').read_text())
        pairs = set()
        for anchor in (1, 2, 3):
            h = m.generate_anchor(p, anchor)
            rows = re.findall(r'\{(\d+)u,(\d+)u,(\d+)u,(\d+)u,\d+u,\d+u,\d+u,\d+u\}', h)
            self.assertEqual(len(rows), 2)
            for sid, period, peer, node in map(lambda r: map(int, r), rows):
                name = 'A' if node == 1 else 'B'
                tag = p['tags'][name]
                s = next(s for s in tag['sessions'] if s['anchor_id'] == anchor)
                self.assertEqual((sid, period, peer), (s['session_id'], s['interval_ms'], tag['address']))
                self.assertFalse(s['tag_initiator'])
                self.assertIn(f'#define ANCHOR_ADDRESS {s["anchor_address"]}u', h)
                pairs.add((node, anchor))
        self.assertEqual(len(pairs), 6)
        with self.assertRaises(ValueError):
            m.generate_anchor(self.p, 1)


    def test_seven_anchor_multicast(self):
        p = json.loads((ROOT/'config/dual_7bp.json').read_text())
        m.validate(p)
        for node in ('A', 'B'):
            self.assertEqual(len(p['tags'][node]['sessions']), 1)
            self.assertEqual(len(m.targets(p['tags'][node]['sessions'][0])), 7)
        self.assertNotEqual(p['tags']['A']['sessions'][0]['channel'], p['tags']['B']['sessions'][0]['channel'])
        for i in range(1, 8):
            anchor = i*0x1111
            h = m.generate_anchor(p, anchor)
            self.assertIn(f'#define ANCHOR_ADDRESS {anchor}u', h)
            for name in ('A','B'):
                t = p['tags'][name];s = t['sessions'][0]
                expected = '{%du,%du,%du,%du,0u,1u,%du,%du}' % (s['session_id'],s['interval_ms'],t['address'],t['node_id'],s['channel'],i)
                self.assertIn(expected,h)
        bad = copy.deepcopy(p)
        bad['tags']['A']['sessions'][0]['anchors'].append(bad['tags']['A']['sessions'][0]['anchors'][0])
        with self.assertRaises(ValueError):m.validate(bad)
        bad = copy.deepcopy(p)
        bad['tags']['B']['sessions'][0]['session_id'] = bad['tags']['A']['sessions'][0]['session_id']
        with self.assertRaises(ValueError):m.validate(bad)
        bad = copy.deepcopy(p);bad['tags']['B']['sessions'][0]['anchors'].pop()
        with self.assertRaises(ValueError):m.generate_anchor(bad,0x7777)


if __name__ == '__main__':
    unittest.main()
