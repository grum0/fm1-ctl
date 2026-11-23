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

**0.3.0** (the hip-hop batch):

- **Nine new hip-hop styles** (42 in all): Memphis, Trap, Native Tongues, Wu-Tang, Madlib, Abstract NY, Old School,
  New Orleans bounce, Chopped and screwed. In Chopped and screwed the last bar of each phrase is chopped: its second
  half replays the first, every part.
- **Style names** from the catalogue (Soulquarians, Quincy Jones, Minneapolis 80s, Prince NPG 90s, New jack swing,
  Modern electro, Detroit techno, Acid house, Drill n bass, Neptunes Tongue-click, …); the picker shows each style's
  artists or scene under its name. The patterns and saved settings are unchanged.
- **Rig → Settings → Hide styles that don't suit the Tanzbär**: leaves out of the picker the styles built on sampled
  breaks or a live kit (off by default; the style playing always stays).

**0.3.1** (playing with the MPC):

- **Panic** (Play's top bar) now also sends All Sound Off, Reset All Controllers and pitch bend centred on every
  channel, besides All Notes Off. The transport keeps going.
- **Song Position and Continue** when following the MPC: move the MPC's playhead while stopped and press Play
  (Continue): the parts play on from there, bar count, fills and the sequencer included. Start still begins at the
  top.
- **Rig → Following the MPC → Output delay** (0–40 ms, default 5): how long after each MPC clock tick the app's
  notes go out. Raise it if the app sounds early against the MPC's own tracks, lower it if it drags.
- **Rig → MIDI monitor**: every message going out and coming in, with its time; Pause, Clear, Hide clock ticks.
  Following the MPC, it shows the MPC clock's tempo and how much its ticks wander (jitter). It only records while
  it is on screen.

**0.3.2** (the Tanzbär's sound per style):

- **Rig → Tanzbär sound follows the style and key** (on by default): picking a style, changing the key and Start
  send the Tanzbär Lite its tone and decay settings for the style, and tune its kick to the key (toms, congas and
  claves too in the styles that ask). The values come from Grumo's sweep recording of the unit
  (`android/tools/tanzbar/`: `measured.tsv`, `styles.tsv`, made into `TanzData.kt` by `make_tanz.py`). The Control
  page's Tanzbär sliders follow and stay free to change. Only when the drums play on the Tanzbär.
- **Cowbell pitch**: Style, Fixed (one pitch in the key) or Melodic (a line on the chords, the cowbell retuned half
  a sixteenth before each hit, with a short decay).
- **Picking a style moves the tempo into its range** (off by default; not while following the MPC).
- **Panic** no longer sends Reset All Controllers to the Tanzbär, and sends its sound again afterwards.

**0.3.3** (batch [D], the bell styles):

- **Four melodic-cowbell styles** (46 in all): Electro bell funk, Miami bass bell, Detroit bell techno, Afro-funk
  bell. With Rig → Cowbell pitch on **Style** (the default) and the drums on the Tanzbär, the cowbell plays each
  style's written bell line, retuned before every hit; Mutate changes at most one note a bar, never the first.
  Detroit bell techno's loop slowly moves one note every 4th bar; Afro-funk bell ends every 4th bar on the flat 7th.
- **Rig → Kick tune**: Style (each style's own kick pitch, the default) or Key (tuned to the key, as 0.3.2 did).
- The cowbell's decay while it plays a line is now per style (Miami bass bell a little longer).

**0.3.4** (the funk batch, the pocket):

- **Four funk styles** (50 in all): Talkbox funk, Modern boogie, Go-go, Blaxploitation.
- **Play → Groove → Cut-A-Dor's pocket on the styles' drums** (off by default): the per-position pocket of
  Cut-A-Dor's Boom Bap (off-beat kicks a little late, the backbeat late, ghost snares early, hats pushed and pulled)
  on every style's own drums, scaled by Feel (8: as Cut-A-Dor ships it; higher: more).

**0.3.5** (the R&B and electro batches):

- **Five styles** (55 in all): Timbaland and Gated 80s pop (RNB); Kraftwerk, Italo disco and Miami bass (ELC).
- **The Simmons zap** in Electro (a laser answer every 4th bar), 808 electro and Modern electro, and in the new
  electro styles; Gated 80s pop's noise snare every 4th bar. Only with the Simmons in the drum set (Drums → drum
  set), so with the default kit these styles play as before.

**0.3.6** (the breaks and beats batch):

- **Four styles** (59 in all): LA beats and Trip-hop (hidden in the Tanzbär picker), Broken beat and Montreal
  bounce.
- **The pump** in LA beats: the chord part's expression (CC 11) dips on every kick and swells back by the next 8th,
  the sidechain sound on the pads without a compressor. Leaving the style puts CC 11 back to full.
- **The harp run** in LA beats: on the first beat of each phrase the arp part plays a quick 32nd run up two octaves
  of the chord.

**0.4.4** (batch [B], part 1):

- **Four styles** (82 in all): Ballroom / vogue (the Ha-dance clap and off-beat crashes), Jersey club, Gqom (no kick
  on the 1, no hats, the low tom and congas carry it over a minor drone) and Kuduro (four on the floor, frantic toms,
  off-beat cowbell, a rave riff).
- **Jersey club's bounce**: the bar's last three kicks play as 8th-note triplets. The bass locked to the kick keeps
  the straight steps.

**0.4.3** (batch [A], part 2):

- **Four IDM styles** (78 in all): Aphex acid-electro (a 303 line whose cutoff drifts over 8 bars), Boards of Canada
  haze, Autechre machine funk, Warp ambient techno (the arp in 16ths).
- **Warble** (Boards of Canada): the chords' and lead's pitch drifts up to 25 cents like worn tape, slowly and never
  quite the same twice. Leaving the style centres the pitch bend again.
- **Drift** (Autechre, Style drum mode): the drums play the Grids map at a position that glides somewhere new every
  8 bars, so the groove keeps morphing and never comes back; the bass follows the drifting kick. Your own Grids
  settings are left as they are.

**0.4.2** (batch [A], part 1):

- **Four styles** (74 in all): Grime, Drum and bass (hidden in the Tanzbär picker), Chicago jack, Ghetto house / juke.
- **Grime's sections**: every 16 bars split in two halves; the first plays the square-wave bass and stabs with the
  drums, the second drops them for the lead, and each half ends on a kick-and-clap stop.
- **Drum and bass's Reese**: the bass synth's filter sweeps slowly over two bars (the wobble's slow rate).

**0.4.1** (Tanzbär knobs as offsets):

- With "Tanzbär sound follows the style and key" on, the Control page's Tanzbär knobs that a style sets (kick decay
  and tone; snare tone, noise and decay; clap filter, decay and attack; tom, conga, cowbell and clave decays; cymbal
  tone and decay; hat decays) become **offsets on every style's value**: turn the cowbell's decay up once and it rings
  longer on every style. The knob shows the value sent, with the offset in brackets, e.g. "52 (+21)".
- Tunings have no offset, so the drums stay in key. "Back to the style's sound (offsets to 0)" under the knobs clears
  them. With the follow switch off the knobs are plain values, as before (the offsets are kept for later).

**0.4.0** (Caribbean and world; the original style plan done):

- **Five styles** (70 in all), all suiting the Tanzbär: Dancehall (snare on 2 and 4, an off-beat organ skank, a
  reggae bass riff), Reggaeton classic (four on the floor under the dembow snare), Reggaeton modern (kick on 1 and 3,
  a soft dembow, a sliding 808), Afrobeat (clave and cowbell against each other, congas, a 16th arp ostinato) and
  Baile funk (the tamborzão on kick, tom and conga, no hats).
- Every style of the 2026-11-15 plan is now built, apart from those moved into batches [A] and [B].

**0.3.9** (IDM):

- **Braindance** (Rephlex; 65 styles in all) with **polymeter**: the kick and snare keep a 16-step bar while the closed
  hat, tom and claves each loop at their own length (the seed picks it: hat 10–14 steps, tom 9–11, claves 5–7), so
  the groove keeps shifting and comes back together at the start of every phrase. Style drum mode only.

**0.3.8** (the UK bass batch):

- **Three styles** (64 in all): Jungle (hidden in the Tanzbär picker), UK garage 2-step and Dubstep.
- **Break edits** in Jungle: in every bar but a phrase's first, one or two beats of the break are replaced by another
  beat of the bar, by a rising snare roll, or by the beat played backwards.
- **The wobble** in Dubstep: the bass part's cutoff (its synth's filter CC, 74 if it has none) rides an LFO whose rate
  changes bar by bar (quarter, 8th, 8th triplet, 16th). Leaving the style puts the filter back.

**0.3.7** (the house and techno batch, and the JV-1010 part suggestions):

- **Two styles** (61 in all): French touch and Dub techno.
- **The filter sweep** in both: the chord part's cutoff (CC 74, or the chord instrument's own filter CC) closes and
  opens again over 16 bars, darkest at the phrase boundary. Dub techno's is narrower and wanders a little bar to bar.
  Leaving the style opens the filter back to its resting value.
