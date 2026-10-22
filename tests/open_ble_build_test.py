#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR MPL-2.0
# Copyright (c) 2026 Julius Vitkauskas
"""Offline build policy tests: no compiler, radio, SDK or forbidden source read."""
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('fm1_build',ROOT/'tools/build.py')
b=importlib.util.module_from_spec(spec)
spec.loader.exec_module(b)
class BuildPolicy(unittest.TestCase):
    def test_selection(self):
        with tempfile.TemporaryDirectory(dir=ROOT/'build') as d:
            p=Path(d); c=p/'fm1.toml'
            c.write_text('name = "test"\nble = "open"\nble_central = true\n')
            with patch.object(sys,'argv',['build.py',d,'--host']):
                with self.assertRaisesRegex(SystemExit,'peripheral BLE-MIDI only'):
                    b.main()
            c.write_text('name = "test"\nble = true\nble_central = true\n')
            with patch.object(sys,'argv',['build.py',d,'--host','--open-ble']), patch.object(b,'generate'), patch.object(b,'build_host',return_value=0):
                self.assertEqual(b.main(),0)
                self.assertTrue(b.BLE_OPEN and b.BLE)
                self.assertFalse(b.CONF['ble_central'])
                self.assertEqual(b.STEM,'test-open')
            with patch.object(sys,'argv',['build.py',d,'--host','--open-ble','--ble-diag','off']), patch.object(b,'generate'), patch.object(b,'build_host',return_value=0):
                self.assertEqual(b.main(),0)
                self.assertIs(b.CONF['ble_diag'],False)
            with patch.object(sys,'argv',['build.py',d,'--host','--no-ble']), patch.object(b,'generate'), patch.object(b,'build_host',return_value=0):
                self.assertEqual(b.main(),0)
                self.assertFalse(b.BLE_OPEN or b.BLE)
            self.assertIs(b.read_conf(ROOT/'template/fm1.toml')['ble'],True)
    def test_diagnostics_selection(self):
        b.APP=ROOT/'template'; b.ROOT=ROOT; b.OUT=ROOT/'template/build'
        b.CONF={'ble':'open','ble_diag':False}
        units,_,libs=b.open_ble_units()
        self.assertFalse(libs)
        self.assertTrue(all('-DFM1B_BLE_DIAG=0' in u for u in units))
        b.CONF={'ble':'open'}
        units,_,_=b.open_ble_units()
        self.assertTrue(all('-DFM1B_BLE_DIAG=1' in u for u in units))
        version=b.VERSION
        try:
            b.VERSION='2.3'
            units,_,_=b.open_ble_units()
            self.assertTrue(all('-DAPP_VERSION="2.3"' in u for u in units))
        finally: b.VERSION=version
    def test_mmio_boundary(self):
        b.APP=ROOT/'template'; b.ROOT=ROOT; b.CONF={}
        original=Path.read_text
        def allowed(path,*args,**kwargs):
            if path==ROOT/'device/radio.c':
                raise AssertionError('open/no-radio audit read the excluded adapter')
            return original(path,*args,**kwargs)
        with patch.object(Path,'read_text',allowed):
            b.BLE_OPEN=True
            self.assertEqual(b.mmio_check(),[])
            b.BLE_OPEN=b.BLE=False
            self.assertEqual(b.mmio_check(),[])
        # New application code remains checked even with open radio support.
        p=ROOT/'build/mmio-policy-app'; p.mkdir(exist_ok=True)
        source=p/'bad.c'; source.write_text('int x(void) { return *(volatile int *)0x10400u; }\n')
        try:
            b.APP=p
            errors=b.mmio_check()
            self.assertTrue(any('bad.c' in e and 'outside the BSP' in e for e in errors))
        finally: source.unlink(); p.rmdir()
    def test_vendor_link_rejected(self):
        b.APP=ROOT/'template'; b.ROOT=ROOT; b.CONF={'ble':'open'}; b.BLE=b.BLE_OPEN=True; b.WIFI=False
        b.OUT=ROOT/'template/build'; b.STEM='template-open'
        syms=(b.OUT/'template-open.symbols').read_text()
        binary=(b.OUT/'template-open.bin').read_bytes()
        # Exercise the real policy with an archive injected into the map text.
        actual=Path.read_text
        def read(path,*a,**k):
            text=actual(path,*a,**k)
            return text+'\nLOAD forbidden.a\n' if str(path).endswith('template-open.map') else text
        with patch.object(Path,'read_text',read):
            errors,_=b.check(binary,syms,'','')
        self.assertTrue(any('forbidden inputs' in e for e in errors))
if __name__=='__main__': unittest.main()
