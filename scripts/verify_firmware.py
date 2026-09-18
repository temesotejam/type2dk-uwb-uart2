#!/usr/bin/env python3
"""Validate QN9090 vectors, header CRC and ROM boot trailer independently."""
import binascii, hashlib, struct

def verify(data):
    h=struct.unpack_from('<11I',data)
    assert sum(h[:8])&0xffffffff==0, 'vector checksum'
    assert h[8]==0x98447902, 'QN9090 marker'
    assert binascii.crc32(data[:40])&0xffffffff==h[10], 'header CRC'
    assert 0x04000400<=h[0]<0x04016000 and h[0]%8==0, 'stack pointer'
    assert h[1]&1 and h[1]&~1<h[9], 'reset vector'
    assert h[9]+32==len(data), 'trailer position'
    boot=struct.unpack_from('<8I',data,h[9])
    assert boot[0]==0xbb0110bb and boot[1]==boot[2]==0, 'load address'
    assert boot[3]==len(data) and len(data)<=boot[4]<=0x9e000 and boot[4]%8192==0, 'image size'
    assert boot[5]==boot[6]==0, 'unsigned image trailer'
    return {'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),
            'vector_checksum':'PASS','header_crc':'PASS','boot_block':'PASS',
            'load_address':0,'stated_image_size':boot[4]}

if __name__=='__main__':
    import argparse,base64,json
    from pathlib import Path
    p=argparse.ArgumentParser();p.add_argument('manifest',type=Path);a=p.parse_args()
    meta=json.loads(a.manifest.read_text())
    for name,info in meta['files'].items():
        data=base64.b64decode(''.join((a.manifest.parent/(name+'.b64')).read_text().split()),validate=True)
        result=verify(data)
        assert result['sha256']==info['sha256'] and result['bytes']==info['bytes'],name
        print(name, result['bytes'],'PASS')
