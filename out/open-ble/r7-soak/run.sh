#!/bin/bash
# R7 owner soak: fast reconnect cycling with 100 ms APTO pings and MIDI notes, btmon recording.
#   run.sh health   - 20 min on one link, then a readout (CPU/audio check before the long run)
#   run.sh cycle    - disconnect/reconnect every ~5 s until the first complete R7 record, mic failure or 12 h
# btmon.btsnoop contains the link key: it stays local (gitignored).
cd ~/fm1-base-open; . venv/bin/activate
A=FD:60:52:C0:DF:6C; D=out/open-ble/r7-soak
P=$(python3 -c "import mido;print([n for n in mido.get_output_names() if n.startswith('TEMPLATE FM-1:TEMPLATE FM-1 MIDI 1')][0])")
log() { echo "$(date +%T) $*" >> $D/events.txt; }
readout() { python3 tools/fm1_remote.py --port "$P" log > $D/$1.txt 2>&1; }
sample() {
  echo "$(date +%T) $(python3 tools/fm1_remote.py --port "$P" log | grep -E "^log: (sample_ms|mic_failures|link_epoch|supervision_fail|executor_item_over_100us|audio_late|audio_xruns|r7_background|r7_cost_ticks24|r7_record)" | sed "s/log: //" | tr "\n" " ")" >> $D/samples.txt
}
hit() { tail -1 $D/samples.txt | grep -qE "complete 00000001|mic_failures [1-9]"; }
wait_conn() {
  for i in $(seq 20); do bluetoothctl info $A | grep -q "Connected: yes" && return 0; sleep 0.5; done
  bluetoothctl connect $A >/dev/null 2>&1
  for i in $(seq 15); do bluetoothctl info $A | grep -q "Connected: yes" && return 0; sleep 1; done
  return 1
}
apto() { h=$(python3 out/open-ble/ping-r4/conhandle.py $A); sudo python3 out/open-ble/ping-r4/apto.py ${h:-0x0800} 10 >/dev/null 2>&1 || log "apto failed $h"; }
pgrep -x btmon >/dev/null || { sudo setsid btmon -w $D/btmon.btsnoop > /dev/null 2>&1 < /dev/null & sleep 1; }
log "start $1 port $P"
( while :; do aplaymidi -p 128:0 out/open-ble/soak-r4/notes.mid >/dev/null 2>&1; sleep 1; done ) & notes=$!
trap 'kill $notes 2>/dev/null; pkill aplaymidi' EXIT
readout before-$1
if [ "$1" = health ]; then
  wait_conn || log "connect FAILED"; apto
  for k in $(seq 20); do sleep 60; sample; hit && { log HIT; break; }; done
  readout after-health; log done; exit 0
fi
end=$((SECONDS+43200)); cycle=0
while [ $SECONDS -lt $end ]; do
  wait_conn || log "reconnect FAILED (cycle $cycle)"
  sleep 0.5; apto; sleep 4
  bluetoothctl disconnect $A >/dev/null 2>&1
  cycle=$((cycle+1))
  if [ $((cycle % 25)) = 0 ]; then
    sample; log "cycle $cycle"
    if hit; then log "HIT at cycle $cycle"; readout hit-1; sleep 5; readout hit-2; break; fi
  fi
done
readout final; log done
