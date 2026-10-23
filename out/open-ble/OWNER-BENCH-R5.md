# Owner bench R5 — F-29

Run after the R4 soak. Image: `template/build/template-open-r5.fwsc`
(diagnostics on). Keep other centrals' Bluetooth off during each session.
No Forget/remove/re-pair on a retained bond until its expected eviction test.
Save each readout, with the central name and step number:

```sh
python3 tools/fm1_remote.py --port "$FM1_USB_PORT" ble
python3 tools/fm1_remote.py --port "$FM1_USB_PORT" log
```

After **every** session check encrypted + bonded + MIDI subscribed, MIDI in
both directions, identity/rank and CCCDs; after disconnect wait for
`bond_pending 0`, no bond failures. Record `bond_count/capacity`,
`bond_replacements`, `bond_writes/erases`, `rl_entries/capacity`,
`ltk_negative`, `mic_failures`, `executor_item_over_100us`.
Rank **1** is newest. CCCD rows use value handles: `0008=0002` Service Changed,
`0010=0001` MIDI; absent `0000=0000` means that CCCD wasn't stored. Check the
CCCDs each central actually enables; keep their values across reconnects.

1. **Upgrade first:** record R4's retained identities before loading R5. Load R5
   without clearing storage. Reconnect those identities without a prompt;
   check their keys work and CCCDs restore. R5 must show capacity 8, RL capacity
   10. A normal boot advances the seed once; don't count that erase as churn.
2. Label the real centrals **A** BlueZ original identity, **B** Mac, **C/D** the
   iPhones, **E** Android. Pair missing ones once, with MIDI subscriptions;
   disconnect/flush each. Record actual addresses. Do not infer identity from
   an RPA or from an adapter's display name.

## Extra desktop identities, with retained BlueZ keys

Use the Intel adapter (`hci0` below; change if needed). Static addresses must
be random-static: the first displayed octet has its top two bits set; use the
four addresses below. LE-only mode is needed on adapters with a public address.
Disable privacy so these identities are easy to recognize. **Separate BlueZ
state per identity** prevents pairing another identity from overwriting its
central-side key. This temporarily stops other desktop Bluetooth use.

Record the original settings with `sudo btmgmt -i hci0 info`. In one terminal:

```sh
R5_STATE=$(mktemp -d "$HOME/fm1-r5-bluez.XXXXXX")
sudo systemctl stop bluetooth
sudo cp -a /etc/bluetooth/main.conf "$R5_STATE/main.conf"
active=original
# Edit [General]: ControllerMode = le and Privacy = off (replace existing
# settings; don't create duplicate keys). The original file is saved above.
sudoedit /etc/bluetooth/main.conf

use_id() {
    sudo systemctl stop bluetooth || return
    sudo mv -T /var/lib/bluetooth "$R5_STATE/$active" || return
    if sudo test -d "$R5_STATE/$1"; then
        sudo mv -T "$R5_STATE/$1" /var/lib/bluetooth || return
    else
        sudo install -d -m 700 /var/lib/bluetooth || return
    fi
    active=$1
    sudo btmgmt -i hci0 power off || return
    sudo btmgmt -i hci0 bredr off || return
    sudo btmgmt -i hci0 le on || return
    sudo btmgmt -i hci0 privacy off || return
    sudo btmgmt -i hci0 static-addr "$2" || return
    sudo btmgmt -i hci0 power on || return
    sudo systemctl start bluetooth
}
use_id F C2:46:4D:31:00:06
```

The required order is **power off → static-addr → power on → pair**. Then:

```text
bluetoothctl
agent NoInputNoOutput
default-agent
scan on
# Find FM-1 Open BLE; substitute its address below.
pair FM1_ADDRESS
trust FM1_ADDRESS
connect FM1_ADDRESS
scan off
```

Open its BLE-MIDI port/app, play MIDI both ways, then `disconnect FM1_ADDRESS`.
Quit `bluetoothctl` before the next `use_id`.
Wait for the FM-1 flush and save the readouts. It must add identity
`C2:46:4D:31:00:06`, **type 1**, newest rank, and its MIDI CCCD. This check is
mandatory: if it still uses the public identity or a setting is unsupported,
stop the capacity run and retain the error; don't count another connection as
another identity. These adapter/BlueZ steps have not been tested by the implementer.

Repeat with `use_id G C2:46:4D:31:00:07`, then
`use_id H C2:46:4D:31:00:08`; pair each once. A–H now make **8**. `use_id`
saves the previous identity's daemon state and restores it on later selection.
For A, use `use_id original 00:00:00:00:00:00` (clears the static override).
Power the desktop adapter off whenever another central is under test.

## Retention, order and replacement

3. Power-cycle **FM-1**, then reconnect A–H without pairing, in the exact order
   **D, B, H, A, F, C, G, E**. Use `use_id` for desktop identities. After every
   disconnect: flush complete, count 8, no replacement, that identity rank 1,
   same CCCDs, encrypted MIDI. Each non-MRU use may cause one object commit.
   With all eight peers distributing IRKs, `rl_entries` should be **9** (eight
   peers + local); capacity is 10. A peer without an IRK uses no RL entry.
4. Reconnect **E three more times**, disconnect/flush each. Take the write/erase
   baseline *after* the first E session above. Both counters must stay exactly
   equal through all three repeats; rank order and CCCDs stay equal too.
5. `use_id I C2:46:4D:31:00:09`; pair, subscribe, MIDI, disconnect/flush.
   Count stays **8**, replacement counter increases **exactly 1**, **D is absent**,
   I is rank 1. Check HCI trace: victim 2028; new peer 2027 then 204E.
6. Power-cycle FM-1 again. First attempt D with its old key: expect one negative
   LTK reply, no encrypted MIDI, and no other identity removed. Cancel any new
   pairing prompt and disconnect; don't pair D again yet. Reconnect **A, B, C, E, F, G, H, I** with existing keys: no prompt,
   both-way MIDI, CCCDs intact, count 8, unchanged replacement counter. Ranks
   persist before these reconnects change them. Check zero new MIC failures or
   executor items over 100 us, especially during late-list RPA resolutions.

## Restore the desktop

Disconnect FM-1; save the final readout. Still in the helper's terminal:

```sh
sudo systemctl stop bluetooth
sudo mv -T /var/lib/bluetooth "$R5_STATE/$active"
sudo mv -T "$R5_STATE/original" /var/lib/bluetooth
sudo btmgmt -i hci0 power off
sudo btmgmt -i hci0 static-addr 00:00:00:00:00:00
sudo cp -a "$R5_STATE/main.conf" /etc/bluetooth/main.conf
# Restore bredr/le/privacy to the settings recorded before the test; e.g.
# sudo btmgmt -i hci0 bredr on  (only if originally on)
sudo systemctl start bluetooth
# Restore the original powered state; if originally on:
sudo btmgmt -i hci0 power on
sudo btmgmt -i hci0 info
```

If `active=original`, the first move saves that directory before restoring it,
so the same commands apply. Keep `$R5_STATE` private: its extra identity stores
contain central keys. Keep the logs and original configuration backup until
results are accepted. Re-pair the deliberately evicted D only after F-29 is
recorded; doing so intentionally replaces another LRU bond.