- **JV-1010 part suggestions** (see below).

## Getting the app

On GitHub: **Actions → Android APK → Run workflow** (branch `android`), then open the run and download the
artifact `gen1-debug-apk`. It is a zip with `app-debug.apk` in it. Open the APK on the phone and allow installing
from that source when Android asks. Needs Android 10 or newer.

From 0.2.0 every build is signed with the same debug key (`android/debug.keystore`), so a new APK installs over the
last and keeps its settings. Builds before 0.2.0 were each signed differently: **uninstall the old app once** before
installing 0.2.0 (its settings go with it; the channels now default to the real ones).

## JV-1010 part suggestions

The JV-1010 can hold a 15-part performance, **"GEN-1 Keys" in USER:01** (Rhodes, Wurlitzer, clavinets, organs,
Mellotrons, a lead, brass, an orchestra hit, a harp and a house chord, mostly from the SR-JV80-08 board). Part *n*
listens on channel *n*; part 10 (rhythm) is off. For every style the app suggests which part suits its chords,
Marble, arp and lead:

- **Play**: a line under the style name, e.g. `JV  chords 1 Jazz Rhodes · Marble 3 Grungy Wurly`, only for the
  parts that are on. The arp and lead belong to the Volca FM and the Monologue, so theirs read "try JV 15 Harp".
