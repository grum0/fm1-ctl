# GEN-1 for Android

The GEN-1 generators on a phone, with USB MIDI out. It runs the firmware's own `app/gen.c` (compiled unchanged
through the Android NDK), so the 24 styles, the seven parts (drums, chords, arp, bass, lead, melodic kick, Marble),
the seed and the instrument profiles behave exactly as on the FM-1. Around it: a clock (the phone's own, or the
MPC's), the arpeggiator, the four-track Euclidean sequencer, the Tanzbär Lite controllers and a touch keyboard.

What differs from the firmware: a touch screen instead of four knobs; **a MIDI channel for each instrument**
(Rig page) instead of one per part; no onboard synth (it sends MIDI only); no Bluetooth MIDI; no MIDI thru.

## Getting the app

On GitHub: **Actions → Android APK → Run workflow** (branch `android`), then open the run and download the
artifact `gen1-debug-apk`. It is a zip with `app-debug.apk` in it. Open the APK on the phone and allow installing
from that source when Android asks. Needs Android 10 or newer.

## Wiring

A class-compliant USB MIDI interface on the phone's USB-C port (through an OTG adapter if the interface has USB-A).

```
phone ── USB ── interface OUT ── splitter ─┬─ Tanzbär Lite   (channel 4)
                                           ├─ MB-33, Hades, Monologue, JV-1010, Volca FM, MBase 11 ...
                                           └─ MPC MIDI IN    (only if the MPC should record what the phone plays)
MPC MIDI OUT ── interface IN                (only for Follow MPC)
```

Everything shares one cable, so each instrument must listen on its own channel; set the same numbers on the
instruments and on the Rig page. A splitter only copies: if an instrument already gets the MPC's MIDI on its input,
it needs a merger to get the phone's too, or the phone's notes can go through the MPC (phone into the MPC's MIDI IN,
a MPC track per instrument, as with the FM-1).

## First run

1. **Rig**: pick the interface under *Notes and clock go out through* (one USB interface is picked for you). Pick the
   same one under *The MPC's clock comes in through* if you will follow the MPC.
2. **Rig → Instruments**: set each instrument's channel. **Test** plays a short note on it. Only the Tanzbär's 4 is
   known; the others are placeholders.
3. **Play → Clock**: *Follow MPC* (starts and stops with the MPC, or with Play here) or *Internal* (the phone keeps
   time; *Send clock* also sends it out).
4. **Parts**: switch parts on, give each its instrument. **Play** to start.

## Timing

Each message is sent with the time it should sound, so the phone's MIDI service releases it on time even when the
app's thread wakes late (the internal clock is made 30 ms ahead). Following the MPC, a note is as steady as the MPC's
clock reaching the phone over USB; the three subticks between two clock ticks are spread over the last tick's length.
The app keeps the screen on while it is open: Android slows apps that are not in front.

## Building and testing

- CI (`.github/workflows/android.yml`) runs the engine's host test, `android/tools/check.sh`, then Gradle.
- `sh android/host-test/run.sh` builds the engine with a virtual clock and checks the clock spacing, transport,
  channels, arpeggiator, sequencer, the settings' ranges, and that a part played alone is the part played in the
  band, event for event.
- `android/tools/gen_fields.sh` rewrites `Fields.kt` after `gen1_engine.h` changes.

## Licence

GPL-3.0-or-later, like `app/gen.c`. The clock, arpeggiator and sequencer in `gen1_engine.c` are ported from CTL-1's
`engine.c` (MPL-2.0, Charles Vestal). The Grids drum maps are Mutable Instruments' (see `app/gen.c`).
