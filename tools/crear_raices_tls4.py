#!/usr/bin/env python3
"""Embed a user-reviewed PEM CA bundle as a C byte array, strictly offline.
The bundle is supplied by the user/OS. This does NOT fetch or trust unverified CAs.
"""
from pathlib import Path
import sys
if len(sys.argv)!=3:
    raise SystemExit('Uso: python3 tools/crear_raices_tls4.py CA-bundle.pem kernel/mios_tls4_roots.inc')
src=Path(sys.argv[1]); dst=Path(sys.argv[2]);
if dst.exists(): raise SystemExit('ERROR: salida ya existe; no sobrescribo')
raw=src.read_bytes()
if len(raw)<500 or len(raw)>400000 or b'-----BEGIN CERTIFICATE-----' not in raw:
    raise SystemExit('ERROR: archivo PEM de CA vacio, enorme o no valido')
raw=raw.rstrip(b'\n')+b'\n\x00'
out=['/* CA bundle imported explicitly by the user. Review/update regularly. */',
     'static const unsigned char mios_tls4_ca_pem[] = {']
for offset in range(0,len(raw),16):
    out.append('    '+', '.join('0x%02x'%b for b in raw[offset:offset+16])+',')
out.append('};')
dst.parent.mkdir(parents=True,exist_ok=True)
dst.write_text('\n'.join(out)+'\n')
print('CA PEM embebido:',len(raw)-1,'bytes (mas NUL)')
print('Salida:',dst)
