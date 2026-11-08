#!/usr/bin/env python3
"""Local tests; no user apps connected and no disk modifications."""
from pathlib import Path
import subprocess, shutil, tempfile
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as td:
    exe=str(Path(td)/'tls4_test')
    for args in [
      ['gcc','-std=c11','-O0','-Wall','-Wextra',str(root/'tests/mock_tls4.c'),'-o',exe],
      [exe],
      ['gcc','-m32','-ffreestanding','-fno-pie','-O0','-Wall','-Wextra',
       '-fsyntax-only','-DTLS4_FREESTANDING_CHECK',str(root/'tests/mock_tls4.c')],
    ]:
      print('COMPROBANDO:', ' '.join(args[:5]))
      subprocess.run(args,check=True)
print('TLS4.1/4.2: PRUEBAS HOST Y COMPILACION -m32 OK')
print('TLS4.3/4.4/4.5: ESTRUCTURA DE CODIGO ENTREGADA, NO ENLAZADA EN MIOS')
