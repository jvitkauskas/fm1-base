#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR MPL-2.0
# Copyright (c) 2026 Julius Vitkauskas
"""Offline BLE suites. Never contacts USB, Bluetooth, devices or the SDK."""
import os
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]
os.chdir(ROOT)
out=ROOT/'build/open-seam-tests'
out.mkdir(parents=True,exist_ok=True)
s=os.environ.copy()
s['ASAN_OPTIONS']='detect_leaks=1:halt_on_error=1'
s['UBSAN_OPTIONS']='halt_on_error=1:print_stacktrace=1'
def run(*cmd):
    print('+ '+' '.join(map(str,cmd)),flush=True)
    subprocess.run(list(map(str,cmd)),check=True,env=s)
source=(ROOT/'device/plat_fm1.c').read_text()
a=source.index('static uint32_t midi_n[NPORTS]')
b=source.index("/* the keys' velocity",a)
(out/'midi_seam.inc').write_text(source[a:b])
macros=subprocess.check_output([os.environ.get('CC','cc'),'-dM','-E','-DFM1B_BLE_OPEN=1',
    '-include','stdint.h','-include','bsp/chip/fm1_flash.h','-'],input=b'').decode()
(out/'flash_ranges.inc').write_text('\n'.join(line for line in macros.splitlines() if line.startswith('#define FL_'))+'\n')
flags=['-std=c11' ,'-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',
       '-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie','-DFM1B_BLE_OPEN=1',f'-I{out}']
for name in ('ble_open_seam_test','ble_open_midi_test','ble_open_ranges_test'):
    run(os.environ.get('CC','cc'),*flags,'tests/'+name+'.c','-o',out/name)
    run(out/name)
run('python3','tests/ble_conn_update_bench_test.py')
run('bash','tests/run_ble_s1_tests.sh')
run('python3','tests/run_ble_s2_tests.py')
run('python3','tests/run_ble_s3_tests.py')
run('python3','tests/run_ble_s3_tests.py','--s31')
run('python3','tests/run_ble_s3_tests.py','--s31','--open-store')
print('All open BLE ASan/UBSan suites passed.',flush=True)
