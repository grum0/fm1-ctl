# GEN-1 for Android

The GEN-1 generators on a phone or tablet, with USB MIDI out. It compiles `app/gen.c` through the Android NDK: the
styles, the seven parts (drums, chords, arp, bass, lead, melodic kick, Marble), the seed and the instrument profiles.
Around it: a clock (the phone's own, or the MPC's), the arpeggiator, the four-track Euclidean sequencer, the Tanzbär
Lite controllers and a touch keyboard.

What differs from the firmware: a touch screen instead of four knobs; **a MIDI channel for each instrument**
(Rig page) instead of one per part; no onboard synth (it sends MIDI only); no Bluetooth MIDI; no MIDI thru.

**On this branch `app/gen.c` is ahead of the firmware's** (branch `gen-1`, which is untouched until the FM-1 has
been tried). Version 0.2.0 adds:

- **Styles**: 33, picked in two steps (family, then style, each with the tempos it sits at). New: Boom Bap, seven
  Neptunes styles (Minimal, Click, Club, Synth-funk, Dark, Stomp, N.E.R.D. Funk-rock) and Dub One-drop.
- **Drum modes** Boom Bap, Lo-Fi, Funk Break, Half Time, Hip-Hop, Fast Break: Cut-A-Dor's pattern generator (two-bar
  kick figures, a pocket for each hit, hats in three levels), usable under any style. The Boom Bap style uses it.
- **Drums page**: closed-hat density (with the kit, 1/4, 1/8, 1/16), flam, how often the fill comes (phrase end,
  never, every 1 to 16 bars), ratchets (32nd hats and snare rushes), Space. **Play → Groove**: Straighten the hats
  (takes the swing off the hats only). Hats are now quieter off the beat (three levels) in every style.
- **Bass → With the kick**: free, follows the kick, or locked to it (some styles choose for themselves).
  **Chords → Voicing**: the style's, tight, wide, octaves. **Marble → Spread**.
- The clock keeps going with the screen off (a notification with *Stop and quit*); a red line says when the USB
  cable is not in MIDI mode; a wider layout on a tablet held sideways (Play stays on the left); the Grumo mark.

## Getting the app

On GitHub: **Actions → Android APK → Run workflow** (branch `android`), then open the run and download the
artifact `gen1-debug-apk`. It is a zip with `app-debug.apk` in it. Open the APK on the phone and allow installing
from that source when Android asks. Needs Android 10 or newer.

From 0.2.0 every build is signed with the same debug key (`android/debug.keystore`), so a new APK installs over the
last and keeps its settings. Builds before 0.2.0 were each signed differently: **uninstall the old app once** before
installing 0.2.0 (its settings go with it; the channels now default to the real ones).

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
2. **Rig → Instruments**: each instrument's channel. **Test** plays a short note on it. The defaults are the rig's:
   Volca FM 2, Monologue 3, Tanzbär Lite 4, MBase 11 8, Hades 9, MB-33 12 (the JV-1010's 6 and 7 are placeholders).
3. **Play → Clock**: *Follow MPC* (starts and stops with the MPC, or with Play here) or *Internal* (the phone keeps
   time; *Send clock* also sends it out).
4. **Parts**: switch parts on, give each its instrument. **Play** to start.

## Timing

Each message is sent with the time it should sound, so the phone's MIDI service releases it on time even when the
app's thread wakes late (the internal clock is made 30 ms ahead). Following the MPC, a note is as steady as the MPC's
clock reaching the phone over USB; the three subticks between two clock ticks are spread over the last tick's length.
A foreground service (the GEN-1 notification) keeps the app and the processor running with the screen off or
another app in front; *Keep the screen on* (Rig → Settings) is on by default all the same.

Plugged straight into the MPC (the phone as a USB MIDI device), Android must be told *Use USB for: MIDI*, and it
forgets after a replug or a lock: the app shows a red line when the cable is in but not in MIDI mode.

## Building and testing

- CI (`.github/workflows/android.yml`) runs the engine's host test, `android/tools/check.sh`, then Gradle.
- `sh android/host-test/run.sh` builds the engine with a virtual clock and checks the clock spacing, transport,
  channels, arpeggiator, sequencer, the settings' ranges, and that a part played alone is the part played in the
  band, event for event.
- `android/tools/gen_fields.sh` rewrites `Fields.kt` after `gen1_engine.h` changes.

## Licence

The app as a whole is under **GPL-3.0-only**: `app/gen_cad.h` is the drum pattern generator of
[Cut-A-Dor](https://github.com/kidbee85-beep/cut-a-dor-release) by Kid Bunka (GPL-3.0-only), without its sounds and
effects. The rest of the generators (`app/gen.c`) and of the app is GPL-3.0-or-later. The clock, arpeggiator and
sequencer in `gen1_engine.c` are ported from CTL-1's `engine.c` (MPL-2.0, Charles Vestal). The Grids drum maps are
Mutable Instruments' (see `app/gen.c`).
