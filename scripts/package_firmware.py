#!/usr/bin/env python3
"""Produce flashable artifacts; verify saved Type2DK binaries before packaging."""
import argparse
import base64
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile
from verify_firmware import verify

ROOT = Path(__file__).resolve().parents[1]
ap = argparse.ArgumentParser()
ap.add_argument('--sha', default='local')
a = ap.parse_args()
out = ROOT/'build/distribution'
if out.exists():
    shutil.rmtree(out)
out.mkdir(parents=True, exist_ok=True)
stored = json.loads((ROOT/'firmware/manifest.json').read_text())
for name, meta in stored['files'].items():
    data = base64.b64decode((ROOT/'firmware'/f'{name}.b64').read_text())
    verify(data)
    if len(data) != meta['bytes'] or hashlib.sha256(data).hexdigest() != meta['sha256']:
        raise ValueError(f'Checksum mismatch: {name}')
    (out/name).write_bytes(data)

pio = ROOT/'.pio/build/cores3_dual'
tool = Path.home()/'.platformio/packages/tool-esptoolpy/esptool.py'
subprocess.run([sys.executable, str(tool), '--chip', 'esp32s3', 'merge_bin',
                '-o', str(out/'cores3-dual-merged.bin'), '--flash_mode', 'dio',
                '--flash_freq', '80m', '--flash_size', '16MB',
                '0x0', str(pio/'bootloader.bin'), '0x8000', str(pio/'partitions.bin'),
                '0xe000', str(Path.home()/'.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin'),
                '0x10000', str(pio/'firmware.bin')], check=True)
for name in ['README.md', 'docs/PROTOCOL.md', 'docs/VALIDATION.md', 'docs/PROVENANCE.md', 'docs/RANGING_SETUP.md']:
    shutil.copyfile(ROOT/name, out/Path(name).name)
licenses = out/'licenses'
licenses.mkdir(exist_ok=True)
shutil.copytree(ROOT/'licenses', licenses, dirs_exist_ok=True)
for p in (ROOT/'type2dk/licenses').iterdir():
    if p.suffix == '.b64':
        (licenses/p.stem).write_bytes(base64.b64decode(p.read_text()))
    elif p.is_file():
        shutil.copyfile(p, licenses/p.name)
(licenses/'type2bp').mkdir(exist_ok=True)
for p in (ROOT/'type2bp/licenses').iterdir():
    if p.suffix == '.b64':
        (licenses/'type2bp'/p.stem).write_bytes(base64.b64decode(p.read_text()))
    elif p.is_file():
        shutil.copyfile(p, licenses/'type2bp'/p.name)
shutil.copyfile(ROOT/'config/dual_7bp.json', out/'dual_7bp.json')
info = {'version': '0.3.0', 'commit': a.sha, 'hardware_tested': False,
        'type2dk_mode': 'Two controllers, seven anchors 1111..7777; requires updated fixed firmware', 'central': 'CoreS3 PORT A RX2 + RX1',
        'files': {p.name: {'bytes': p.stat().st_size, 'sha256': hashlib.sha256(p.read_bytes()).hexdigest()}
                  for p in out.glob('*.bin')}}
(out/'build-info.json').write_text(json.dumps(info, indent=2)+'\n')
(out/'SHA256SUMS.txt').write_text(''.join(f'{v["sha256"]}  {k}\n' for k, v in info['files'].items()))
zip_path = out/'type2dk-uwb-uart2-0.3.0-ranging.zip'
with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED) as z:
    for p in sorted(out.rglob('*')):
        if p != zip_path and p.is_file():
            z.write(p, p.relative_to(out))
print(json.dumps(info, indent=2))
