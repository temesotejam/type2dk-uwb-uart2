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


if __name__ == '__main__':
    unittest.main()