- **Style picker**: each style shows its chords part as a tag ("JV 1"), so styles can be browsed by sound.
- **Rig → Settings**: "Show JV-1010 part suggestions" (on by default).

These are suggestions only: the app keeps sending each part on its own channel, and you pick the JV part on the
MPC track's output channel. Nothing is sent to the JV by the app.

**Loading the performance** (once): put the JV-1010 in Performance mode, then send
`android/tools/jv1010/jv1010_user01.syx` to its MIDI IN from a computer with a SysEx tool (SysEx Librarian on a Mac,
MIDI-OX on Windows) through a USB-MIDI interface. It writes USER:01. To call it up: Bank Select 80/0 and Program
Change 1 on the Performance Control Channel. If the expansion board's sounds come up wrong, the board's group ID in
`make_jv.py` (8, assumed) is the one value to change.

The sounds and suggestions live in `android/tools/jv1010/` (`perf.tsv`, `hints.tsv`); `make_jv.py` writes both
`JvData.kt` and the `.syx` from them, so the app and the JV always agree. Change a sound in both tables together.

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
- Versions are tagged in git (`v0.2.1`, `v0.2.2`, `v0.3.0`, `v0.3.1`, …; the CI `tag` job makes them, with the `TAG_TOKEN` repository secret): CI artifacts expire after 90 days. The build ID (version and
  commit) is on the launch screen and in Rig → About.
- `android/tools/gen_fields.sh` rewrites `Fields.kt` after `gen1_engine.h` changes.
- `python3 android/tools/tanzbar/make_tanz.py` rewrites `TanzData.kt` (the Tanzbär profile) after its `.tsv` files
  change; `android/tools/check.sh` fails if it is stale.
- `python3 android/tools/jv1010/make_jv.py` rewrites `JvData.kt` and `jv1010_user01.syx` after `perf.tsv` or
  `hints.tsv` change; it refuses a style without a `hints.tsv` row, and `android/tools/check.sh` fails if either
  output is stale.
- Style drums are written as grids (`app/styles/*.grid`: `X` accent, `x` hit, `o` ghost, `r` a hit ratchets may
  double, `.` rest); `python3 app/styles/make_grids.py` turns them into `app/style_grids.h` (committed;
  `android/tools/check.sh` fails if it is stale).

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
