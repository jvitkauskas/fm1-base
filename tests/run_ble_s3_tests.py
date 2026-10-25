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
release='--no-diag' in sys.argv
mic='--mic-stress' in sys.argv
reliability='--reliability' in sys.argv
assert not reliability or mic or (s31 and objects)
out=nb.ROOT/('build/reliability-tests' if reliability and mic else 'build/reliability-store-tests' if reliability else 'build/mic-tests' if mic else 'build/release-tests' if release else 'build/open-tests' if objects else 'build/s31-tests' if s31 else 'build/s3-tests');out.mkdir(exist_ok=True,parents=True)
cc=os.environ.get('CC','cc')
common=['-DBLE_HOST_TEST=1','-DFELUCCA_BLE_STAGE=3','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-ffunction-sections','-fdata-sections','-fno-builtin-memset','-fno-builtin-memcpy','-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie']
common+=['-DFM1B_BLE_RELIABILITY='+('1' if reliability else '0')]
if release: common+=['-DFM1B_BLE_DIAG=0']
if s31: common+=['-DFELUCCA_BLE_S31=1']
if objects: common+=['-DFM1B_BLE_OPEN=1','-Wno-unused-function']
if mic:
    common+=['-DBLE_MIC_RADIO=1',f'-I{out}']
    # Compile the production S3 HAL RX/refill functions verbatim on a virtual
    # register bus, rather than a second implementation of their ordering.
    import re
    hal=(nb.ROOT/'open_ble/hal/fm1_ble.h').read_text()
    def extract(name):
        m=re.search(r'^(?:static )?(?:void|int) '+name+r'\([^;]+?\)\n\{',hal,re.M)
        assert m,name
        end=m.end(); depth=1
        while depth:
            if hal[end]=='{': depth+=1
            if hal[end]=='}': depth-=1
            end+=1
        return hal[m.start():end]+'\n'
    (out/'hal_mic_seam.inc').write_text(extract('bb_refill_limit')+
        'static void bb_refill(void) { bb_refill_limit(2); }\n'+extract('bb_connected_rx')+ ('\n#if FM1B_BLE_RELIABILITY\n'+extract('ble_port_diag_rx_read')+extract('ble_port_diag_rx_release')+'#endif\n' if reliability else ''))
    diag=(nb.ROOT/'device/ble_open_diag.c').read_text()
    (out/'mic_dump_seam.inc').write_text(diag[diag.index('static void open_ble_mic_dump(void)'):
        diag.index('\nstatic void open_ble_events_dump')])
def compile(p,flags=()):
    obj=out/(str(p).replace('/','_')+'.o')
    subprocess.run([cc,*common,*flags,'-c',str(p),'-o',str(obj)],check=True)
    return obj
def run(name,objects,args=(),extra_ldflags=()):
    exe=out/name
    subprocess.run([cc,'-no-pie','-fsanitize=address,undefined','-Wl,--gc-sections',
                    '-Wl,--wrap=tc_sha256_update,--wrap=tc_sha256_final,--wrap=memset,--wrap=memcpy,--wrap=ble_secure_zero',
                    *(['-Wl,--wrap=ble_ccm,--wrap=tc_aes_encrypt,--wrap=tc_aes128_set_encrypt_key'] if reliability and mic else []),*extra_ldflags,*map(str,objects),'-o',str(exe)],check=True)
    subprocess.run([str(exe),*args],check=True)
base=[compile(Path('open_ble')/(n+'.c'),nb.flags(vendor=False)) for n in ('ble_logic','ble_controller','ble_link_logic','ble_link','ble_midi','ble_heap','ble_crypto','ble_seed','ble_bonds','ble_timing','ble_privacy')]
if objects:
    base+=[compile(Path('open_ble/ble_store.c'),nb.flags(vendor=False))]
    old=out/'r4-store'; old.mkdir(exist_ok=True)
    for filename in ('ble_store.c','ble_store.h'):
        (old/filename).write_bytes(subprocess.check_output(['git','show','208ebfb:open_ble/'+filename]))
    names=('init','seed_load','seed_save','bond_load','bonds_save')
    base+=[compile(old/'ble_store.c',[*nb.flags(vendor=False),
        *['-Dble_object_'+n+'=r4_ble_object_'+n for n in names]])]
sim=[compile(Path('tests')/(n+'.c'),nb.flags(vendor=False)) for n in ('ble_s2_sim','ble_s3_sim')]
if objects: sim+=[compile(Path('tests/ble_open_sim_store.c'),nb.flags(vendor=False))]
vendor=[compile(p,[*nb.flags(),"-Wno-old-style-declaration",*(['-fno-sanitize=shift-base'] if p.name=='aes_encrypt.c' else [])]) for p in nb.sources()]
port=[compile(Path('open_ble')/(n+'.c'),nb.flags(vendor=False)) for n in ('ble_npl','ble_host')]
if mic:
    run('mic-stress',[*base,*sim,*vendor,*port,compile(Path('tests/ble_mic_stress.c'),nb.flags(vendor=False))],
        [os.environ.get('BLE_MIC_EVENTS','10000000')])
    subprocess.run([str(out/'mic-stress'),'--trace-timeout'],check=True)
    if reliability:
        for case in ('counter-2','counter-1','counter1','counter2','counter-zero','counter-max',
                     'direction','previous-key','previous-context','copy-byte','dma-byte',
                     'aes-transient','aes-persistent','key-byte','derive-block','tx-block','phase','background','tx-toggle','audio-preempt','old-command','kat-under-load'):
            subprocess.run([str(out/'mic-stress'),'--r7',case],check=True)
    sys.exit(0)
if '--capacity-only' in sys.argv:
    assert objects and s31
    run('pairing-f29', [*base,*sim,*vendor,*port,compile(Path('tests/ble_s3_integration.c'),nb.flags(vendor=False))],['f29'])
    sys.exit(0)
run('nimble110-crypto', [*base,*sim,*vendor,*port,compile(Path('tests/ble_nimble110_crypto.c'),nb.flags(vendor=False))])
run('nimble110-gap', [*base,*sim,*vendor,*port,compile(Path('tests/ble_nimble110_gap.c'),nb.flags(vendor=False))],
    extra_ldflags=['-Wl,--wrap=ble_hs_hci_cmd_tx'])
if not objects and not release: run('logic' ,[*base,*sim,*vendor,*port,compile(Path('tests/ble_s3_test.c'),nb.flags(vendor=False))])
run('s2-regression',[*base,*sim,*vendor,*port,compile(Path('tests/ble_s2_integration.c'),nb.flags(vendor=False))])
if not release: run('connection-updates', [*base,*sim,*vendor,*port,compile(Path('tests/ble_conn_update_test.c'),nb.flags(vendor=False))])
integration=compile(Path('tests/ble_s3_integration.c'),nb.flags(vendor=False))
if s31 and not objects and not release:
    run('privacy-store-timing', [*base,*sim,*vendor,*port,compile(Path('tests/ble_s31_test.c'),nb.flags(vendor=False))])
if s31 and not release:
    run('resumable-steps', [*base,*sim,*vendor,*port,compile(Path('tests/ble_s31_steps.c'),nb.flags(vendor=False))])
if objects: run('object-store', [*base,*sim,*vendor,*port,compile(Path('tests/ble_open_store_test.c'),nb.flags(vendor=False))])
for mode in ((('f29',) if objects and s31 else ())+('legacy','sc','rpa','repeat-legacy','repeat-sc') if s31 else ('legacy','sc')):
    run('pairing-'+mode,[*base,*sim,*vendor,*port,integration],[mode])
