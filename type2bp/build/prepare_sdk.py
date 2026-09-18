#!/usr/bin/env python3
"""Prepare an external SR150 04.08.01 SDK using the user's Murata patch."""
import argparse, hashlib, subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('sdk',type=Path);p.add_argument('--patch',type=Path,required=True);a=p.parse_args()
s=a.sdk.resolve()
marker=s/'.type2bp_dual_patch'
digest=hashlib.sha256(a.patch.read_bytes()).hexdigest()
if not marker.exists():
    subprocess.run(['patch','--batch','--forward','-p0','-d',str(s),'-i',str(a.patch.resolve())],check=True)
    marker.write_text(digest)
elif marker.read_text()!=digest:raise ValueError('Different vendor patch already applied')
def replace(path,old,new):
    f=s/path;t=f.read_text()
    if new in t:return
    if t.count(old)!=1:raise ValueError('Unexpected SDK contents: '+path)
    f.write_text(t.replace(old,new))
replace('project/RhodesV4_SE/uwb_iot_ftr.h','#define UWBFTR_SE_SE051W 1','#define UWBFTR_SE_SE051W 0')
replace('demos/common/Standalone_Main_qn9090.c','    ACCEL_Configure();','#if !defined(ANCHOR_BUILD)\n    ACCEL_Configure();\n#endif')
replace('boards/Host/Rhodes4/FreeRTOSConfig.h','((size_t)(45 * 1024))','((size_t)(48 * 1024))')
replace('boards/Host/Rhodes4/uwb_board.h',
        '#define UWB_BOARD_RX_ANTENNA_CONFIG_MODE_VAL UWB_BOARD_RX_ANTENNA_CONFIG_MODE_3DAOA',
        '#define UWB_BOARD_RX_ANTENNA_CONFIG_MODE_VAL UWB_BOARD_RX_ANTENNA_CONFIG_MODE_TOF /* distance only */')
print('Type2BP vendor patch applied, SE051W and accelerometer disabled')
