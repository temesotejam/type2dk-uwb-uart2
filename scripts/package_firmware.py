#!/usr/bin/env python3
"""Produce the CoreS3 v0.4.0 installer payload.

The proprietary-SDK UWB binaries are intentionally not reconstructed in CI; they are
built and verified separately from the user's locally supplied NXP/Murata SDKs.
"""
import argparse, hashlib, json, shutil, subprocess, sys, zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
ap=argparse.ArgumentParser();ap.add_argument('--sha',default='local');a=ap.parse_args()
out=ROOT/'build/distribution'
if out.exists(): shutil.rmtree(out)
out.mkdir(parents=True)
pio=ROOT/'.pio/build/cores3_dual'
tool=Path.home()/'.platformio/packages/tool-esptoolpy/esptool.py'
subprocess.run([sys.executable,str(tool),'--chip','esp32s3','merge_bin','-o',str(out/'cores3-dual-merged.bin'),
                '--flash_mode','dio','--flash_freq','80m','--flash_size','16MB',
                '0x0',str(pio/'bootloader.bin'),'0x8000',str(pio/'partitions.bin'),
                '0xe000',str(Path.home()/'.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin'),
                '0x10000',str(pio/'firmware.bin')],check=True)
for name in ['README.md','docs/PROTOCOL.md','docs/VALIDATION.md','docs/PROVENANCE.md','docs/RANGING_SETUP.md']:
    shutil.copyfile(ROOT/name,out/Path(name).name)
shutil.copyfile(ROOT/'config/legacy_8anchor.json',out/'legacy_8anchor.json')
core=out/'cores3-dual-merged.bin'
info={'version':'0.4.0','commit':a.sha,'hardware_tested':False,
      'type2dk_mode':'legacy-compatible 8 anchors; UWB BINs built separately from user-supplied vendor SDKs',
      'central':'CoreS3 PORT A GPIO2=A(0050), GPIO1=B(0051), anchors 1111..8888',
      'files':{core.name:{'bytes':core.stat().st_size,'sha256':hashlib.sha256(core.read_bytes()).hexdigest()}}}
(out/'build-info.json').write_text(json.dumps(info,indent=2)+'\n')
(out/'SHA256SUMS.txt').write_text(f"{info['files'][core.name]['sha256']}  {core.name}\n")
zip_path=out/'type2dk-uwb-uart2-0.4.0-cores3.zip'
with zipfile.ZipFile(zip_path,'w',zipfile.ZIP_DEFLATED) as z:
    for p in sorted(out.rglob('*')):
        if p!=zip_path and p.is_file(): z.write(p,p.relative_to(out))
print(json.dumps(info,indent=2))
