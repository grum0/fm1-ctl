/* SPDX-License-Identifier: MPL-2.0
 * The clock, arpeggiator and four-track sequencer are ported from CTL-1's engine.c (Copyright (C) 2026 Charles
 * Vestal, MPL-2.0); the rest is GEN-1 for Android (GPL-3.0-or-later, like app/gen.c, which this drives). */
/* GEN-1 for Android: the engine around app/gen.c, with no Android in it so it can be tested on a computer.
 *
 * Time is in nanoseconds on any monotonic clock the caller likes. Every message goes out through one sink with
 * the time it should sound; the Android layer hands it to the MIDI service with that timestamp, so the output is
 * steady even though the thread that makes it wakes up late now and then.
 *
 *   internal clock   eng_pump(now), called every few milliseconds, makes the clock and the generators' subticks up
 *                    to LOOKAHEAD ahead of now.
 *   external clock   eng_midi_clock(byte, time) for each F8 FA FB FC received; a tick makes its own four subticks,
 *                    spread over the last tick's length.
 * Not thread safe: the caller serialises every call. */
#pragma once
#include <stdint.h>
#include "gen.h"

enum { CLK_INT_OUT, CLK_INT, CLK_EXT, NCLK };   /* clock kept and sent out / kept in / followed from MIDI in */
enum { ARP_UP, ARP_DOWN, ARP_UPDOWN, ARP_RANDOM, ARP_PLAYED, NARP };
enum { RATE_4, RATE_8, RATE_8T, RATE_16, RATE_16T, RATE_32, NRATE };
#define NTRACKS 4
#define ENG_LOOKAHEAD_NS 30000000LL      /* how far ahead the internal clock is made */

/* A setting is one integer with an id; Kotlin has the same ids (tools/gen_fields.sh makes Fields.kt). */
enum {
    F_STYLE, F_ROOT, F_MINOR, F_PHRASE, F_FEEL, F_MUTATE, F_PROG, F_DSET, F_SEED, F_GX, F_GY, F_CHAOS, F_SIM2,
    F_SIM_CHAN, F_BEND, F_MB_XPOSE, F_MB_TUNE, F_MB_LO, F_SWING,
    F_TEMPO, F_CLOCK, F_KEYS_CHAN, F_VEL, F_HUMAN, F_ARP_ON, F_ARP_MODE, F_ARP_RATE, F_ARP_OCT, F_ARP_LATCH, F_SEQ_CH,
    F_LANE0,                             /* + lane: the note it sends */
    F_PART0 = F_LANE0 + GEN_NLANES,      /* + part * 8 + PF_* */
    F_TRK0 = F_PART0 + GEN_NPARTS * 8,   /* + track * 5 + TF_* */
    F_X0 = F_TRK0 + NTRACKS * 5,         /* added since: at the end, so settings saved before keep their numbers */
    F_HAT_DENS = F_X0, F_FLAM, F_FILL_EVERY, F_HAT_STRAIGHT, F_SPACE, F_SPREAD, F_COUPLE, F_VOICE, F_RATCHET,
    F_OUT_DELAY,                         /* 0.3.1: following, ms from a tick's arrival to what it plays (default 5) */
    F_BELL,                              /* 0.3.2: the cowbell plays a line (the app sets it from Cowbell pitch) */
    F_POCKET,                            /* 0.3.4: Cut-A-Dor's pocket on the styles' drums (Style mode), by Feel */
    F_COUNT
};
enum { PF_ON, PF_CHAN, PF_DEV, PF_VAR, PF_DENS, PF_FLAIR, PF_MODE, PF_OCT };
enum { TF_STEPS, TF_HITS, TF_ROT, TF_NOTE, TF_MUTE };
#define F_PART(p, f) (F_PART0 + (p) * 8 + (f))
#define F_TRK(t, f) (F_TRK0 + (t) * 5 + (f))

/* the sink: a MIDI message (1 or 3 bytes) to sound at when_ns */
typedef void (*eng_sink_t)(void *ud, int64_t when_ns, const uint8_t *msg, int len);

void eng_init(eng_sink_t sink, void *ud);
int eng_get(int id);
void eng_range(int id, int *lo, int *hi);
void eng_set(int id, int v, int64_t now);        /* clamped; a part's channel or device change ends its notes first */
void eng_defaults(int64_t now);                  /* everything back to its default (stops, notes off) */

int64_t eng_pump(int64_t now);                   /* the internal clock and the generators' main loop; returns the time to call again by */
void eng_midi_clock(uint8_t byte, int64_t at);   /* F8 FA FB FC from MIDI in, as received at `at` */
void eng_midi_spp(int sixteenths, int64_t at);   /* F2 (Song Position Pointer) from MIDI in */
void eng_play(int on, int64_t now);              /* the transport */
void eng_bell_table(int cc, const uint8_t tab[12]);   /* the cowbell line's tune CC and values (Tanzbär profile) */
void eng_panic(int64_t now);                     /* every note off; all notes off, all sound off, reset controllers
                                                  * and pitch bend centred on every channel (transport unchanged) */
void eng_send_now(uint8_t st, uint8_t d1, uint8_t d2, int64_t now);   /* a message from the screen: keys, knobs, wheels */
void eng_arp_press(int note);
void eng_arp_release(int note);
void eng_arp_latch_changed(void);
void eng_chord_key(int pc, int64_t now);
void eng_part_off(int p, int64_t now);

/* for the screen: out[0] running, [1] bar, [2] sixteenth, [3] phrase bars, [4] beat light, [5] parts sounding (bits),
 * [6] tempo x 10 (followed from the clock when it is external), [7] clock seen lately */
void eng_status(int32_t *out, int64_t now);
