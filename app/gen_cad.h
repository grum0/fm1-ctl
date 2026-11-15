/* SPDX-License-Identifier: GPL-3.0-only */
/* The drum pattern generator of Cut-A-Dor, by Kid Bunka (github.com/kidbee85-beep/cut-a-dor-release, GPL-3.0-only):
 * cad_generate, cad_mutate and cad_step_tick of its firmware/src/dsp/cad.c with their tables, carried over as written
 * but for the plumbing: a pattern is a plain argument (no project, no locks, no per-hit nudges), and none of its
 * sounds, sampler or effects are here. gen.c includes this file and plays the patterns as MIDI.
 *
 *   two bars     a pattern is 32 sixteenths: sixteen two-bar kick figures where the second bar answers the first,
 *                eight hat figures, a fixed backbeat, ghost notes and percussion by probability at chosen steps.
 *   velocities   hats in three levels (the beat, the eighth, the sixteenth); ghost snares quiet.
 *   the pocket   cad_pocket: where in its sixteenth a hit sits, in 96ths of a quarter note, by track and position,
 *                scaled by Human: off-step hats late, in-between hats early, the backbeat late, ghosts early, the
 *                kick's downbeat firm. */
#pragma once
#include <stdint.h>

enum { C_KICK, C_SNARE, C_HAT, C_OPEN, C_CLAP, C_PERC, C_RIM, C_EXTRA, CAD_TRACKS };
enum { CAD_BOOMBAP, CAD_LOFI, CAD_FUNKBREAK, CAD_HALFTIME, CAD_HIPHOP, CAD_FASTBREAK, CAD_NSTYLES };
#define CAD_STEPS 32
typedef struct { uint8_t vel[CAD_TRACKS][CAD_STEPS]; } cad_pat_t;

static const char *const CAD_NAME[CAD_NSTYLES] = {"Boom Bap", "Lo-Fi", "Funk Break", "Half Time", "Hip-Hop", "Fast Break"};

/* Sixteen two-bar kick conversations: the second bar answers the first. */
static const uint16_t cad_kick_a[16] = {0x0441, 0x4209, 0x0101, 0x0881, 0x0483, 0x8449, 0x0841, 0x0401,
                                        0x0241, 0x2081, 0x0821, 0x0429, 0x4009, 0x1081, 0x0405, 0x8201};
static const uint16_t cad_kick_b[16] = {0x4481, 0x0841, 0x8401, 0x8405, 0x8141, 0x4905, 0x4009, 0x44c1,
                                        0x8421, 0x0249, 0x8801, 0x4181, 0x0489, 0x8409, 0x4241, 0x0485};
static const uint16_t cad_hats[8] = {0x5555, 0x4545, 0x5545, 0x454d, 0x5555, 0x6565, 0x5955, 0x555d};

