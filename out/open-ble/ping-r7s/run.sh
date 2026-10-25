#!/bin/bash
# R7 sniffer run: LE Ping stress (APTO 100 ms + MIDI notes every ~1 s) with the nRF sniffer following the FM-1
# and btmon recording. Every 5 min: new sniffer segment + forced reconnect. Stops at the first FM-1 MIC failure
# (two readouts, sniffer kept 5 s longer) or after 12 h. Sniffer pcaps and btmon contain keys: desktop only.
cd ~/fm1-base-open; . venv/bin/activate
P=$(python3 -c "import mido;print([n for n in mido.get_output_names() if n.startswith('TEMPLATE FM-1:TEMPLATE FM-1 MIDI 1')][0])"); A=FD:60:52:C0:DF:6C
D=out/open-ble/ping-r7s; S=~/fm1-sniff/r7s
pgrep -x btmon >/dev/null || { sudo setsid btmon -w $D/btmon.btsnoop >/dev/null 2>&1 </dev/null & sleep 1; }
log() { echo "$(date +%T) $*" >> $D/events.txt; }
sniff_restart() {
  pkill -INT -f "nrfutil-ble-sniffer sniff"; for i in $(seq 20); do pgrep -f "nrfutil-ble-sniffer sniff" >/dev/null || break; sleep 0.5; done
  pkill -KILL -f "nrfutil-ble-sniffer sniff" 2>/dev/null
  seg=$S/seg-$(date +%H%M%S).pcap
  setsid ~/fm1-sniff/sniff.sh $seg > $seg.log 2>&1 < /dev/null &
  sleep 3; log "sniffer segment $(basename $seg)"
}
wait_conn() {
  for i in $(seq 30); do bluetoothctl info $A | grep -q "Connected: yes" && return 0; sleep 1; done
  bluetoothctl connect $A >/dev/null 2>&1
  for i in $(seq 15); do bluetoothctl info $A | grep -q "Connected: yes" && return 0; sleep 1; done
  return 1
}
apto() { h=$(python3 out/open-ble/ping-r4/conhandle.py $A); log "apto handle $h $(sudo python3 out/open-ble/ping-r4/apto.py ${h:-0x0800} 10 2>&1)"; }
follow_new_link() {   # restart the sniffer, force a reconnect, retry until the sniffer follows it
  for t in 1 2 3 4; do
    sniff_restart
    bluetoothctl disconnect $A >/dev/null 2>&1; log "disconnect $1 try $t"; sleep 2
    wait_conn || log "reconnect FAILED"
    sleep 5; cp $seg /tmp/r4L-check.pcap
    n=$(tshark -r /tmp/r4L-check.pcap -Y btle.data_header 2>/dev/null | wc -l)
    log "sniffer data packets after reconnect: $n"
    [ "$n" -gt 50 ] && break
  done
  apto
}
sample() { echo "$(date +%T) $(python3 tools/fm1_remote.py --port "$P" log | grep -E "^log: (sample_ms|mic_failures|mic_records|link_epoch|supervision_fail|executor_kind control_rx)" | sort -u | sed "s/log: //" | tr "\n" " ")" >> $D/samples.txt; }
end=$((SECONDS+43200))
( while [ $SECONDS -lt $end ]; do aplaymidi -p 128:0 out/open-ble/soak-r4/notes.mid >/dev/null 2>&1; sleep 1; done ) & notes=$!
python3 tools/fm1_remote.py --port "$P" log > $D/before-log.txt
cycle=0
while [ $SECONDS -lt $end ]; do
  follow_new_link "forced (cycle $cycle)"
  for k in 1 2 3 4 5; do
    sleep 60
    if ! bluetoothctl info $A | grep -q "Connected: yes"; then
      log "UNPLANNED DISCONNECT"; python3 tools/fm1_remote.py --port "$P" log > $D/drop-$(date +%H%M%S).txt
      follow_new_link "after unplanned drop"
    fi
    sample
    if tail -1 $D/samples.txt | grep -qE "mic_failures [1-9]"; then log HIT; python3 tools/fm1_remote.py --port "$P" log > $D/hit-1.txt; sleep 5; python3 tools/fm1_remote.py --port "$P" log > $D/hit-2.txt; break 2; fi
  done
  cycle=$((cycle+1))
done
kill $notes 2>/dev/null; pkill aplaymidi
python3 tools/fm1_remote.py --port "$P" log > $D/final-log.txt
sleep 5; pkill -INT -f "nrfutil-ble-sniffer sniff"
log done
