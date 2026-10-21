#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR MPL-2.0
# Copyright (c) 2026 Julius Vitkauskas
import os,sys,subprocess
from pathlib import Path
os.environ['FELUCCA_BLE_STAGE']='3'
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import nimble_build as nb
os.chdir(nb.ROOT)
s31='--s31' in sys.argv
objects='--open-store' in sys.argv
out=nb.ROOT/('build/open-tests' if objects else 'build/s31-tests' if s31 else 'build/s3-tests');out.mkdir(exist_ok=True,parents=True)
cc=os.environ.get('CC','cc')
common=['-DBLE_HOST_TEST=1','-DFELUCCA_BLE_STAGE=3','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-ffunction-sections','-fdata-sections','-fno-builtin-memset','-fno-builtin-memcpy','-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie']
if s31: common+=['-DFELUCCA_BLE_S31=1']
if objects: common+=['-DFM1B_BLE_OPEN=1','-Wno-unused-function']
def compile(p,flags=()):
    obj=out/(str(p).replace('/','_')+'.o')
    subprocess.run([cc,*common,*flags,'-c',str(p),'-o',str(obj)],check=True)
    return obj
def run(name,objects,args=()):
    exe=out/name
    subprocess.run([cc,'-no-pie','-fsanitize=address,undefined','-Wl,--gc-sections',
                    '-Wl,--wrap=tc_sha256_update,--wrap=tc_sha256_final,--wrap=memset,--wrap=memcpy,--wrap=ble_secure_zero',*map(str,objects),'-o',str(exe)],check=True)
    subprocess.run([str(exe),*args],check=True)
base=[compile(Path('open_ble')/(n+'.c'),nb.flags(vendor=False)) for n in ('ble_logic','ble_controller','ble_link_logic','ble_link','ble_midi','ble_heap','ble_crypto','ble_seed','ble_bonds','ble_timing','ble_privacy')]
if objects: base+=[compile(Path('open_ble/ble_store.c'),nb.flags(vendor=False))]
sim=[compile(Path('tests')/(n+'.c'),nb.flags(vendor=False)) for n in ('ble_s2_sim','ble_s3_sim')]
if objects: sim+=[compile(Path('tests/ble_open_sim_store.c'),nb.flags(vendor=False))]
vendor=[compile(p,[*nb.flags(),"-Wno-old-style-declaration",*(['-fno-sanitize=shift-base'] if p.name=='aes_encrypt.c' else [])]) for p in nb.sources()]
port=[compile(Path('open_ble')/(n+'.c'),nb.flags(vendor=False)) for n in ('ble_npl','ble_host')]
if not objects: run('logic',[*base,*sim,*vendor,*port,compile(Path('tests/ble_s3_test.c'),nb.flags(vendor=False))])
run('s2-regression',[*base,*sim,*vendor,*port,compile(Path('tests/ble_s2_integration.c'),nb.flags(vendor=False))])
run('connection-updates', [*base,*sim,*vendor,*port,compile(Path('tests/ble_conn_update_test.c'),nb.flags(vendor=False))])
integration=compile(Path('tests/ble_s3_integration.c'),nb.flags(vendor=False))
if s31 and not objects:
    run('privacy-store-timing', [*base,*sim,*vendor,*port,compile(Path('tests/ble_s31_test.c'),nb.flags(vendor=False))])
if s31:
    run('resumable-steps', [*base,*sim,*vendor,*port,compile(Path('tests/ble_s31_steps.c'),nb.flags(vendor=False))])
if objects: run('object-store', [*base,*sim,*vendor,*port,compile(Path('tests/ble_open_store_test.c'),nb.flags(vendor=False))])
for mode in (('legacy','sc','rpa','repeat-legacy','repeat-sc') if s31 else ('legacy','sc')):
    run('pairing-'+mode,[*base,*sim,*vendor,*port,integration],[mode])
