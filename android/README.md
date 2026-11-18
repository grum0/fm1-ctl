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

**0.2.1** (fixes):

- The JV-1010 defaults to channels 13 and 7. A saved 6 on its first part (the old placeholder) becomes 13.
- Channels and the instrument chosen for each part, the keys, the sequencer and the Control page are saved by
  instrument, so adding an instrument later keeps them. The Control page's *Any instrument* choice and its sliders are
  saved too, and the Tanzbär sliders are sent to it again whenever the MIDI output opens.
- No more notes left on: following the MPC, when its clock stops with no Stop (cable out), every note ends after
  300 ms; changing the clock mode stops the transport and ends the notes; the MPC's Stop ends the arpeggiator's note;
  Play after *Stop and quit* starts the background service again.

**0.2.2** (fixes, and checks for what comes next):

- Following the MPC: everything a clock tick makes is sent a fixed 5 ms after the tick arrives, so notes on the
  tick and between ticks keep their spacing; two ticks arriving together no longer throw the tick length back to
  the app's own tempo.
- The internal clock stays in step with the parts after the phone holds the app up for a moment.
- The bass's *With the kick* and the melodic kick follow the kick that Grids and Turing really play. Space in the
  Cut-A-Dor modes keeps its gaps with a hat density set.
- A note played again is no longer cut short by the safety note-off. A held key on the touch keyboard always lets
  go where it started. A MIDI device that disappears while connecting no longer leaves the app saying "Sending to…"
  with nothing going out. The background service can no longer make Android close the app if it fails to start.
- The app and the FM-1 firmware now make the same patterns (some random choices depended on the compiler).
- Build ID (version and commit) on the launch screen and in Rig → About, with complete credits.
- For each build: preview MIDI files per style (artifact `gen1-preview-midi`), golden-output checks, the
  generators' size for the FM-1.

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
   Volca FM 2, Monologue 3, Tanzbär Lite 4, MBase 11 8, Hades 9, MB-33 12, JV-1010 13 (first part) and 7 (second).
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

- CI (`.github/workflows/android.yml`) runs the engine's host test and the golden check, writes the preview MIDI
  files (artifact `gen1-preview-midi`), prints the generators' size for a microcontroller, runs
  `android/tools/check.sh`, then Gradle (artifact `gen1-debug-apk`).
- `sh android/host-test/run.sh` builds the engine with a virtual clock and checks the clock spacing, transport,
  channels, arpeggiator, sequencer, the settings' ranges, and that a part played alone is the part played in the
  band, event for event; then `render golden` compares every style under every drum mode (seed 1234, four bars at
  120 BPM, all parts on) with the fingerprints in `host-test/golden.txt`. When a style is changed on purpose:
  `android/host-test/build/render golden --update`, and say so in the commit.
- `render preview DIR`: one MIDI file per style (eight bars at the middle of its tempo range, one track per part,
  on the default channels: Tanzbär 4, JV-1010 13 and 7, Volca FM 2, MB-33 12, Monologue 3, MBase 11 8), to
  audition on the MPC before installing an APK. `render dump STYLE MODE` prints every message, for diffs.
- `sh android/tools/gen_size.sh`: Flash and RAM of `app/gen.c` compiled for a 32-bit microcontroller (Cortex-M3;
  the FM-1's JieLi chip needs the firmware builder's toolchain, so this is an estimate; RAM carries over closely).
- Versions are tagged in git (`v0.2.1`, `v0.2.2`, …): CI artifacts expire after 90 days. The build ID (version and
  commit) is on the launch screen and in Rig → About.
- `android/tools/gen_fields.sh` rewrites `Fields.kt` after `gen1_engine.h` changes.

## Licence and credits

The app as a whole is under **GPL-3.0-only**: `app/gen_cad.h` is the drum pattern generator of
[Cut-A-Dor](https://github.com/kidbee85-beep/cut-a-dor-release) by Kid Bunka (GPL-3.0-only), without its sounds and
effects. The rest of the generators (`app/gen.c`) and of the app is GPL-3.0-or-later. Credits (also in Rig → About):

- **CTL-1** by Charles Vestal (MPL-2.0): the clock, arpeggiator and four-track sequencer in `gen1_engine.c`, ported
  from CTL-1's `engine.c`.
- **Cut-A-Dor** by Kid Bunka (GPL-3.0-only): the drum pattern generator (`app/gen_cad.h`).
- **Mutable Instruments**: the Grids drum maps by Emilie Gillet (GPL-3.0-or-later), by way of Hortator's C port
  (`app/grids_tables.h`); Marbles was the inspiration for the Marble part.
- No fonts or other data are bundled yet; their licences will be listed here when they are.
