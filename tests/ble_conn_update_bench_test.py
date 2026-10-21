#!/usr/bin/env python3
# Copyright (c) 2026 Julius Vitkauskas
# SPDX-License-Identifier: MIT OR MPL-2.0
"""Offline bench protocol/counter fixtures. Opens no radio, console or device."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('bench', ROOT/'tools/ble_conn_update_bench.py')
bench = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bench)


class Radio:
    def sendall(self, payload):
        assert payload == bytes.fromhex('01 13 20 0e 00 08 06 00 06 00 00 00 f4 01 00 00 00 00')


class Fixtures(unittest.TestCase):
    def test_hci(self):
        events = [(15, bytes.fromhex('00 01 13 20')), (62, bytes.fromhex('03 00 00 08 06 00 00 00 f4 01'))]
        with patch.object(bench.select, 'select', return_value=([], [], [])), \
                patch.object(bench, 'next_event', side_effect=events):
            bench.update(Radio(), 0x800, 6)
        with patch.object(bench.select, 'select', return_value=([], [], [])), \
                patch.object(bench, 'next_event', return_value=(15, bytes.fromhex('0c 01 13 20'))):
            with self.assertRaisesRegex(RuntimeError, 'Command Status 0c'):
                bench.update(Radio(), 0x800, 6)

    def test_counter_measurement(self):
        a = dict(link_epoch=2, conn_interval_1250us=12, conn_updates=1, pending_kind=0,
                 conn_event=65500, conn_events=65500, supervision_fail=0, late_instants=0,
                 link_state_idle_created_up_closing_stopped=2, enc_enabled=1)
        b = dict(a, conn_event=(65500+1000) & 65535, conn_events=66500)
        class Console:
            def sample(self):
                return 25, b
        with patch.object(bench, 'stable', return_value=(10, a)), patch.object(bench.time, 'sleep'):
            self.assertEqual(bench.measure(Console(), 12, 15, 'fixture wrap')['measured_ms'], 15)
            for change in (dict(conn_event=65500), dict(conn_event=5000), dict(link_epoch=4),
                           dict(pending_kind=1), dict(enc_enabled=0)):
                saved = dict(b)
                b.update(change)
                with self.assertRaises(RuntimeError):
                    bench.measure(Console(), 12, 15, 'fixture must fail')
                b.clear()
                b.update(saved)

    def test_console_parser(self):
        raw = b'ble\r\nconn_event 65535\r\nworker_timer_last_ticks 0xF403A1CD\r\npending_proc kind 0 instant 0\r\nvco_limits 47 47\r\n> '
        with tempfile.TemporaryDirectory(dir=ROOT/'build') as directory:
            console = bench.Console.__new__(bench.Console)
            console.fd, console.directory, console.sequence = 123, Path(directory), 0
            with patch.object(bench.termios, 'tcflush'), patch.object(bench.os, 'write'), \
                    patch.object(bench.os, 'read', return_value=raw), \
                    patch.object(bench.select, 'select', return_value=([123], [], [])), \
                    patch.object(bench.time, 'monotonic', side_effect=(1, 1, 1.1)):
                stamp, values = console.sample()
            self.assertAlmostEqual(stamp, 1.05)
            self.assertEqual(values['conn_event'], 65535)
            self.assertEqual(values['worker_timer_last_ticks'], 0xF403A1CD)
            self.assertEqual(values['pending_kind'], 0)
            self.assertTrue((Path(directory)/'console-001.txt').is_file())


if __name__ == '__main__':
    unittest.main()
