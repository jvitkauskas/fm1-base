#!/bin/bash
# 20-minute LE Ping stress: APTO 100 ms, MIDI notes as in the soak, sample FM-1 every 30 s.
cd ~/fm1-base-open; . venv/bin/activate
P="TEMPLATE FM-1:TEMPLATE FM-1 MIDI 1 16:0"; A=FD:60:52:C0:DF:6C; D=out/open-ble/ping-r4
end=$((SECONDS+1200))
( while [ $SECONDS -lt $end ]; do aplaymidi -p 128:0 out/open-ble/soak-r4/notes.mid >/dev/null 2>&1; sleep 4; done ) &
n=0
while [ $SECONDS -lt $end ]; do
  if ! bluetoothctl info $A | grep -q "Connected: yes"; then
     echo "$(date +%T) DISCONNECTED, reconnecting" >> $D/events.txt
     python3 tools/fm1_remote.py --port "$P" log > $D/drop-$(date +%H%M%S).txt
     bluetoothctl connect $A >/dev/null 2>&1; sleep 3
  fi
  st=$(sudo python3 $D/apto.py 0x0800 10 2>&1)
  n=$((n+1))
  echo "$(date +%T) apto $st $(python3 tools/fm1_remote.py --port "$P" log | grep -E "^log: (mic_failures|mic_records|link_epoch|supervision_fail|executor_kind control_rx|enc_tx_counter_lo|enc_rx_counter_lo|conn_interval_1250us)" | sort -u | sed "s/log: //" | tr "\n" " ")" >> $D/samples.txt
  sleep 30
done
wait
python3 tools/fm1_remote.py --port "$P" log > $D/final-log.txt
echo done >> $D/events.txt
