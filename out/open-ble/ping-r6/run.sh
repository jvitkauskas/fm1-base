#!/bin/bash
# R6 owner-only adaptation of ../ping-r4/run.sh; no automatic reconnect.
# Uses the unchanged R4 APTO helper. Never run by the implementer.
set -euo pipefail
: "${FM1_USB_PORT:?exact USB-MIDI port name required}"
: "${FM1_BLE_MIDI_PORT:?ALSA BLE-MIDI destination required}"
: "${FM1_ADDRESS:?FM-1 Bluetooth address required}"
: "${FM1_HCI_HANDLE:?current hci0 ACL handle required, for example 0x0800}"
D=out/open-ble/ping-r6
mkdir -p "$D"
if test -e "$D/before-log.txt"; then
    echo 'Save/remove the prior ping-r6 readouts before another run.' >&2
    exit 1
fi
P="$FM1_USB_PORT"
python3 tools/fm1_remote.py --port "$P" log > "$D/before-log.txt"
python3 tools/fm1_remote.py --port "$P" ble > "$D/before-ble.txt"
sudo python3 out/open-ble/ping-r4/apto.py "$FM1_HCI_HANDLE" 10 | tee "$D/apto-status.txt"
if ! rg -q '^status 0x0$' "$D/apto-status.txt"; then exit 1; fi
end=$((SECONDS+1200))
( while [ "$SECONDS" -lt "$end" ]; do
    aplaymidi -p "$FM1_BLE_MIDI_PORT" out/open-ble/soak-r4/notes.mid
    sleep 4
  done ) > "$D/midi.txt" 2>&1 &
FM1_R6_MIDI_PID=$!
trap 'kill "$FM1_R6_MIDI_PID" 2>/dev/null || true' EXIT
while [ "$SECONDS" -lt "$end" ]; do
    if ! bluetoothctl info "$FM1_ADDRESS" | rg -q 'Connected: yes'; then
        date -Is >> "$D/events.txt"
        echo 'DISCONNECTED: stop, preserve first failure; no reconnect' >> "$D/events.txt"
        python3 tools/fm1_remote.py --port "$P" log > "$D/drop-log.txt"
        python3 tools/fm1_remote.py --port "$P" ble > "$D/drop-ble.txt"
        exit 1
    fi
    date -Is >> "$D/samples.txt"
    python3 tools/fm1_remote.py --port "$P" ble >> "$D/samples.txt"
    sleep 30
done
wait "$FM1_R6_MIDI_PID"
python3 tools/fm1_remote.py --port "$P" log > "$D/final-log.txt"
python3 tools/fm1_remote.py --port "$P" ble > "$D/final-ble.txt"
echo 'completed 20 minutes; compare counters and btmon before declaring PASS' >> "$D/events.txt"