static uint32_t cad_rng(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *s = x ? x : 0x1cba374du;
}
static int cad_clip(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static int cad_anchor(int style, int t, int st)
{
    return t == C_KICK ? st % 16 == 0 : t == C_SNARE ? (style == CAD_HALFTIME ? st % 16 == 8 : st % 8 == 4) : 0;
}
/* Space: the hats and percussion leave the end of the second bar open */
static int cad_gap(int space, int t, int st)
{
    int n = space >= 75 ? 8 : space >= 35 ? 4 : 0;
    return t >= C_HAT && n && st >= 32 - n;
}

/* style 0..5, dens and space 0..100 */
static void cad_generate(cad_pat_t *q, int style, int dens, int space, uint32_t seed)
{
    uint32_t s = seed ? seed : 1, km, hm;
    int family = (int)(seed % 16u), t, st;
    unsigned f;
    s ^= (uint32_t)(style + 1) * 0x85ebca6bu;
    f = (unsigned)(family + style * 3) % 16;
    km = (uint32_t)cad_kick_a[f] | ((uint32_t)cad_kick_b[f] << 16);
    hm = (uint32_t)cad_hats[f % 8] | ((uint32_t)cad_hats[(f + 3) % 8] << 16);
    if (style == CAD_FUNKBREAK || style == CAD_FASTBREAK) {
        km |= 1u << ((family & 1) ? 14 : 6);
        hm |= family & 1 ? 0x80802020u : 0x22000808u;
    }
    if (style == CAD_HALFTIME) {
        km &= 0x81018101u;
        hm &= 0x55555555u;
    }
    km |= 1u | (1u << 16);                               /* kick on step 1 of each bar */
    for (t = 0; t < CAD_TRACKS; t++)
        for (st = 0; st < CAD_STEPS; st++) {
            int j = st % 16, on = 0, prob = 0, vel = 0;
            uint32_t rv = cad_rng(&s), chance = cad_rng(&s) % 100u;
            if (t == C_KICK) {
                on = (int)((km >> st) & 1u);
                prob = (j == 3 || j == 10 || j == 15) ? dens / 8 : 0;
                vel = on ? (j % 4 ? 88 + (int)(rv % 13) : 114) : 65;
            } else if (t == C_SNARE) {
                on = cad_anchor(style, t, st);
                prob = (j == 3 || j == 11) ? dens / 2 : (j == 7 || j == 15) ? dens / 3 : 0;
                if (style == CAD_FUNKBREAK || style == CAD_FASTBREAK)
                    prob = prob * 3 / 2;
                vel = on ? (j == 12 ? 105 : 116) : 24 + (int)(rv % 22);
            } else if (t == C_HAT) {
                on = (int)((hm >> st) & 1u);
                prob = j % 2 ? dens / 3 : dens / 12;
                vel = on ? (j % 4 == 0 ? 78 : j % 4 == 2 ? 51 : 35) : 29;
            } else if (t == C_OPEN) {
                prob = ((j + family) % 8 == 6) ? dens : 0;
                vel = 58;
            } else if (t == C_CLAP) {
                prob = cad_anchor(style, C_SNARE, st) && ((st / 8 + family) % 3 == 0) ? dens : 0;
                vel = 49;
            } else if (t == C_PERC) {
                prob = j % 4 == (family + 3) % 4 ? (st >= 16 ? dens : dens / 3) : 0;
                vel = 44 + (int)(rv % 23);
            } else if (t == C_RIM) {
                prob = j == (family * 3 + 7) % 16 ? dens : 0;
                vel = 52;
            } else {
                prob = st == 26 + family % 4 ? dens / 2 : 0;
                vel = 47;
            }
            on = on || (int)chance < prob;
            if (cad_gap(space, t, st))
                on = 0;
            q->vel[t][st] = (uint8_t)(on ? cad_clip(vel + (int)(rv % 11) - 5, 1, 127) : 0);
        }
}

/* a few hits (two to five) switched on or off, never the anchors */
static void cad_mutate(cad_pat_t *q, int style, int space, uint32_t seed)
{
    uint32_t s = seed ? seed : 1, touched[CAD_TRACKS] = {0};
    unsigned need = 2 + cad_rng(&s) % 4u, done = 0, k;
    for (k = 0; k < 256 && done < need; k++) {
        int t = (int)(cad_rng(&s) % 8u), st = (int)(cad_rng(&s) % 32u);
        uint32_t bit = 1u << st;
        if ((touched[t] & bit) || cad_anchor(style, t, st) || cad_gap(space, t, st))
            continue;
        touched[t] |= bit;
        q->vel[t][st] = q->vel[t][st] ? 0 : (uint8_t)(t == C_KICK ? 88 : t == C_SNARE ? 35 : 45 + cad_rng(&s) % 25u);
        done++;
    }
}

/* The pocket: a hit's place against its sixteenth, in 96ths of a quarter note, before swing. human 0..100 (and
 * beyond: more of the same). At 92 BPM one is 6.79 ms. */
static int cad_pocket(const cad_pat_t *q, int style, int human, uint32_t seed, int t, int st)
{
    int j = st % 16, feel, ghost = q->vel[t][st] < 65;
    if (t == C_KICK)
        feel = j % 4 == 0 ? 0 : (style == CAD_FUNKBREAK ? -2 : style == CAD_LOFI ? 2 : 1);
    else if (t == C_SNARE)
        feel = ghost ? -2 : (style == CAD_LOFI ? 6 : style == CAD_HALFTIME ? 4 : style == CAD_FASTBREAK ? 1 : 3);
    else if (t == C_HAT)
        feel = j % 2 ? (style == CAD_LOFI ? 4 : 2) : (j % 4 == 2 ? -3 : -1);
    else if (t == C_CLAP)
        feel = 4;
    else
        feel = ((st * 3 + t * 5 + (int)(seed & 3)) % 7) - 3;
    return feel * human / 100;
}
