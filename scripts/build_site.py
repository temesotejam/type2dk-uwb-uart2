#!/usr/bin/env python3
"""Publish the exact merged binary from the verified firmware distribution."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]
ap = argparse.ArgumentParser()
ap.add_argument('--distribution', type=Path, default=ROOT/'build/distribution')
ap.add_argument('--out', type=Path, default=ROOT/'build/site')
a = ap.parse_args()
info = json.loads((a.distribution/'build-info.json').read_text())
name = 'cores3-dual-merged.bin'
binary = (a.distribution/name).read_bytes()
digest = hashlib.sha256(binary).hexdigest()
if len(binary) != info['files'][name]['bytes'] or digest != info['files'][name]['sha256']:
    raise ValueError('CoreS3 firmware checksum mismatch')
a.out.mkdir(parents=True, exist_ok=True)
shutil.copytree(ROOT/'web', a.out, dirs_exist_ok=True)
shutil.copytree(a.distribution, a.out/'firmware', dirs_exist_ok=True)
target = f'firmware/cores3-dual-{digest[:12]}.bin'
(a.out/target).write_bytes(binary)
info.update(firmware_version=info['version']+'-dual', installer_file=target,
            installer_bytes=len(binary), installer_sha256=digest)
manifest = {
    'name': 'CoreS3 Type2DK A+B UART receiver', 'version': info['firmware_version'],
    'new_install_prompt_erase': True, 'new_install_improv_wait_time': 0,
    'builds': [{'chipFamily': 'ESP32-S3', 'parts': [{'path': target, 'offset': 0}]}],
}
(a.out/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
(a.out/'build-info.json').write_text(json.dumps(info, indent=2)+'\n')
(a.out/'.nojekyll').touch()
print(json.dumps({'site': str(a.out), 'version': info['firmware_version'], 'sha256': digest}))
