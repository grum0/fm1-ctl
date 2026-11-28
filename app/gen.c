/* SPDX-License-Identifier: GPL-3.0-or-later */
/* GEN: the generators. See gen.h. Integer only, no allocation; gen_subtick runs in the engine's tick.
 *
 *   time      96 subticks a quarter note (four a MIDI clock tick), 24 a sixteenth, 384 a bar. At subtick 0 of
 *             each bar every part that is on writes the bar's events (notes with lengths, controllers, pitch
 *             bends) into its list, sorted; the subticks play them.
 *   harmony   the key and the style choose a progression (chords by the half bar); bass, chords, arpeggio, lead
 *             and the melodic kick all read the chord of the moment.
 *   seed      every choice is drawn from hashes of the seed, the part's variation and the bar of the phrase:
 *             nothing depends on what was played before, so a part recorded later fits the ones before it.
 *   styles    per style: drum lanes as 16 weights ('0'..'9', 'a'..'e' ghost notes of weight 5..9), timing
 *             (swing, lanes late or early, jitter), fills, progressions, chord voicing and rhythm, bass and lead.
 *   devices   per instrument: range, how an accent and a slide are sent, the filter controller, bend range.
 *
 * The Grids drum maps: Mutable Instruments Grids by Emilie Gillet (grids_tables.h, GPL-3.0-or-later), by way of
 * Hortator's C port. The Boom Bap family of drum modes: Cut-A-Dor's pattern generator by Kid Bunka (gen_cad.h,
 * GPL-3.0-only; so a program built with it is under GPL-3.0-only as a whole). */
#include "gen.h"
#ifdef GEN_STANDALONE
#include <string.h>
#endif
#include "grids_tables.h"
#include "gen_cad.h"
#include "style_grids.h"

#define GEN_MAXON 10
enum { EV_BEND, EV_CC, EV_NOTE };        /* in the order they go out within a subtick */
typedef struct { uint16_t pos, dur; uint8_t kind, a, b, ch; } gev_t;
typedef struct { uint8_t note, ch; uint16_t left; } gon_t;
typedef struct { uint8_t root, q; } chord_t;
typedef struct { uint32_t s; } rng_t;

/* ------------------------------------------------------------- tables --- */
enum { SC_MAJ, SC_AEO, SC_DOR, SC_MIX, SC_PHR, NSC };
static const uint16_t SC_MASK[NSC] = {0xAB5, 0x5AD, 0x6AD, 0x6B5, 0x5AB};   /* bit n: semitone n in the scale */
#define PENT_MIN 0x4A9u                  /* minor pentatonic: 0 3 5 7 10 */
#define PENT_MAJ 0x295u                  /* major pentatonic: 0 2 4 7 9 */

enum { Q_MAJ, Q_MIN, Q_7, Q_MAJ7, Q_MIN7, Q_9, Q_MAJ9, Q_MIN9, Q_MIN11, Q_13, Q_7SUS, Q_ADD9, Q_DIM7, Q_M7B5, Q_7S9,
       Q_MIN6, Q_P5, Q_MMAJ7, NQ };
static const uint8_t QIV[NQ][6] = {{0, 4, 7}, {0, 3, 7}, {0, 4, 7, 10}, {0, 4, 7, 11}, {0, 3, 7, 10}, {0, 4, 7, 10, 14},
                                   {0, 4, 7, 11, 14}, {0, 3, 7, 10, 14}, {0, 3, 7, 10, 14, 17}, {0, 4, 7, 10, 14, 21},
                                   {0, 5, 7, 10}, {0, 4, 7, 14}, {0, 3, 6, 9}, {0, 3, 6, 10}, {0, 4, 7, 10, 15},
                                   {0, 3, 7, 9}, {0, 7}, {0, 3, 7, 11}};
static const uint8_t QN[NQ] = {3, 3, 4, 4, 4, 5, 5, 5, 6, 6, 4, 4, 4, 4, 5, 4, 2, 4};
/* (spelled so they read the same in capitals: the screen's large type has no lower case) */
static const char *const QNAME[NQ] = {"", "min", "7", "maj7", "min7", "9", "maj9", "min9", "min11", "13", "7sus", "add9",
                                      "dim7", "min7b5", "7#9", "min6", "5", "minmaj7"};
static const char *const PC_NAME[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};

/* progressions: chords as semitones above the key, a quality and a length in half bars (2, 4 or 8 in all) */
typedef struct { uint8_t scale, n; struct { uint8_t semi, q, len; } c[5]; } prog_t;
static const prog_t PROG_MIN[] = {
    /* 0 */ {SC_DOR, 1, {{0, Q_MIN7, 8}}},
    /* 1 */ {SC_DOR, 2, {{0, Q_MIN7, 4}, {5, Q_9, 4}}},
    /* 2 */ {SC_AEO, 2, {{0, Q_MIN7, 4}, {10, Q_MAJ, 4}}},
    /* 3 */ {SC_AEO, 2, {{0, Q_MIN9, 4}, {5, Q_MIN9, 4}}},
    /* 4 */ {SC_AEO, 3, {{0, Q_MIN7, 4}, {8, Q_MAJ7, 2}, {10, Q_MAJ, 2}}},
    /* 5 */ {SC_AEO, 2, {{0, Q_MIN7, 4}, {8, Q_MAJ7, 4}}},
    /* 6 */ {SC_AEO, 2, {{0, Q_MIN7, 4}, {7, Q_MIN7, 4}}},
    /* 7 */ {SC_AEO, 4, {{0, Q_MIN9, 2}, {5, Q_MIN9, 2}, {8, Q_MAJ9, 2}, {7, Q_7S9, 2}}},
    /* 8 */ {SC_DOR, 5, {{0, Q_MIN11, 2}, {5, Q_13, 2}, {3, Q_MAJ9, 2}, {2, Q_M7B5, 1}, {7, Q_7S9, 1}}},
    /* 9 */ {SC_PHR, 2, {{0, Q_MIN, 6}, {1, Q_MAJ, 2}}},
    /* 10 */ {SC_AEO, 4, {{0, Q_MIN, 2}, {10, Q_MAJ, 2}, {8, Q_MAJ, 2}, {10, Q_MAJ, 2}}},
    /* 11 */ {SC_AEO, 1, {{0, Q_P5, 8}}},
    /* 12 */ {SC_DOR, 2, {{0, Q_7SUS, 4}, {10, Q_7SUS, 4}}},
    /* 13 */ {SC_DOR, 3, {{0, Q_MIN9, 4}, {5, Q_13, 3}, {6, Q_DIM7, 1}}},
    /* 14 */ {SC_AEO, 4, {{0, Q_MIN9, 2}, {8, Q_MAJ7, 2}, {10, Q_9, 2}, {7, Q_7S9, 2}}},
    /* 15 */ {SC_AEO, 3, {{8, Q_MAJ9, 2}, {10, Q_ADD9, 2}, {0, Q_MIN9, 4}}},
    /* 16 */ {SC_DOR, 1, {{0, Q_7S9, 8}}},
    /* 17 */ {SC_AEO, 5, {{0, Q_MIN7, 2}, {10, Q_ADD9, 2}, {8, Q_MAJ7, 2}, {7, Q_7SUS, 1}, {7, Q_7, 1}}},
    /* 18 */ {SC_AEO, 4, {{0, Q_MIN, 2}, {3, Q_MAJ, 2}, {5, Q_MIN7, 2}, {3, Q_MAJ, 2}}},
    /* 19 */ {SC_AEO, 3, {{0, Q_MIN9, 4}, {11, Q_MAJ9, 2}, {1, Q_MAJ7, 2}}},
    /* 20 */ {SC_DOR, 3, {{0, Q_7S9, 6}, {3, Q_9, 1}, {2, Q_9, 1}}},          /* the funk turnaround, down by semitones */
    /* 21 */ {SC_AEO, 2, {{0, Q_MIN7, 4}, {8, Q_MIN7, 4}}},                   /* cold: two minor chords */
    /* 22 */ {SC_AEO, 3, {{0, Q_MIN9, 4}, {3, Q_MAJ7, 2}, {5, Q_MIN7, 2}}},
    /* 23 */ {SC_DOR, 4, {{0, Q_MIN, 2}, {3, Q_MAJ, 2}, {10, Q_MAJ, 2}, {5, Q_MAJ, 2}}},
    /* 24 */ {SC_PHR, 2, {{0, Q_P5, 6}, {1, Q_P5, 2}}},
    /* 25 */ {SC_PHR, 2, {{0, Q_MIN7, 6}, {1, Q_9, 2}}},
    /* 26 */ {SC_AEO, 3, {{0, Q_MMAJ7, 4}, {8, Q_MAJ7, 2}, {5, Q_MIN9, 2}}},   /* the spy-film chord (Trip-hop) */
    /* 27 */ {SC_DOR, 2, {{0, Q_MIN7, 1}, {5, Q_7, 1}}},                     /* the one-bar disco loop (French touch) */
    /* 28 */ {SC_AEO, 1, {{0, Q_MIN9, 8}}},                                   /* one chord (Dub techno) */
    /* 29 */ {SC_AEO, 4, {{0, Q_MIN, 2}, {8, Q_MAJ, 2}, {10, Q_MAJ, 2}, {0, Q_MIN, 2}}},  /* i-VI-VII-i (Reggaeton) */
    /* 30 */ {SC_AEO, 4, {{0, Q_MIN7, 2}, {8, Q_MAJ7, 2}, {3, Q_MAJ7, 2}, {10, Q_7, 2}}}, /* i-VI-III-VII */
    /* 31 */ {SC_AEO, 3, {{0, Q_MIN, 4}, {5, Q_MIN, 2}, {7, Q_7, 2}}},     /* i-iv-V7, the harmonic minor's (Cumbia) */
    /* 32 */ {SC_PHR, 2, {{0, Q_MIN7, 4}, {1, Q_MAJ7, 4}}},                  /* i7 rubbing on bII maj7, 2 bars each (Punk-funk) */
    /* 33 */ {SC_DOR, 2, {{0, Q_MIN9, 4}, {5, Q_MIN9, 4}}},                  /* i9-iv9, 2 bars each (Classic disco) */
};
/* "major": where a style has no use for a bright key, its dominant side (mixolydian vamps) */
static const prog_t PROG_MAJ[] = {
    /* 0 */ {SC_MAJ, 2, {{0, Q_MAJ7, 4}, {5, Q_MAJ7, 4}}},
    /* 1 */ {SC_MAJ, 4, {{2, Q_MIN9, 2}, {7, Q_13, 2}, {0, Q_MAJ9, 2}, {9, Q_7S9, 2}}},
    /* 2 */ {SC_MAJ, 4, {{0, Q_MAJ7, 2}, {9, Q_MIN7, 2}, {2, Q_MIN7, 2}, {7, Q_7SUS, 2}}},
    /* 3 */ {SC_MAJ, 4, {{5, Q_MAJ9, 2}, {4, Q_MIN7, 2}, {2, Q_MIN9, 2}, {7, Q_7SUS, 2}}},
    /* 4 */ {SC_MIX, 1, {{0, Q_9, 8}}},
    /* 5 */ {SC_MIX, 2, {{0, Q_9, 6}, {5, Q_9, 2}}},
    /* 6 */ {SC_MIX, 3, {{0, Q_MAJ, 4}, {10, Q_MAJ, 2}, {5, Q_MAJ, 2}}},
    /* 7 */ {SC_MAJ, 2, {{0, Q_MAJ9, 4}, {2, Q_MIN9, 4}}},
    /* 8 */ {SC_MAJ, 2, {{5, Q_MAJ9, 4}, {4, Q_MIN7, 4}}},
    /* 9 */ {SC_MIX, 2, {{0, Q_7, 4}, {5, Q_7, 4}}},
    /* 10 */ {SC_MIX, 1, {{0, Q_7SUS, 8}}},
    /* 11 */ {SC_MAJ, 4, {{0, Q_MAJ7, 2}, {4, Q_MIN7, 2}, {5, Q_MAJ7, 2}, {5, Q_MIN6, 2}}},
    /* 12 */ {SC_MIX, 1, {{0, Q_P5, 8}}},
    /* 13 */ {SC_MIX, 2, {{0, Q_13, 4}, {10, Q_13, 4}}},
    /* 14 */ {SC_MAJ, 4, {{0, Q_MAJ9, 2}, {3, Q_MAJ9, 2}, {8, Q_MAJ9, 2}, {1, Q_MAJ9, 2}}},   /* major ninths, planing */
};

/* devices */
#define DF_NOVEL 1u                      /* no velocity: a ghost note is a short gate, an accent the controller */
#define DF_LEGATO 2u                     /* a slide is an overlap */
typedef struct { const char *name; uint8_t lo, hi, poly, acc, vmax, cc, rest, bend, flags; } gdev_t;
static const gdev_t DEV[GEN_NDEV] = {
    {"Generic", 0, 127, 8, 120, 110, 74, 64, 2, DF_LEGATO},
    {"MB-33", 24, 72, 1, 127, 112, 0, 0, 0, DF_LEGATO},      /* accent: velocity above 120; slide: overlap */
    {"Hades", 24, 84, 1, 100, 100, 1, 0, 3, DF_NOVEL | DF_LEGATO},   /* mod wheel to a CV; bend 3 semitones */
    {"Monologue", 24, 96, 1, 122, 110, 43, 64, 255, DF_LEGATO},      /* cutoff; glide on legato (portamento Auto) */
    {"JV-1010", 24, 108, 8, 120, 108, 74, 64, 2, 0},
    {"Volca FM", 24, 96, 3, 120, 108, 1, 0, 0, 0},
    {"MBase 11", 24, 72, 1, 127, 110, 0, 0, 0, 0},
    {"Tanzbar", 0, 127, 16, 127, 115, 0, 0, 0, 0},
};

/* styles */
enum { FILL_NONE, FILL_SNARE, FILL_TOMS, FILL_ELECTRO, FILL_LIVE, FILL_DROP, FILL_DNB, FILL_BUSY, FILL_CAD };
enum { V_TRIAD, V_SEVENTH, V_RICH, V_QUARTAL, V_POWER, V_TIGHT, V_WIDE, V_OCT };   /* (the last three: the Voicing setting's) */
enum { DM_STYLE, DM_GRIDS, DM_TURING, DM_CAD0, NDM = DM_CAD0 + CAD_NSTYLES };       /* DM_CAD0 + n: Cut-A-Dor's style n */
enum { CP_FREE, CP_FOLLOW, CP_LOCK };    /* the bass and the kick */
enum { CM_STYLE, CM_PAD, CM_STABS, NCM };
enum { AM_UP, AM_DOWN, AM_UPDOWN, AM_RANDOM, AM_CONVERGE, AM_PLAYED, NAM };
enum { BM_STYLE, BM_ROOT, BM_ACID, BM_BOOTSY, BM_WORRELL, BM_HERBIE, BM_QUINCY, BM_GLIDE, BM_ROBOT, BM_LAZY, BM_LIVE,
       BM_OCTAVE, BM_WOBBLE, BM_TURING, BM_DUB, BM_CUMBIA, NBM };
enum { LM_STYLE, LM_HOOK, LM_CALL, LM_SOLO, LM_WHINE, LM_TURING, NLM };
enum { KK_STYLE, KK_FOUR, KK_808, NKK };
enum { RM_CHORD, RM_PENT, RM_SCALE, NRM };

typedef struct {
    const char *name;
    const char *lane[GEN_NLANES];        /* 16 weights a lane, or none */
    int8_t sw, lk, ls, lh;               /* swing of the off sixteenths; kick, snare, hats late (subticks) */
    uint8_t jit, rat, fill;              /* jitter; ratchets (percent); FILL_* */
    uint8_t prog[2][4];                  /* progressions: minor, major */
    uint8_t voicing;                     /* V_* */
    const char *comp;                    /* the chords' rhythm */
    uint8_t comp_len;                    /* a stab's length in sixteenths, 0: held to the next */
    uint8_t bass, lead;                  /* BM_*, LM_* */
    const char *lead_rhy;
    uint8_t lead_base, pent, arp_div;    /* the lead's centre note; pentatonic; the arpeggio's step in 16ths */
    uint8_t couple;                      /* CP_*: the bass and the kick */
    uint8_t cad;                         /* 0: the lanes above; 1 + n: its drums are Cut-A-Dor's style n */
} style_t;

static const style_t STYLE[GEN_NSTYLES] = {
    [GS_DILLA] = {"Dilla",
        {[L_KICK] = "9000000705200030", [L_SNARE] = "000090000300900a", [L_RIM] = "0000000200000000",
         [L_CLAP] = "0000400000004000", [L_CHH] = "9161916191619162", [L_OHH] = "0000000000000020"},
        5, 1, 3, -1, 3, 0, FILL_LIVE, {{7, 3, 8, 22}, {1, 11, 14, 7}}, V_RICH, "9000000000300000", 0, BM_LAZY, LM_HOOK,
        "7000300000700030", 70, 1, 2, CP_FREE, 0},
    [GS_SOULQ] = {"Soulquarians",
        {[L_KICK] = "9000000007200400", [L_SNARE] = "0000900a00c09000", [L_RIM] = "0002000000020000",
         [L_CHH] = "9272927292729272", [L_OHH] = "0000000000000003"},
        6, 2, 4, 0, 3, 0, FILL_LIVE, {{8, 7, 13, 3}, {1, 11, 14, 3}}, V_RICH, "9000003000000600", 0, BM_LAZY, LM_CALL,
        "0070300070003000", 70, 0, 2, CP_FREE, 0},
    [GS_GFUNK] = {"G-Funk",
        {[L_KICK] = "9000001090070000", [L_SNARE] = "0000900000009000", [L_CLAP] = "0000600000006000",
         [L_CHH] = "9797979797979797", [L_OHH] = "0000000000000040", [L_COWB] = "0000000000200000"},
        2, 0, 1, 0, 1, 0, FILL_SNARE, {{5, 6, 2, 0}, {13, 5, 9, 4}}, V_SEVENTH, "9000000000000000", 0, BM_GLIDE, LM_WHINE,
        "9000000000700030", 79, 1, 2, CP_FREE, 0},
    [GS_BOMB] = {"Bomb Squad",
        {[L_KICK] = "9007009070050700", [L_SNARE] = "0000900200009003", [L_CHH] = "9898989898989898",
         [L_OHH] = "0040004000400040", [L_CONGL] = "0303503003053030", [L_COWB] = "0000300000300000",
         [L_SIM2] = "0000000300000050"},
        1, 0, 0, 0, 1, 10, FILL_BUSY, {{16, 0, 20, 11}, {4, 9, 12, 4}}, V_SEVENTH, "0090000900900000", 1, BM_ROOT, LM_CALL,
        "9090000090900000", 72, 1, 1, CP_FREE, 0},
    [GS_QUINCY] = {"Quincy Jones",
        {[L_KICK] = "9000003090000030", [L_SNARE] = "0000900000009000", [L_CLAP] = "0000700000007000",
         [L_CHH] = "9797979797979797", [L_OHH] = "0000000300000030", [L_CONGH] = "0030030000300302",
         [L_COWB] = "3000300030003000"},
        0, 0, 0, 0, 1, 0, FILL_TOMS, {{17, 1, 4, 18}, {3, 2, 13, 7}}, V_RICH, "9000009000700300", 2, BM_QUINCY, LM_HOOK,
        "9070007000907000", 72, 1, 1, CP_FREE, 0},
    [GS_JUSTIFIED] = {"Justified",
        {[L_KICK] = "9000007000090030", [L_SNARE] = "0000900000009000", [L_CLAP] = "0000900000009000",
         [L_RIM] = "0020000302000020", [L_CHH] = "9050905090509050", [L_CLAVE] = "0003000030000300",
         [L_CONGH] = "0000003000000030"},
        2, 0, 0, 0, 1, 5, FILL_DROP, {{3, 1, 15, 18}, {7, 3, 8, 13}}, V_SEVENTH, "0090009000900030", 1, BM_OCTAVE, LM_HOOK,
        "7000700030700000", 72, 1, 1, CP_FREE, 0},
    [GS_MPLS] = {"Minneapolis 80s",
        {[L_KICK] = "9000000090070000", [L_SNARE] = "0000900000009000", [L_CLAP] = "0000900000009000",
         [L_CHH] = "9797979797979797", [L_OHH] = "0000000000000030", [L_SIM1] = "0000000000000200"},
        0, 0, 0, 0, 0, 0, FILL_ELECTRO, {{12, 0, 1, 20}, {6, 10, 5, 4}}, V_QUARTAL, "9000009000090000", 2, BM_OCTAVE,
        LM_HOOK, "9000700090070000", 72, 1, 1, CP_FREE, 0},
    [GS_NPG] = {"Prince NPG 90s",
        {[L_KICK] = "9000030090300500", [L_SNARE] = "0000900a0000900c", [L_CHH] = "9494949494949494",
         [L_OHH] = "0000000500000005", [L_CONGH] = "0030003000300030", [L_COWB] = "0000300000003000"},
        4, 0, 1, 0, 2, 5, FILL_LIVE, {{13, 14, 20, 1}, {5, 13, 9, 1}}, V_RICH, "0090009700900900", 1, BM_LIVE, LM_CALL,
        "9070509070307050", 72, 1, 1, CP_FREE, 0},
    [GS_NJS] = {"New jack swing",
        {[L_KICK] = "9000000390300000", [L_SNARE] = "000090000000900a", [L_CLAP] = "0000800000008000",
         [L_RIM] = "0030000000300000", [L_CHH] = "9597959795979597", [L_OHH] = "0000000000000050"},
        7, 0, 0, 0, 1, 5, FILL_SNARE, {{14, 4, 3, 7}, {13, 2, 9, 3}}, V_SEVENTH, "9000000900900000", 2, BM_ROOT, LM_HOOK,
        "9000709000507000", 72, 1, 1, CP_FREE, 0},
    [GS_BOOGIE] = {"Boogie",
        {[L_KICK] = "9000000090000000", [L_SNARE] = "0000600000006000", [L_CLAP] = "0000900000009000",
         [L_CHH] = "9595959595959595", [L_OHH] = "0050005000500050"},
        1, 0, 0, 0, 1, 0, FILL_TOMS, {{15, 5, 3, 4}, {8, 7, 13, 3}}, V_RICH, "9000000000700000", 0, BM_WORRELL, LM_HOOK,
        "9000007000900050", 74, 1, 2, CP_FREE, 0},
    [GS_PFUNK] = {"P-Funk",
        {[L_KICK] = "9000000390000300", [L_SNARE] = "0000900a0a009000", [L_CLAP] = "0000300000003000",
         [L_CHH] = "9595959595959595", [L_OHH] = "0000000500000050"},
        3, 0, 1, 0, 2, 0, FILL_LIVE, {{16, 20, 1, 12}, {4, 5, 9, 13}}, V_SEVENTH, "0090000090000900", 1, BM_BOOTSY, LM_CALL,
        "9000705090007050", 72, 1, 1, CP_FREE, 0},
    [GS_JAZZFUNK] = {"Jazz-funk",
        {[L_KICK] = "9003000090030000", [L_SNARE] = "0000900a00a09002", [L_CHH] = "9797979797979797",
         [L_CONGL] = "0030000300300003", [L_CONGH] = "3000030000030300"},
        2, 0, 0, 0, 2, 0, FILL_LIVE, {{1, 12, 8, 20}, {5, 13, 4, 9}}, V_RICH, "9000090000900000", 2, BM_HERBIE, LM_SOLO,
        "9070309070503070", 72, 0, 1, CP_FREE, 0},
    [GS_ELECTRO] = {"Electro",
        {[L_KICK] = "9003009000900000", [L_SNARE] = "0000900000009000", [L_CLAP] = "0000900000009000",
         [L_CHH] = "9797979797979797", [L_OHH] = "0000000000300000", [L_COWB] = "0050005030050050",
         [L_SIM1] = "0000000000000070"},           /* 0.3.5: the zap as designed (the 4th bar: ZAP_RULES) */
        0, 0, 0, 0, 0, 15, FILL_ELECTRO, {{10, 9, 0, 2}, {12, 6, 10, 12}}, V_TRIAD, "9000009000900000", 2, BM_ROBOT,
        LM_HOOK, "9090009090009000", 72, 1, 1, CP_FREE, 0},
    [GS_ELECTRO2] = {"Modern electro",
        {[L_KICK] = "9000009000090000", [L_SNARE] = "0000900000009000", [L_CLAP] = "0000300000009000",
         [L_RIM] = "0030000300003000", [L_CHH] = "9090909790909097", [L_CLAVE] = "0000030000300000",
         [L_SIM1] = "000000000000000b"},           /* 0.3.5: a soft sonar ping, every other bar */
        0, 0, 0, 0, 0, 10, FILL_ELECTRO, {{9, 24, 21, 10}, {12, 10, 12, 6}}, V_TRIAD, "0000009000000090", 2, BM_ACID,
        LM_CALL, "9000900090709000", 72, 1, 1, CP_FREE, 0},
    [GS_WONKY] = {"Wonky",
        {[L_KICK] = "9000002000300600", [L_SNARE] = "000000a09000000b", [L_CLAP] = "0000000070000000",
         [L_RIM] = "0002000000020000", [L_CHH] = "9090909090909090", [L_OHH] = "0000000000000300"},
        4, -2, 5, 0, 5, 25, FILL_DNB, {{19, 8, 22, 3}, {14, 11, 1, 14}}, V_RICH, "9000000000090000", 0, BM_WOBBLE, LM_CALL,
        "7000305000300700", 74, 0, 3, CP_FREE, 0},
    [GS_BIGBEAT] = {"Big beat",
        {[L_KICK] = "9020000000970020", [L_SNARE] = "000090020a009003", [L_CHH] = "9090909090909090",
         [L_OHH] = "0000000500000000"},
        1, 0, 0, 0, 1, 5, FILL_SNARE, {{11, 0, 2, 9}, {12, 9, 6, 4}}, V_TRIAD, "9009000090090000", 1, BM_ACID, LM_HOOK,
        "9090709000907000", 72, 1, 1, CP_FREE, 0},
    [GS_BRAINDANCE] = {"Drill n bass",
        {[L_KICK] = "9003000030900030", [L_SNARE] = "000090030030a00a", [L_RIM] = "0300003000030300",
         [L_CHH] = "9797979797979797"},
        0, 0, 0, 0, 0, 40, FILL_DNB, {{19, 9, 11, 18}, {14, 12, 11, 6}}, V_SEVENTH, "9000000000000000", 0, BM_ACID,
        LM_SOLO, "9070907050907090", 74, 0, 1, CP_FREE, 0},
    /* Drexciya, Dopplereffekt: fast, straight, cold; the arpeggio belongs here */
    [GS_DETROIT] = {"Detroit techno",
        {[L_KICK] = "9000009000090090", [L_SNARE] = "0000900000009000", [L_CLAP] = "0000000000009000",
         [L_CHH] = "9797979797979797", [L_OHH] = "0050005000500050", [L_RIM] = "0000030000000300"},
        0, 0, 0, 0, 0, 10, FILL_ELECTRO, {{21, 9, 0, 10}, {12, 10, 6, 12}}, V_TRIAD, "9000000000000000", 0, BM_ROBOT,
        LM_HOOK, "9090900090909000", 72, 1, 1, CP_FREE, 0},
    /* Egyptian Lover: the 808 alone, long kicks, rolling hats, cowbell */
    [GS_808] = {"808 electro",
        {[L_KICK] = "9000009000900900", [L_SNARE] = "0000900000009000", [L_CLAP] = "0000900000009000",
         [L_CHH] = "9797979797979797", [L_OHH] = "0000000000000030", [L_COWB] = "9050905090509050",
         [L_TOML] = "0000000000000302", [L_SIM1] = "0000000700000700"},   /* 0.3.5: pews between the claps */
        0, 0, 0, 0, 0, 30, FILL_ELECTRO, {{11, 9, 0, 2}, {12, 10, 12, 6}}, V_POWER, "0000009000000000", 2, BM_OCTAVE,
        LM_HOOK, "9090009090009090", 72, 1, 1, CP_FREE, 0},
    /* Tipper: mid tempo, heavy swing, edits and ratchets, a bass that moves under long notes */
    [GS_GLITCH] = {"Glitch hop",
        {[L_KICK] = "9000000700200300", [L_SNARE] = "0000900000009002", [L_RIM] = "0020000002000030",
         [L_CHH] = "9060906090609060", [L_OHH] = "0000000000000030"},
        6, 0, 1, 0, 2, 35, FILL_DNB, {{25, 19, 0, 9}, {14, 12, 10, 14}}, V_SEVENTH, "0000009000000000", 1, BM_WOBBLE,
        LM_CALL, "7000300070003000", 72, 1, 3, CP_FREE, 0},
    /* Two Fingers: half time, the snare on three, room for the low end */
    [GS_HALFTIME] = {"Halftime",
        {[L_KICK] = "9000000000300600", [L_SNARE] = "0000000090000000", [L_CLAP] = "0000000050000000",
         [L_RIM] = "0003000000030002", [L_CHH] = "9050905090509757", [L_OHH] = "0000000000000300"},
        2, 0, 0, 0, 1, 25, FILL_DNB, {{24, 11, 21, 9}, {12, 12, 10, 14}}, V_POWER, "9000000000000000", 0, BM_LAZY,
        LM_CALL, "7000000030000700", 70, 1, 2, CP_FOLLOW, 0},
    /* Mr. Oizo: four on the floor, dry, and a bass riff that slides about */
    [GS_FLAT] = {"Flat Beat",
        {[L_KICK] = "9000900090009000", [L_CLAP] = "0000900000009000", [L_OHH] = "0090009000900090",
         [L_CHH] = "5050505050505057", [L_RIM] = "0000000300000030"},
        0, 0, 0, 0, 0, 5, FILL_DROP, {{11, 0, 16, 25}, {12, 4, 10, 9}}, V_TRIAD, "0000006000000000", 1, BM_HERBIE,
        LM_CALL, "9000900030009000", 72, 1, 1, CP_FREE, 0},
    /* The Prodigy: breakbeats at speed, minor stabs */
    [GS_RAVE] = {"Rave breaks",
        {[L_KICK] = "9090000000990000", [L_SNARE] = "0000900a0a00900a", [L_CHH] = "9090909090909090",
         [L_OHH] = "0000000000300000", [L_CYM] = "3000000000000000"},
        1, 0, 0, 0, 1, 15, FILL_DNB, {{23, 10, 2, 11}, {6, 12, 6, 12}}, V_TRIAD, "9009009000900900", 1, BM_ROOT, LM_HOOK,
        "9090709090007090", 72, 1, 1, CP_FREE, 0},
    /* Aphex Twin's analogue side: a 303 line over machine drums, wide minor pads behind */
    [GS_ACIDDREAM] = {"Acid house",
        {[L_KICK] = "9000000090090000", [L_SNARE] = "0000900000009000", [L_CLAP] = "0000300000003000",
         [L_CHH] = "9797979797979797", [L_OHH] = "0030000000300000", [L_RIM] = "0000003000000300"},
        0, 0, 0, 0, 1, 15, FILL_SNARE, {{22, 3, 5, 0}, {14, 11, 0, 7}}, V_RICH, "9000000000000000", 0, BM_ACID, LM_TURING,
        "7000300070003000", 72, 0, 1, CP_FREE, 0},
    /* Cut-A-Dor's Boom Bap: its drums (gen_cad.h) under dusty minor chords, a lazy bass on the kicks, a short hook */
    [GS_BOOMBAP] = {"Boom Bap",
        {[L_KICK] = "9000000300900030", [L_SNARE] = "0000900000009000", [L_CHH] = "9070907090709070"},
        4, 0, 2, 0, 2, 0, FILL_CAD, {{7, 3, 13, 22}, {1, 11, 7, 14}}, V_RICH, "9000000000300000", 0, BM_LAZY, LM_HOOK,
        "7000300000700030", 70, 1, 2, CP_FOLLOW, 1 + CAD_BOOMBAP},
    /* The Neptunes, seven ways. Bare: a drum machine and its gaps, knocks for a tune, the sub only where the kick is */
    [GS_NEP_MIN] = {"Neptunes Minimal",
        {[L_KICK] = "9009000090090000", [L_SNARE] = "0000900000009000", [L_CLAP] = "0000900000009090",
         [L_RIM] = "0090000900000000", [L_TOML] = "0000000000900900", [L_CLAVE] = "0000030000000003"},
        0, 0, 0, 0, 0, 0, FILL_DROP, {{11, 24, 9, 11}, {12, 12, 10, 4}}, V_POWER, "9000000000000000", 1, BM_ROOT, LM_CALL,
        "9000000000007000", 72, 1, 2, CP_LOCK, 0},
    /* clicks for hats and a rim for the snare, a long sliding sub, one whining synth line, and room */
    [GS_NEP_CLICK] = {"Neptunes Tongue-click",
        {[L_KICK] = "9000000000900300", [L_SNARE] = "0000300000003000", [L_RIM] = "0000900000009000",
         [L_CLAVE] = "9009009030090090", [L_CONGH] = "0000000300000030"},
        2, 0, 0, 0, 1, 0, FILL_NONE, {{0, 9, 25, 21}, {4, 10, 13, 4}}, V_TRIAD, "0000000000900300", 2, BM_GLIDE, LM_WHINE,
        "9000000000700030", 76, 1, 2, CP_FOLLOW, 0},
    /* the party: a chunky kick, claps, cowbell off the beat, congas, chord chops */
    [GS_NEP_CLUB] = {"Neptunes Club",
        {[L_KICK] = "9000000090090030", [L_SNARE] = "0000900000009000", [L_CLAP] = "0000900000009000",
         [L_CHH] = "9070907090709070", [L_OHH] = "0000000000000050", [L_COWB] = "0090009000900090",
         [L_CONGH] = "0030030000300300"},
        3, 0, 0, 0, 1, 0, FILL_SNARE, {{16, 20, 1, 12}, {4, 5, 9, 13}}, V_SEVENTH, "9000000090000900", 1, BM_ROOT, LM_HOOK,
        "9000709000507000", 72, 1, 1, CP_FOLLOW, 0},
    /* bleeping keys in eighths over a light bounce, a synth bass that walks and slides */
    [GS_NEP_SYNTH] = {"Neptunes Synth-funk",
        {[L_KICK] = "9000009000090000", [L_SNARE] = "0000900000009000", [L_CLAP] = "0000700000007000",
         [L_RIM] = "0000000300000300", [L_CHH] = "9090909090909090", [L_OHH] = "0000000000000030",
         [L_CLAVE] = "0030000000300000"},
        2, 0, 0, 0, 1, 5, FILL_DROP, {{3, 6, 21, 2}, {7, 0, 8, 2}}, V_TRIAD, "9090009090000090", 1, BM_WORRELL, LM_HOOK,
        "9070007000907000", 76, 1, 1, CP_FREE, 0},
    /* cold and stripped: two minor chords or a flat second, hats only off the beat, the lead answers and leaves */
    [GS_NEP_DARK] = {"Neptunes Dark",
        {[L_KICK] = "9000000090000090", [L_SNARE] = "0000900000009000", [L_CHH] = "0090009000900090",
         [L_RIM] = "0000000000030000", [L_TOML] = "0000000000000090"},
        0, 0, 0, 0, 0, 0, FILL_DROP, {{24, 21, 9, 25}, {12, 10, 12, 6}}, V_TRIAD, "9000000000000000", 0, BM_LAZY, LM_CALL,
        "7000000030000700", 70, 0, 2, CP_FOLLOW, 0},
    /* a marching band's stomp, stomp, clap; snare drags; a chant for a tune */
    [GS_NEP_STOMP] = {"Neptunes Stomp",
        {[L_KICK] = "9090000090900000", [L_SNARE] = "0000900a0000900a", [L_CLAP] = "0000900000009000",
         [L_TOML] = "0000000000000090"},
        0, 0, 0, 0, 0, 20, FILL_SNARE, {{11, 0, 10, 18}, {12, 6, 4, 9}}, V_POWER, "9000000000000000", 1, BM_ROOT, LM_HOOK,
        "9090009090009000", 72, 1, 1, CP_LOCK, 0},
    /* N.E.R.D.: the band in the room: a tight kick and snare, sixteenths on the hat, fifths for guitars, a busy bass */
    [GS_NERD] = {"N.E.R.D. Funk-rock",
        {[L_KICK] = "9000000390300000", [L_SNARE] = "000090030a009000", [L_CHH] = "9595959595959595",
         [L_OHH] = "0000000500000050"},
        1, 0, 1, 0, 2, 0, FILL_LIVE, {{16, 20, 23, 10}, {4, 6, 9, 5}}, V_POWER, "9000009090000900", 2, BM_LIVE, LM_CALL,
        "9070509070307050", 72, 1, 1, CP_FOLLOW, 0},
    /* one drop: nothing on the one, kick and rim together on three, the chords on every off beat, the bass leads */
    [GS_DUB] = {"Dub one-drop",
        {[L_KICK] = "0000000090000000", [L_SNARE] = "000000000000000a", [L_RIM] = "0000000090000000",
         [L_CHH] = "5090509050905090", [L_OHH] = "0000000000000030", [L_CONGL] = "0000030000000300"},
        4, 0, 1, 0, 2, 0, FILL_TOMS, {{2, 6, 10, 18}, {6, 9, 0, 2}}, V_TRIAD, "0090009000900090", 1, BM_DUB, LM_CALL,
        "7000000030007000", 72, 1, 2, CP_FREE, 0},
    /* 0.3.0, the hip-hop batch: drums from app/styles/hip.grid (style_grids.h), the rest from the designs */
    /* Memphis: half-time, cowbell, 8th hats with a flick, a phrygian horror riff over a long gliding 808 */
    [GS_MEMPHIS] = {"Memphis", GRID_MEMPHIS,
        1, 0, 0, 0, 0, 0, FILL_SNARE, {{9, 24, 25, 21}, {12, 4, 10, 6}}, V_OCT, "9000000000000000", 0, BM_GLIDE, LM_HOOK,
        "9000007000900000", 72, 0, 2, CP_LOCK, 0},
    /* Trap: half-time, straight, the hats rolled (ratchets on), 808 locked to the kick, a triplet bell line */
    [GS_TRAP] = {"Trap", GRID_TRAP,
        0, 0, 0, 0, 0, 60, FILL_SNARE, {{21, 5, 4, 19}, {7, 0, 8, 13}}, V_WIDE, "9000000000000000", 0, BM_GLIDE, LM_CALL,
        "9007000900700000", 76, 0, 2, CP_LOCK, 0},
    /* Native Tongues: on the grid but swung, bright jazz chords, the bass carries the tune */
    [GS_NATIVE] = {"Native Tongues", GRID_NATIVE,
        4, 0, 1, 0, 1, 0, FILL_LIVE, {{1, 0, 8, 13}, {7, 1, 2, 0}}, V_TIGHT, "9000000700000000", 6, BM_HERBIE, LM_CALL,
        "0000900070000000", 79, 1, 2, CP_FREE, 0},
    /* Wu-Tang: near-silent hats, a clashing chord stab, an eerie late piano loop */
    [GS_WUTANG] = {"Wu-Tang", GRID_WUTANG,
        2, 0, 1, 0, 3, 0, FILL_DROP, {{19, 9, 25, 3}, {14, 1, 11, 7}}, V_TIGHT, "9000000000000000", 2, BM_LAZY, LM_HOOK,
        "9000700070009000", 72, 0, 2, CP_FOLLOW, 0},
    /* Madlib: swung and loose, dusty conga and rim, library-jazz chords, flute fragments with long rests */
    [GS_MADLIB] = {"Madlib", GRID_MADLIB,
        5, 2, 2, 1, 3, 0, FILL_LIVE, {{13, 12, 3, 8}, {1, 10, 13, 7}}, V_TIGHT, "9000000000900000", 0, BM_LAZY, LM_CALL,
        "0000900000000700", 79, 1, 2, CP_FREE, 0},
    /* Abstract NY: mechanical, displaced second snare, dissonant drones, chromatic off-beat stabs */
    [GS_ABSTRACT] = {"Abstract NY", GRID_ABSTRACT,
        0, 0, 0, 0, 0, 0, FILL_TOMS, {{24, 25, 9, 11}, {12, 10, 4, 6}}, V_WIDE, "9000000000000000", 0, BM_ROBOT, LM_TURING,
        "0700070007000700", 72, 0, 1, CP_FOLLOW, 0},
    /* Old School (Run-DMC): hard and straight, loud kick and clap, tom fills ending on the zap, power-chord stab */
    [GS_OLDSCHOOL] = {"Old School", GRID_OLDSCHOOL,
        0, 0, 0, 0, 0, 0, FILL_TOMS, {{11, 24, 10, 6}, {12, 6, 4, 9}}, V_OCT, "9000000000000000", 2, BM_ROOT, LM_CALL,
        "9070900000000000", 72, 1, 2, CP_LOCK, 0},
    /* New Orleans bounce: the relentless kick-clap chant, 16th hats, a short descending bell riff */
    [GS_BOUNCE] = {"New Orleans bounce", GRID_BOUNCE,
        1, 0, 0, 0, 0, 0, FILL_SNARE, {{21, 6, 10, 2}, {6, 9, 12, 5}}, V_OCT, "9000000000000000", 0, BM_ROOT, LM_HOOK,
        "9070700070000000", 79, 0, 2, CP_LOCK, 0},
    /* Chopped and screwed: slow and dragging, soul chords, a low legato lead; the phrase's last bar is chopped */
    [GS_CHOPPED] = {"Chopped and screwed", GRID_CHOPPED,
        2, 2, 4, 2, 2, 0, FILL_NONE, {{3, 5, 22, 0}, {7, 0, 1, 8}}, V_TIGHT, "9000000000000000", 0, BM_GLIDE, LM_CALL,
        "9000000000000000", 60, 0, 2, CP_FOLLOW, 0},
    /* 0.3.3, batch [D], the melodic-cowbell styles: drums from app/styles/bell.grid, bell lines in BELL_LINES */
    /* Electro bell funk: the bell riff is the hook; stabs only at the phrase start, a robot bass on the kick */
    [GS_BELLFUNK] = {"Electro bell funk", GRID_BELLFUNK,
        1, 0, 1, 0, 1, 0, FILL_ELECTRO, {{10, 9, 0, 2}, {12, 6, 10, 12}}, V_TIGHT, "9000000000000000", 0, BM_ROBOT, LM_CALL,
        "9000000000000000", 72, 0, 2, CP_FOLLOW, 0},
    /* Miami bass bell: the broken kick and a long gliding sub, a chant-like bell hook around the fifth */
    [GS_MIAMIBELL] = {"Miami bass bell", GRID_MIAMIBELL,
        0, 0, 0, 0, 0, 0, FILL_SNARE, {{21, 5, 4, 19}, {7, 0, 8, 13}}, V_OCT, "9000000000000000", 0, BM_GLIDE, LM_CALL,
        "9000000000000000", 72, 0, 2, CP_LOCK, 0},
    /* Detroit bell techno: a 3+3+2 bell melody that slowly evolves, over held minor pads and a root sub */
    [GS_DETBELL] = {"Detroit bell techno", GRID_DETBELL,
        0, 0, 0, 0, 0, 0, FILL_NONE, {{13, 21, 9, 0}, {12, 10, 6, 12}}, V_WIDE, "9000000000000000", 0, BM_ROOT, LM_HOOK,
        "9000000000000000", 72, 0, 2, CP_FREE, 0},
    /* Afro-funk bell: a double bell (root and fifth) keeping time over a dorian vamp and a busy funk bass */
    [GS_AFROBELL] = {"Afro-funk bell", GRID_AFROBELL,
        3, 0, 1, 1, 2, 0, FILL_LIVE, {{13, 16, 20, 1}, {4, 5, 9, 13}}, V_TIGHT, "0000000000000090", 0, BM_BOOTSY, LM_CALL,
        "9070900000000000", 72, 1, 2, CP_FREE, 0},
    /* 0.3.4, the FNK batch: drums from app/styles/fnk.grid */
    /* Talkbox funk (Zapp): clap-led drum machine, chicken-scratch off-16th chords, octave-popping bass, a gliding
     * whine lead for the talkbox (the vowel sweep itself is not built) */
    [GS_TALKBOX] = {"Talkbox funk", GRID_TALKBOX,
        2, 0, 1, 0, 1, 0, FILL_SNARE, {{1, 0, 12, 16}, {4, 5, 9, 13}}, V_TIGHT, "0700070007000700", 1, BM_OCTAVE, LM_WHINE,
        "9000000070000000", 72, 0, 2, CP_FREE, 0},
    /* Modern boogie (Dâm-Funk): the dark, slow, dreamy side of Boogie: two bars a chord, wide minor 9 pads, a
     * sliding bass, noodling solo lines */
    [GS_MODBOOGIE] = {"Modern boogie", GRID_MODBOOGIE,
        3, 0, 1, 0, 1, 0, FILL_SNARE, {{3, 7, 8, 22}, {1, 10, 13, 7}}, V_WIDE, "9000000000000000", 0, BM_GLIDE, LM_SOLO,
        "9070907000000000", 79, 0, 2, CP_FREE, 0},
    /* Go-go (Chuck Brown): the conga and cowbell pocket, heavy swing, horn stabs, call and response */
    [GS_GOGO] = {"Go-go", GRID_GOGO,
        6, 0, 1, 1, 2, 0, FILL_LIVE, {{1, 2, 6, 20}, {5, 13, 9, 1}}, V_WIDE, "0090009000900090", 1, BM_LIVE, LM_CALL,
        "9070000000000000", 79, 1, 2, CP_FREE, 0},
    /* Blaxploitation (Isaac Hayes, Curtis Mayfield): sizzling 16th hats, wah-like muted 16th chords, octave bass,
     * a cinematic dorian line */
    [GS_BLAX] = {"Blaxploitation", GRID_BLAX,
        2, 0, 1, 0, 2, 0, FILL_LIVE, {{1, 8, 20, 13}, {4, 5, 9, 13}}, V_TIGHT, "9797979797979797", 1, BM_OCTAVE, LM_HOOK,
        "9000700090007000", 79, 0, 2, CP_FREE, 0},
    /* 0.3.5, the RNB batch: drums from app/styles/rnb.grid */
    /* Timbaland: a stuttering kick, 32nd hats (ratchets), a tabla-like rim and conga, the stop before the bar */
    [GS_TIMBALAND] = {"Timbaland", GRID_TIMBALAND,
        0, 0, 0, 0, 0, 60, FILL_DROP, {{9, 25, 21, 5}, {7, 0, 8, 13}}, V_TIGHT, "9000000000000000", 1, BM_ROOT, LM_HOOK,
        "9070000000700000", 72, 0, 2, CP_FOLLOW, 0},
    /* Gated 80s pop: the plainest straight beat, a big snare, an 8th root bass, wide pads, tom fills */
    [GS_GATED] = {"Gated 80s pop", GRID_GATED,
        0, 0, 0, 0, 0, 0, FILL_TOMS, {{4, 17, 5, 2}, {0, 5, 9, 13}}, V_WIDE, "9000000000000000", 0, BM_ROOT, LM_HOOK,
        "9000000000000000", 72, 0, 2, CP_FREE, 0},
    /* the ELC batch: drums from app/styles/elc.grid */
    /* Kraftwerk: metallic claves, a single machine zap, no looseness at all, plain triads and an 8th sequence */
    [GS_KRAFTWERK] = {"Kraftwerk", GRID_KRAFTWERK,
        0, 0, 0, 0, 0, 0, FILL_ELECTRO, {{10, 18, 23, 2}, {0, 5, 9, 13}}, V_TRIAD, "9000000000000000", 0, BM_ROBOT,
        LM_HOOK, "9070907090709070", 72, 0, 2, CP_FREE, 0},
    /* Italo disco: four-on-the-floor, the continuous octave bass, minor pads, a catchy line over a 16th arp */
    [GS_ITALO] = {"Italo disco", GRID_ITALO,
        0, 0, 0, 0, 0, 0, FILL_ELECTRO, {{10, 4, 5, 17}, {0, 5, 9, 13}}, V_WIDE, "9000000000000000", 0, BM_OCTAVE,
        LM_HOOK, "9070907000000000", 79, 0, 1, CP_FREE, 0},
    /* Miami bass: the broken kick under a long gliding 808 locked to it, cowbell, party claps */
    [GS_MIAMI] = {"Miami bass", GRID_MIAMI,
        0, 0, 0, 0, 0, 0, FILL_SNARE, {{21, 9, 5, 19}, {7, 0, 8, 13}}, V_OCT, "9000000000000000", 0, BM_GLIDE, LM_CALL,
        "9070000000000000", 72, 0, 2, CP_LOCK, 0},
    /* 0.3.6, the BRK batch: drums from app/styles/brk.grid */
    /* LA beats: a steady kick and snare under scattered hats, rim and conga; lush pumping pads, harp runs, a sub */
    [GS_LABEATS] = {"LA beats", GRID_LABEATS,
        5, 0, 0, 2, 2, 0, FILL_DROP, {{3, 1, 13, 8}, {7, 1, 0, 8}}, V_TIGHT, "9000000000000000", 0, BM_GLIDE, LM_CALL,
        "0000000700900000", 72, 0, 2, CP_FREE, 0},
    /* Trip-hop: a plain heavy break, the snare late, the minor-major 7 strings, a slow bass, a gliding lead */
    [GS_TRIPHOP] = {"Trip-hop", GRID_TRIPHOP,
        2, 0, 3, 0, 1, 0, FILL_DROP, {{26, 5, 21, 26}, {8, 7, 0, 8}}, V_TIGHT, "9000000000000000", 0, BM_LAZY, LM_WHINE,
        "9000000000000000", 67, 0, 2, CP_FREE, 0},
    /* Broken beat: the snare on 2 and the "and" of 3, tight rootless stabs, a sliding bass, a 3+3+2 lead */
    [GS_BROKEN] = {"Broken beat", GRID_BROKEN,
        3, 0, 1, 0, 2, 0, FILL_SNARE, {{1, 3, 12, 1}, {0, 7, 8, 0}}, V_TIGHT, "9000003000900070", 2, BM_LIVE, LM_CALL,
        "9009009090090090", 72, 0, 2, CP_FREE, 0},
    /* Montreal bounce: a broken kick between hip-hop and house, skippy hats, off-beat stabs, a bouncing bass */
    [GS_MONTREAL] = {"Montreal bounce", GRID_MONTREAL,
        6, 0, 0, 1, 1, 0, FILL_DROP, {{14, 7, 15, 22}, {1, 2, 3, 0}}, V_TIGHT, "0090009000900090", 1, BM_OCTAVE, LM_CALL,
        "0700000700070000", 74, 1, 2, CP_FREE, 0},
    /* 0.3.7, the HSE batch: drums from app/styles/hse.grid */
    /* French touch: four on the floor, off-beat open hats, a one-bar disco loop under the filter sweep */
    [GS_FRENCH] = {"French touch", GRID_FRENCH,
        1, 0, 0, 0, 0, 0, FILL_DROP, {{27, 27, 1, 27}, {2, 0, 2, 0}}, V_TIGHT, "9000300900309000", 1, BM_OCTAVE, LM_CALL,
        "9000000070000000", 72, 0, 2, CP_FREE, 0},
    /* Dub techno: a soft round kick, one minor 9 stab on the off-beats for the delay, the cutoff wandering */
    [GS_DUBTECH] = {"Dub techno", GRID_DUBTECH,
        0, 0, 0, 0, 0, 0, FILL_DROP, {{28, 3, 28, 3}, {0, 0, 0, 0}}, V_TIGHT, "0070000000900000", 1, BM_LAZY, LM_CALL,
        "0000000000000070", 72, 0, 2, CP_FREE, 0},
    /* 0.3.8, the BAS batch: drums from app/styles/bas.grid */
    /* Jungle: the chopped break (edited bar by bar, SFX_EDIT) with the ride, dark pads, a slow reggae-style bass */
    [GS_JUNGLE] = {"Jungle", GRID_JUNGLE,
        0, 0, 0, 0, 1, 0, FILL_DNB, {{28, 3, 28, 3}, {0, 0, 0, 0}}, V_WIDE, "9000000000000000", 0, BM_DUB, LM_CALL,
        "9000000000000000", 72, 1, 2, CP_FREE, 0},
    /* UK garage 2-step: the skipping kick under the heaviest shuffle, off-16th organ stabs, a rubbery bass */
    [GS_UKG] = {"UK garage 2-step", GRID_UKG,
        8, 0, 0, 0, 1, 0, FILL_SNARE, {{1, 8, 3, 13}, {0, 7, 8, 9}}, V_TIGHT, "0009000900090000", 1, BM_LIVE, LM_CALL,
        "0700700000000000", 72, 0, 2, CP_FREE, 0},
    /* Dubstep: half-step drums, sparse dark pads, held sub notes under the wobble (SFX_WOBBLE) */
    [GS_DUBSTEP] = {"Dubstep", GRID_DUBSTEP,
        2, 0, 0, 0, 1, 0, FILL_SNARE, {{25, 3, 28, 25}, {0, 0, 0, 0}}, V_WIDE, "9000000000000000", 0, BM_LAZY, LM_CALL,
        "9000000700000000", 72, 0, 2, CP_FREE, 0},
    /* 0.3.9, the IDM batch: drums from app/styles/idm.grid */
    /* Braindance (Rephlex): kick and snare on 16, the hats, tom and claves each looping at their own length
     * (SFX_POLY), all lining up again at the phrase start; sparse dorian chords, a toy-like bell lead */
    [GS_REPHLEX] = {"Braindance", GRID_REPHLEX,
        0, 0, 0, 0, 0, 0, FILL_SNARE, {{1, 8, 13, 1}, {0, 7, 8, 9}}, V_WIDE, "9000000000000000", 0, BM_OCTAVE, LM_CALL,
        "9090900090909000", 76, 0, 1, CP_FREE, 0},
    /* 0.4.0, the WLD batch: drums from app/styles/wld.grid */
    /* Dancehall: snare on 2 and 4, the rim and an organ skank on the off-beats, a reggae bass riff, a hook over it */
    [GS_DANCEHALL] = {"Dancehall", GRID_DANCEHALL,
        1, 0, 0, 0, 0, 0, FILL_SNARE, {{2, 21, 6, 2}, {0, 7, 8, 9}}, V_TIGHT, "0090009000900090", 1, BM_DUB, LM_HOOK,
        "9009009000900900", 72, 1, 2, CP_FREE, 0},
    /* Reggaeton classic: four on the floor under the dembow snare, minor stabs, an 808 locked to the kick */
    [GS_REGGAETON] = {"Reggaeton classic", GRID_REGGAETON,
        0, 0, 0, 0, 0, 0, FILL_TOMS, {{29, 10, 29, 4}, {0, 7, 8, 9}}, V_WIDE, "9000000000000000", 2, BM_ROOT, LM_HOOK,
        "9009009000909000", 72, 0, 2, CP_LOCK, 0},
    /* Reggaeton modern: kick on 1 and 3, a soft dembow marked by the clap, lush chords, a sliding 808 */
    [GS_REGGAETON2] = {"Reggaeton modern", GRID_REGGAETON2,
        0, 0, 0, 0, 1, 0, FILL_DROP, {{30, 22, 30, 5}, {0, 7, 8, 9}}, V_TIGHT, "9000000000000000", 0, BM_GLIDE, LM_CALL,
        "9090909090909090", 72, 0, 2, CP_FREE, 0},
    /* Afrobeat: clave and bell against each other, congas, muted dorian stabs with a 16th arp ostinato, horn riffs */
    [GS_AFROBEAT] = {"Afrobeat", GRID_AFROBEAT,
        3, 0, 0, 0, 2, 0, FILL_LIVE, {{0, 1, 0, 12}, {0, 7, 8, 9}}, V_TIGHT, "9090009000900090", 1, BM_LIVE, LM_CALL,
        "9090009000000000", 72, 1, 1, CP_FREE, 0},
    /* Baile funk: the tamborzão on kick, tom and conga, no hats, claps on 2 and 4, an 808 locked to the kick */
    [GS_BAILE] = {"Baile funk", GRID_BAILE,
        0, 0, 0, 0, 0, 0, FILL_TOMS, {{11, 9, 11, 24}, {0, 7, 8, 9}}, V_OCT, "9000000000000000", 0, BM_ROOT, LM_CALL,
        "9000000090000000", 72, 0, 2, CP_LOCK, 0},
    /* 0.4.2, batch [A] part 1: drums from app/styles/a1.grid */
    /* Grime: claps on 2 and 4, icy rim and claves, no hats; square stabs and bass in the first 8 bars of 16, the
     * lead in the second (SFX_SECT) */
    [GS_GRIME] = {"Grime", GRID_GRIME,
        0, 0, 0, 0, 0, 0, FILL_NONE, {{21, 9, 21, 25}, {0, 0, 0, 0}}, V_OCT, "0009000000090000", 1, BM_LIVE, LM_HOOK,
        "9000900000000000", 72, 0, 2, CP_FREE, 0},
    /* Drum and bass: a clean two-step under a long Reese bass, its cutoff on a slow LFO (the wobble's slow rate) */
    [GS_DNB] = {"Drum and bass", GRID_DNB,
        0, 0, 0, 0, 0, 0, FILL_DNB, {{28, 3, 28, 3}, {0, 0, 0, 0}}, V_WIDE, "9000000000000000", 0, BM_LAZY, LM_CALL,
        "9000000000000000", 72, 0, 2, CP_FREE, 0},
    /* Chicago jack: four on the floor, the jacking hat, one minor 7 vamp stabbed all phrase, a repeating bass riff */
    [GS_JACK] = {"Chicago jack", GRID_JACK,
        4, 0, 0, 0, 1, 0, FILL_NONE, {{0, 0, 0, 0}, {0, 0, 0, 0}}, V_TIGHT, "9009009000900900", 1, BM_ROBOT, LM_CALL,
        "0000000000000070", 72, 0, 2, CP_FREE, 0},
    /* Ghetto house / juke: kicks in 3-step groups, chopped chord stabs in 3s, an 808 locked to the kick */
    [GS_JUKE] = {"Ghetto house / juke", GRID_JUKE,
        0, 0, 0, 0, 0, 0, FILL_SNARE, {{21, 0, 21, 6}, {0, 0, 0, 0}}, V_TIGHT, "9009009009009000", 1, BM_ROOT, LM_HOOK,
        "9009009009000000", 72, 0, 2, CP_LOCK, 0},
    /* 0.4.3, batch [A] part 2: drums from app/styles/a2.grid */
    /* Aphex acid-electro: an electro beat under a 303 line whose cutoff drifts over 8 bars, sad wide pads */
    [GS_APHEX] = {"Aphex acid-electro", GRID_APHEX,
        2, 0, 0, 0, 1, 0, FILL_TOMS, {{3, 1, 21, 3}, {0, 7, 8, 9}}, V_WIDE, "9000000000000000", 0, BM_ACID, LM_CALL,
        "9000000000000000", 72, 0, 2, CP_FREE, 0},
    /* Boards of Canada haze: soft dusty drums, wide nostalgic chords and a slow lead that warble like worn tape */
    [GS_BOC] = {"Boards of Canada haze", GRID_BOC,
        3, 0, 0, 0, 2, 0, FILL_NONE, {{1, 13, 8, 1}, {0, 7, 8, 9}}, V_WIDE, "9000000000000000", 0, BM_LAZY, LM_CALL,
        "9000900090009000", 72, 0, 2, CP_FREE, 0},
    /* Autechre machine funk: the drums on the Grids map, drifting bar by bar (SFX_DRIFT), cold clusters, the bass
     * locked to the drifting kick */
    [GS_AUTECHRE] = {"Autechre machine funk", GRID_AUTECHRE,
        0, 0, 0, 0, 0, 0, FILL_NONE, {{25, 21, 13, 25}, {0, 7, 8, 9}}, V_WIDE, "9000000000000000", 0, BM_ROBOT, LM_CALL,
        "9090909090909090", 72, 0, 2, CP_LOCK, 0},
    /* Warp ambient techno: a gentle four on the floor, warm wide pads, the arp in 16ths over two octaves */
    [GS_WARP] = {"Warp ambient techno", GRID_WARP,
        0, 0, 0, 0, 0, 0, FILL_SNARE, {{1, 8, 13, 22}, {0, 7, 8, 9}}, V_WIDE, "9000000000000000", 0, BM_ROOT, LM_CALL,
        "9000000000000000", 76, 0, 1, CP_FREE, 0},
    /* 0.4.4, batch [B] part 1: drums from app/styles/b1.grid */
    /* Ballroom / vogue: the Ha-dance clap, off-beat crashes on the cymbal lane, a sparse kick; one stab every 2 bars */
    [GS_BALLROOM] = {"Ballroom / vogue", GRID_BALLROOM,
        0, 0, 0, 0, 0, 0, FILL_SNARE, {{21, 0, 21, 2}, {0, 0, 0, 0}}, V_OCT, "9000000000000000", 0, BM_ROOT, LM_CALL,
        "0000000000000070", 72, 0, 2, CP_LOCK, 0},
    /* Jersey club: the bouncing kick (its last three kicks as 8th-note triplets, SFX_TRIP), the rim squeaks, claps */
    [GS_JERSEY] = {"Jersey club", GRID_JERSEY,
        0, 0, 0, 0, 0, 0, FILL_SNARE, {{21, 6, 21, 2}, {0, 0, 0, 0}}, V_OCT, "9000000000000000", 0, BM_ROOT, LM_HOOK,
        "9000009000909090", 72, 0, 2, CP_LOCK, 0},
    /* Gqom: no kick on 1, no hats; the low tom takes the downbeat, congas broken around it; a minor drone */
    [GS_GQOM] = {"Gqom", GRID_GQOM,
        0, 0, 0, 0, 1, 0, FILL_TOMS, {{28, 28, 28, 28}, {0, 0, 0, 0}}, V_WIDE, "9000000000000000", 0, BM_LAZY, LM_CALL,
        "9000000000000000", 72, 0, 2, CP_FREE, 0},
    /* Kuduro: four on the floor, frantic toms, off-beat cowbell, a rave square riff, the bass locked to the kick */
    [GS_KUDURO] = {"Kuduro", GRID_KUDURO,
        0, 0, 0, 0, 0, 0, FILL_TOMS, {{21, 9, 21, 25}, {0, 0, 0, 0}}, V_OCT, "9000000000000000", 0, BM_ROOT, LM_HOOK,
        "9090009090900090", 72, 0, 2, CP_LOCK, 0},
    /* 0.4.5, batch [B] part 2 and batch [C]: drums from app/styles/b2.grid */
    /* Tribal guarachero: conga rolls and the 2-3 clave over a steady four on the floor; a high pentatonic flute call */
    [GS_TRIBAL] = {"Tribal guarachero", GRID_TRIBAL,
        0, 0, 0, 0, 1, 0, FILL_TOMS, {{21, 3, 21, 9}, {0, 0, 0, 0}}, V_WIDE, "9000000000000000", 0, BM_ROOT, LM_CALL,
        "9070007090000000", 79, 1, 2, CP_FOLLOW, 0},
    /* Detroit ghettotech: straight and fast, the 808 cowbell, a chant hook; the electro fill ends on a zap */
    [GS_GHETTOTECH] = {"Detroit ghettotech", GRID_GHETTOTECH,
        0, 0, 0, 0, 0, 0, FILL_ELECTRO, {{21, 11, 21, 9}, {0, 0, 0, 0}}, V_OCT, "9000000000000000", 0, BM_ROOT, LM_HOOK,
        "9070907000000000", 72, 0, 2, CP_FOLLOW, 0},
    /* Cumbia: the guira scrape on the closed hat, the bass on 1 and 3 (root, pickup, fifth: 0.5.1), accordion
     * off-beats in i-iv-V7 */
    [GS_CUMBIA] = {"Cumbia", GRID_CUMBIA,
        1, 0, 0, 0, 1, 0, FILL_LIVE, {{31, 31, 31, 31}, {0, 0, 0, 0}}, V_TIGHT, "0090009000900090", 1, BM_CUMBIA, LM_SOLO,
        "9090909090909090", 76, 0, 2, CP_FREE, 0},
    /* Latin freestyle: the syncopated kick, gated snares, zaps, brass stabs on off-16ths, a sad lead over a 16th arp */
    [GS_FREESTYLE] = {"Latin freestyle", GRID_FREESTYLE,
        0, 0, 0, 0, 0, 0, FILL_ELECTRO, {{4, 10, 4, 17}, {0, 0, 0, 0}}, V_WIDE, "0700070007000700", 1, BM_OCTAVE, LM_SOLO,
        "9000007000900070", 76, 0, 1, CP_FOLLOW, 0},
    /* 0.5.2, batch [E]: drums from app/styles/e.grid */
    /* Punk-funk (ESG, Liquid Liquid): a stiff kit under cowbell and conga, the bass ostinato held 8 bars (SFX_HOLD),
     * thin scratchy stabs on i7 against bII, the lead almost silent */
    [GS_PUNKFUNK] = {"Punk-funk", GRID_PUNKFUNK,
        0, 0, 0, 0, 1, 0, FILL_DROP, {{32, 32, 32, 32}, {0, 0, 0, 0}}, V_OCT, "0707000707000700", 1, BM_OCTAVE, LM_CALL,
        "0000000000000070", 76, 0, 2, CP_FREE, 0},
    /* Acid techno (Liberator, Hardfloor): a hard four on the floor, the 303 line held 8 bars for the hands on the
     * MB-33, its cutoff drifting over 4 bars on a synth with a filter CC (SFX_WOBBLE's acid rate) */
    [GS_ACIDTECH] = {"Acid techno", GRID_ACIDTECH,
        0, 0, 0, 0, 0, 0, FILL_DROP, {{28, 28, 28, 28}, {0, 0, 0, 0}}, V_OCT, "9000000000000000", 0, BM_ACID, LM_CALL,
        "0000000000000070", 72, 0, 2, CP_FREE, 0},
    /* Classic disco (Chic): a swung live four on the floor, off-beat open hats, chicken-scratch on i9-iv9, the harp
     * run as the strings going up, a free bass with octave jumps */
    [GS_DISCO] = {"Classic disco", GRID_DISCO,
        2, 0, 1, 0, 1, 0, FILL_DROP, {{33, 33, 33, 33}, {0, 0, 0, 0}}, V_TIGHT, "0707070707070707", 1, BM_LIVE, LM_CALL,
        "0000000000000070", 76, 0, 1, CP_FREE, 0},
};

/* the styles' families, the tempos they sit at (as set on the MPC: half-time styles are counted fast), whether they
 * suit the Tanzbär Lite (the picker can hide the others), effects (SFX_*), and the artists or scene */
#define SFX_CHOP 1u                      /* the phrase's last bar: beats 3 and 4 replay beats 1 and 2, every part */
#define SFX_BELL 2u                      /* the cowbell plays a line (batch [D]) */
#define SFX_PUMP 4u                      /* the chords pump: CC 11 dips on each kick (BRK) */
#define SFX_HARP 8u                      /* the arp plays a harp run on the phrase's first beat (BRK) */
#define SFX_SWEEP 16u                    /* the chords' cutoff sweeps over 16 bars (HSE) */
#define SFX_EDIT 32u                     /* break edits: beats replaced, reversed or rolled (Jungle) */
#define SFX_WOBBLE 64u                   /* the bass part's cutoff wobbles at a rate per bar (Dubstep) */
#define SFX_POLY 128u                    /* hats, tom and claves loop at their own lengths (Braindance) */
#define SFX_SECT 256u                    /* 8-bar sections: bass and stabs, then the lead (Grime) */
#define SFX_WARBLE 512u                  /* the chords' and lead's pitch drifts like worn tape (Boards of Canada) */
#define SFX_DRIFT 1024u                  /* the drums on the Grids map, its X/Y drifting bar by bar (Autechre) */
#define SFX_TRIP 2048u                   /* the bar's last three kicks as 8th-note triplets (Jersey club) */
#define SFX_HOLD 4096u                   /* the bass riff held unchanged for 8 bars, Mutate only at 8-bar lines (0.5.2) */
static const struct { uint8_t cat, lo, hi, tanz; uint16_t fx; const char *ref; } STYLE_META[GEN_NSTYLES] = {
    [GS_DILLA] = {CAT_HIP, 80, 95, 1, 0, "J Dilla, Slum Village"},
    [GS_SOULQ] = {CAT_HIP, 80, 95, 0, 0, "D'Angelo, Questlove"},
    [GS_GFUNK] = {CAT_HIP, 88, 100, 1, 0, "Dr. Dre, West Coast"},
    [GS_BOMB] = {CAT_HIP, 94, 112, 0, 0, "Public Enemy"},
    [GS_QUINCY] = {CAT_RNB, 100, 125, 1, 0, "Off the Wall era"},
    [GS_JUSTIFIED] = {CAT_RNB, 90, 120, 1, 0, "Justin Timberlake, Neptunes"},
    [GS_MPLS] = {CAT_RNB, 100, 130, 1, 0, "Prince, The Time"},
    [GS_NPG] = {CAT_RNB, 85, 110, 0, 0, "Prince and the NPG"},
    [GS_NJS] = {CAT_RNB, 100, 115, 1, 0, "Teddy Riley"},
    [GS_BOOGIE] = {CAT_RNB, 105, 125, 1, 0, "80s post-disco"},
    [GS_PFUNK] = {CAT_FNK, 90, 115, 0, 0, "Parliament, Bootsy"},
    [GS_JAZZFUNK] = {CAT_FNK, 90, 115, 1, 0, "Herbie Hancock"},
    [GS_ELECTRO] = {CAT_ELC, 110, 130, 1, 0, "Afrika Bambaataa, Egyptian Lover"},
    [GS_ELECTRO2] = {CAT_ELC, 120, 140, 1, 0, "Drexciya, Dopplereffekt"},
    [GS_WONKY] = {CAT_BRK, 85, 130, 1, 0, "Hudson Mohawke, Rustie"},
    [GS_BIGBEAT] = {CAT_BRK, 100, 135, 0, 0, "Chemical Brothers, Fatboy Slim"},
    [GS_BRAINDANCE] = {CAT_IDM, 150, 190, 0, 0, "Squarepusher, Venetian Snares"},
    [GS_DETROIT] = {CAT_HSE, 120, 135, 1, 0, "Juan Atkins, Derrick May"},
    [GS_808] = {CAT_ELC, 110, 130, 1, 0, "Mantronix"},
    [GS_GLITCH] = {CAT_BRK, 95, 115, 0, 0, "Tipper"},
    [GS_HALFTIME] = {CAT_BAS, 140, 170, 1, 0, "halftime drum and bass"},
    [GS_FLAT] = {CAT_BRK, 105, 125, 1, 0, "Mr. Oizo"},
    [GS_RAVE] = {CAT_BRK, 135, 165, 0, 0, "The Prodigy"},
    [GS_ACIDDREAM] = {CAT_HSE, 120, 140, 1, 0, "Phuture, 303"},
    [GS_BOOMBAP] = {CAT_HIP, 85, 98, 1, 0, "Cut-A-Dor drums"},
    [GS_NEP_MIN] = {CAT_HIP, 85, 105, 1, 0, "Grindin'"},
    [GS_NEP_CLICK] = {CAT_HIP, 78, 95, 1, 0, "Drop It Like It's Hot"},
    [GS_NEP_CLUB] = {CAT_HIP, 100, 110, 1, 0, "Hot in Herre"},
    [GS_NEP_SYNTH] = {CAT_HIP, 100, 115, 1, 0, "Milkshake, Frontin'"},
    [GS_NEP_DARK] = {CAT_HIP, 75, 100, 1, 0, "Clipse, Hell Hath No Fury"},
    [GS_NEP_STOMP] = {CAT_HIP, 105, 115, 1, 0, "Hollaback Girl"},
    [GS_NERD] = {CAT_HIP, 95, 130, 0, 0, "N.E.R.D."},
    [GS_DUB] = {CAT_WLD, 65, 85, 1, 0, "King Tubby, Lee Perry"},
    [GS_MEMPHIS] = {CAT_HIP, 130, 150, 1, 0, "Three 6 Mafia"},
    [GS_TRAP] = {CAT_HIP, 130, 160, 1, 0, "Atlanta"},
    [GS_NATIVE] = {CAT_HIP, 88, 100, 0, 0, "A Tribe Called Quest, De La Soul"},
    [GS_WUTANG] = {CAT_HIP, 85, 98, 0, 0, "RZA"},
    [GS_MADLIB] = {CAT_HIP, 80, 100, 0, 0, "Stones Throw"},
    [GS_ABSTRACT] = {CAT_HIP, 85, 100, 0, 0, "El-P, Company Flow"},
    [GS_OLDSCHOOL] = {CAT_HIP, 95, 115, 1, 0, "Run-DMC, Def Jam"},
    [GS_BOUNCE] = {CAT_HIP, 95, 105, 1, 0, "Triggerman beat"},
    [GS_CHOPPED] = {CAT_HIP, 60, 75, 1, SFX_CHOP, "DJ Screw, Houston"},
    [GS_BELLFUNK] = {CAT_ELC, 108, 124, 1, SFX_BELL, "Bambaataa-era bell riff"},
    [GS_MIAMIBELL] = {CAT_ELC, 120, 135, 1, SFX_BELL, "bell-hook bass music"},
    [GS_DETBELL] = {CAT_HSE, 125, 135, 1, SFX_BELL, "looping bell melody"},
    [GS_AFROBELL] = {CAT_FNK, 100, 118, 1, SFX_BELL, "Afrobeat bell over a funk groove"},
    [GS_TALKBOX] = {CAT_FNK, 95, 112, 1, 0, "Zapp, Roger Troutman"},
    [GS_MODBOOGIE] = {CAT_FNK, 95, 115, 1, 0, "Dam-Funk"},
    [GS_GOGO] = {CAT_FNK, 95, 120, 1, 0, "Chuck Brown, Washington DC"},
    [GS_BLAX] = {CAT_FNK, 90, 115, 0, 0, "Isaac Hayes, Curtis Mayfield"},
    [GS_TIMBALAND] = {CAT_RNB, 90, 110, 1, 0, "Aaliyah, Missy Elliott"},
    [GS_GATED] = {CAT_RNB, 90, 125, 1, 0, "Phil Collins, LinnDrum, Simmons"},
    [GS_KRAFTWERK] = {CAT_ELC, 100, 130, 1, 0, "Euro-electro"},
    [GS_ITALO] = {CAT_ELC, 110, 130, 1, 0, "Giorgio Moroder"},
    [GS_MIAMI] = {CAT_ELC, 115, 135, 1, 0, "2 Live Crew, freestyle"},
    [GS_LABEATS] = {CAT_BRK, 75, 110, 0, SFX_PUMP | SFX_HARP, "Flying Lotus, Brainfeeder"},
    [GS_TRIPHOP] = {CAT_BRK, 70, 100, 0, 0, "Massive Attack, Portishead"},
    [GS_BROKEN] = {CAT_BRK, 105, 125, 1, 0, "West London bruk"},
    [GS_MONTREAL] = {CAT_BRK, 100, 125, 1, 0, "Kaytranada"},
    [GS_FRENCH] = {CAT_HSE, 115, 130, 1, SFX_SWEEP, "Daft Punk, filter house"},
    [GS_DUBTECH] = {CAT_HSE, 110, 130, 1, SFX_SWEEP, "Basic Channel"},
    [GS_JUNGLE] = {CAT_BAS, 160, 175, 0, SFX_EDIT, "Amen break, ragga"},
    [GS_UKG] = {CAT_BAS, 130, 140, 1, 0, "Artful Dodger, MJ Cole"},
    [GS_DUBSTEP] = {CAT_BAS, 138, 142, 1, SFX_WOBBLE, "Skream, Benga"},
    [GS_REPHLEX] = {CAT_IDM, 90, 140, 1, SFX_POLY, "Rephlex"},
    [GS_DANCEHALL] = {CAT_WLD, 85, 105, 1, 0, "Sleng Teng era, digital"},
    [GS_REGGAETON] = {CAT_WLD, 90, 100, 1, 0, "Dembow, Daddy Yankee era"},
    [GS_REGGAETON2] = {CAT_WLD, 85, 100, 1, 0, "Dembow, today"},
    [GS_AFROBEAT] = {CAT_WLD, 100, 130, 1, 0, "Fela Kuti"},
    [GS_BAILE] = {CAT_WLD, 125, 135, 1, 0, "Rio tamborzão"},
    [GS_GRIME] = {CAT_BAS, 138, 142, 1, SFX_SECT, "Wiley, Dizzee Rascal"},
    [GS_DNB] = {CAT_BAS, 170, 178, 0, SFX_WOBBLE, "Goldie, Reese bass"},
    [GS_JACK] = {CAT_HSE, 118, 128, 1, 0, "Trax Records"},
    [GS_JUKE] = {CAT_HSE, 125, 160, 1, 0, "Dance Mania, footwork"},
    [GS_APHEX] = {CAT_IDM, 110, 130, 1, SFX_WOBBLE, "Aphex Twin, Analord"},
    [GS_BOC] = {CAT_IDM, 85, 100, 1, SFX_WARBLE, "Boards of Canada"},
    [GS_AUTECHRE] = {CAT_IDM, 90, 120, 1, SFX_DRIFT, "Autechre"},
    [GS_WARP] = {CAT_IDM, 120, 135, 1, 0, "Warp, Artificial Intelligence"},
    [GS_BALLROOM] = {CAT_HSE, 120, 130, 1, 0, "Ha dance, vogue"},
    [GS_JERSEY] = {CAT_HSE, 130, 140, 1, SFX_TRIP, "Jersey club"},
    [GS_GQOM] = {CAT_WLD, 120, 127, 1, 0, "Durban, South Africa"},
    [GS_KUDURO] = {CAT_WLD, 130, 140, 1, 0, "Buraka Som Sistema, Angola"},
    [GS_TRIBAL] = {CAT_WLD, 125, 135, 1, 0, "3Ball MTY, Mexico"},
    [GS_GHETTOTECH] = {CAT_ELC, 145, 160, 1, 0, "DJ Assault, DJ Godfather"},
    [GS_CUMBIA] = {CAT_WLD, 85, 110, 1, 0, "Colombia, digital cumbia"},
    [GS_FREESTYLE] = {CAT_ELC, 112, 128, 1, 0, "Shannon, Lisa Lisa, Stevie B"},
    [GS_PUNKFUNK] = {CAT_FNK, 105, 125, 1, SFX_HOLD, "ESG, Liquid Liquid, A Certain Ratio"},
    [GS_ACIDTECH] = {CAT_HSE, 135, 150, 1, SFX_HOLD | SFX_WOBBLE, "Liberator, Hardfloor"},
    [GS_DISCO] = {CAT_RNB, 110, 125, 1, SFX_HARP, "Chic, Philadelphia"},
};
static const char *const CAT_NAME[GEN_NCATS] = {"Hip-hop", "Funk", "R&B and pop", "Electro", "Breaks and beats",
                                                "House and techno", "UK bass", "IDM", "Caribbean and world"};
static const char *const CAT_CODE[GEN_NCATS] = {"HIP", "FNK", "RNB", "ELC", "BRK", "HSE", "BAS", "IDM", "WLD"};

/* bass presets: the rhythm, then percentages: octave, fifth, colour (3rd, 7th), passing, chromatic approach, slide,
 * accent, ghost; the gate (0 short, 1 medium, 2 to the next note); late (subticks); a run at the phrase's end */
typedef struct { const char *rhy; uint8_t riff, oct, fifth, color, pass, appr, slide, acc, ghost, gate; int8_t late;
                 uint8_t run; } bass_t;
static const bass_t BASS[NBM] = {
    [BM_ROOT] = {"9002007090025030", 2, 10, 25, 10, 5, 20, 5, 15, 0, 1, 0, 10},
    [BM_ACID] = {"9686978697869787", 1, 30, 10, 25, 10, 5, 30, 30, 0, 0, 0, 0},
    [BM_BOOTSY] = {"9000002006050370", 2, 40, 10, 20, 5, 25, 15, 35, 30, 0, 1, 30},
    [BM_WORRELL] = {"9007060070607050", 2, 15, 15, 20, 40, 30, 45, 15, 5, 2, 0, 70},
    [BM_HERBIE] = {"9070030790700370", 1, 20, 15, 30, 10, 50, 10, 20, 10, 1, 0, 20},
    [BM_QUINCY] = {"9070979070709790", 1, 30, 10, 15, 5, 15, 5, 20, 0, 0, 0, 10},
    [BM_GLIDE] = {"9000000070000030", 2, 20, 20, 10, 10, 10, 65, 5, 0, 2, 0, 20},
    [BM_ROBOT] = {"9090009090900090", 1, 50, 10, 5, 0, 5, 0, 20, 0, 0, 0, 0},
    [BM_LAZY] = {"9000000600030000", 2, 10, 20, 25, 15, 30, 25, 5, 0, 2, 4, 20},
    [BM_LIVE] = {"9030607090306075", 2, 35, 15, 20, 30, 40, 15, 25, 30, 1, 1, 50},
    [BM_OCTAVE] = {"9070907090709070", 1, 0, 5, 5, 0, 10, 0, 15, 0, 0, 0, 10},
    [BM_WOBBLE] = {"9000000090000030", 2, 10, 20, 10, 0, 10, 30, 0, 0, 2, 2, 0},
    [BM_TURING] = {"9797979797979797", 1, 0, 0, 0, 0, 0, 15, 20, 0, 1, 0, 0},
    [BM_DUB] = {"9009000000907050", 2, 5, 30, 20, 15, 10, 10, 5, 0, 1, 1, 20},   /* low, few notes, its own tune */
    /* 0.5.1: the root on 1, an 8th-note pickup on the "and" of 2, the fifth on 3 (bass_riff forces the three) */
    [BM_CUMBIA] = {"9000007090000000", 1, 0, 0, 0, 0, 10, 0, 20, 0, 1, 0, 15},
};

static const char *const PART_NAME[GEN_NPARTS] = {"Drums", "Chords", "Arp", "Bass", "Lead", "Kick", "Marble"};
static const char *const RM_NAME[NRM] = {"Chord", "Penta", "Scale"};
static const char *const LANE_NAME[GEN_NLANES] = {"Kick", "Snare", "Rim", "Clap", "Closed hat", "Open hat", "Low tom",
                                                  "High tom", "Low conga", "High conga", "Cowbell", "Claves", "Cymbal",
                                                  "Simmons 1", "Simmons 2"};
static const char *const DSET_NAME[GEN_NDSET] = {"All", "Kit", "Simmons", "Kick", "No kick"};
static const char *const DM_NAME[DM_CAD0] = {"Style", "Grids", "Turing"};   /* (then Cut-A-Dor's: CAD_NAME) */
static const char *const CM_NAME[NCM] = {"Style", "Pad", "Stabs"};
static const char *const AM_NAME[NAM] = {"Up", "Down", "Up-down", "Random", "Converge", "Played"};
static const char *const BM_NAME[NBM] = {"Style", "Root", "Acid", "Bootsy", "Worrell", "Herbie", "Quincy", "Glide",
                                         "Robot", "Lazy", "Live", "Octave", "Wobble", "Turing", "Dub",
                                         "Cumbia"};
static const char *const LM_NAME[NLM] = {"Style", "Hook", "Call+resp", "Solo", "Whine", "Turing"};
static const char *const KK_NAME[NKK] = {"Style", "4 floor", "808"};
static const uint8_t NMODES[GEN_NPARTS] = {NDM, NCM, NAM, NBM, NLM, NKK, NRM};
/* the lanes' notes: the MFB Tanzbar Lite's (one tom and one conga note each), and the MPC pads that trigger the
 * Simmons (A01 37, A02 36: the same numbers as the Tanzbar's snare and kick, so they go out by themselves, Lanes) */
static const uint8_t LANE_DEFAULT[GEN_NLANES] = {36, 37, 38, 41, 43, 39, 40, 40, 44, 44, 45, 46, 47, 37, 36};
static const uint8_t PHRASE_BARS[4] = {1, 2, 4, 8};

/* -------------------------------------------------------------- state --- */
/* Two buffers of a bar's events: the one playing (the tick only reads it) and the one gen_service writes in the
 * main loop: the next bar, or this bar again after a setting changed. The tick never builds anything. */
#ifdef GEN_STANDALONE
#define GEN_LOCK() 0u
#define GEN_UNLOCK(f) (void)(f)
#else
#define GEN_LOCK() plat_lock()
#define GEN_UNLOCK(f) plat_unlock(f)
#endif
#define GEN_POOL 496
static const uint16_t GEV_OFF[GEN_NPARTS + 1] = {0, 128, 224, 264, 360, 456, 472, GEN_POOL};   /* a part's share */
typedef struct {
    gev_t ev[GEN_POOL];
    uint16_t n[GEN_NPARTS];
    uint16_t lanes[GEN_NLANES];          /* the drum part's hits, a bit a sixteenth (for the screen and the keys) */
    uint32_t bar, sig;                   /* the bar it holds, and the settings it was made from */
    uint8_t ok;
} gbuf_t;
static gbuf_t gb[2];
static gbuf_t *wb;                       /* the buffer being written */
static uint32_t bb;                      /* the bar being written */
static gon_t gon[GEN_NPARTS][GEN_MAXON];
static struct {
    gen_cfg_t *c;
    gen_out_t out;
    uint8_t run, cur;                    /* cur: the buffer playing */
    uint16_t pos;                        /* the subtick of the bar */
    uint32_t bar;                        /* bars since the start */
    int8_t man_pc;                       /* prog = keys: the chord's pitch class, -1 none yet */
    uint16_t i[GEN_NPARTS];              /* each part's next event */
    uint8_t moved[GEN_NPARTS];           /* bit 0: the controller, bit 1: the bend is away from rest */
} g;

/* ------------------------------------------------------------ helpers --- */
static int gclamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static uint32_t gh(uint32_t a, uint32_t b)
{
    a ^= b + 0x9E3779B9u + (a << 6) + (a >> 2);
    a ^= a >> 16;
    a *= 0x7FEB352Du;
    a ^= a >> 15;
    a *= 0x846CA68Bu;
    a ^= a >> 16;
    return a;
}
static uint32_t rnext(rng_t *r)
{
    uint32_t s = r->s ? r->s : 0x1234567u;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return r->s = s;
}
static uint32_t rnd(rng_t *r, uint32_t n) { return n ? (rnext(r) >> 8) % n : 0u; }
static int chance(rng_t *r, int pct) { return (int)rnd(r, 100) < pct; }
static const style_t *sty(void) { return &STYLE[g.c->style % GEN_NSTYLES]; }
static const gdev_t *devp(int p) { return &DEV[g.c->part[p].dev % GEN_NDEV]; }
static int phrase_len(void) { return PHRASE_BARS[g.c->phrase & 3]; }
static uint32_t epoch(void)              /* 0 while the phrase repeats exactly */
{
    return g.c->mutate ? bb / (uint32_t)phrase_len() / (uint32_t)(9 - gclamp(g.c->mutate, 1, 8)) : 0u;
}
static uint32_t pseed(int p, uint32_t salt)   /* the part's seed: the song, the style, its variation */
{
    return gh(gh(g.c->seed * 65537u + g.c->style, (uint32_t)p * 131u + g.c->part[p].var), salt);
}
static int wdec(char c, int *ghost)
{
    *ghost = c >= 'a' && c <= 'e';
    return *ghost ? c - 'a' + 5 : c >= '0' && c <= '9' ? c - '0' : 0;
}
static int wlevel(int w) { return w >= 9 ? 255 : w * 28; }
static int feel(int v) { return v * g.c->feel / 8; }
/* the off sixteenths' delay: the style's own (by Feel), or Swing as an MPC counts it, 50 % straight to 75 %:
 * the off sixteenth that far through its pair (66 % a triplet) */
static int swing_sub(void)
{
    return g.c->swing ? (g.c->swing - 1) * 2 * (int)GEN_STEP / 100 : feel(sty()->sw);
}
/* a sixteenth's subtick: swing (sw) on the off sixteenths, the lane's lateness and jitter, by Feel */
static int tpos_sw(int step, int sw, int late, int jit, rng_t *r)
{
    int p = step * (int)GEN_STEP + ((step & 1) ? sw : 0) + feel(late), j = feel(jit);
    if (j > 0)
        p += (int)rnd(r, 2u * (uint32_t)j + 1u) - j;
    return gclamp(p, 0, (int)GEN_BAR - 1);
}
static int tpos(int step, int late, int jit, rng_t *r) { return tpos_sw(step, swing_sub(), late, jit, r); }

/* ------------------------------------------------------------- events --- */
static void gput_ev(int p, int pos, int kind, int a, int b, int dur, int ch)
{
    gev_t *e;
    if (wb->n[p] >= GEV_OFF[p + 1] - GEV_OFF[p])
        return;
    e = &wb->ev[GEV_OFF[p] + wb->n[p]++];
    e->pos = (uint16_t)gclamp(pos, 0, (int)GEN_BAR - 1);
    e->dur = (uint16_t)gclamp(dur, 1, 4 * (int)GEN_BAR);
    e->kind = (uint8_t)kind;
    e->a = (uint8_t)(a & 127);
    e->b = (uint8_t)(b & 127);
    e->ch = (uint8_t)(ch & 15);
}
static int fold(int p, int n)             /* into the device's range, by octaves */
{
    const gdev_t *d = devp(p);
    while (n < d->lo)
        n += 12;
    while (n > d->hi)
        n -= 12;
    return gclamp(n, 0, 127);
}
static void gput_note(int p, int pos, int note, int vel, int dur)
{
    gput_ev(p, pos, EV_NOTE, fold(p, note), gclamp(vel, 1, 127), dur, g.c->part[p].chan);
}
static void gput_cc(int p, int pos, int v)
{
    if (devp(p)->cc)
        gput_ev(p, pos, EV_CC, devp(p)->cc, gclamp(v, 0, 127), 1, g.c->part[p].chan);
}
static int bend_range(int p)
{
    int b = devp(p)->bend;
    return b == 255 ? gclamp(g.c->bend, 1, 12) : b;
}
static void gput_bend(int p, int pos, int quarter)   /* in quarter semitones */
{
    int rg = bend_range(p), v;
    if (!rg)
        return;
    v = gclamp(8192 + quarter * 2048 / rg, 0, 16383);
    gput_ev(p, pos, EV_BEND, v & 127, v >> 7, 1, g.c->part[p].chan);
}
static void ramp_cc(int p, int p0, int p1, int v0, int v1, int n)
{
    int i;
    for (i = 0; i < n; i++)
        gput_cc(p, p0 + (p1 - p0) * i / (n > 1 ? n - 1 : 1), v0 + (v1 - v0) * i / (n > 1 ? n - 1 : 1));
}
static void ramp_bend(int p, int p0, int p1, int q0, int q1, int n)
{
    int i;
    for (i = 0; i < n; i++)
        gput_bend(p, p0 + (p1 - p0) * i / (n > 1 ? n - 1 : 1), q0 + (q1 - q0) * i / (n > 1 ? n - 1 : 1));
}
static void sort_ev(int p)
{
    gev_t *v = wb->ev + GEV_OFF[p];
    int i, j, n = wb->n[p];
    for (i = 1; i < n; i++) {
        gev_t t = v[i];
        uint32_t k = (uint32_t)t.pos << 2 | t.kind;
        for (j = i; j > 0 && ((uint32_t)v[j - 1].pos << 2 | v[j - 1].kind) > k; j--)
            v[j] = v[j - 1];
        v[j] = t;
    }
}

/* ------------------------------------------------------------ harmony --- */
static const prog_t *cur_prog(void)
{
    const style_t *s = sty();
    int m = g.c->minor ? 0 : 1, i = s->prog[m][gh(g.c->seed, 0x50u + g.c->style) & 3];
    return m ? &PROG_MAJ[i % (int)(sizeof PROG_MAJ / sizeof PROG_MAJ[0])]
             : &PROG_MIN[i % (int)(sizeof PROG_MIN / sizeof PROG_MIN[0])];
}
static uint16_t rot12(uint16_t m, int by) { by = ((by % 12) + 12) % 12; return (uint16_t)(((m << by) | (m >> (12 - by))) & 0xFFFu); }
static uint16_t key_mask(void) { return rot12(SC_MASK[cur_prog()->scale], g.c->root); }
static uint16_t chord_mask(chord_t c)
{
    uint16_t m = 0;
    int i;
    for (i = 0; i < QN[c.q]; i++)
        m |= (uint16_t)(1u << ((c.root + QIV[c.q][i]) % 12));
    return m;
}
static chord_t chord_at(uint32_t bar, int half)
{
    const prog_t *pr = cur_prog();
    chord_t c;
    int i, tot = 0, h;
    if (g.c->prog && g.man_pc >= 0) {     /* the keys: the chord the key's scale makes on that note */
        uint16_t km = key_mask();
        int r = g.man_pc, maj3 = km >> ((r + 4) % 12) & 1, min3 = km >> ((r + 3) % 12) & 1;
        int b7 = km >> ((r + 10) % 12) & 1, p5 = km >> ((r + 7) % 12) & 1;
        c.root = (uint8_t)r;
        c.q = (uint8_t)(maj3 ? (b7 ? Q_9 : Q_MAJ7) : min3 ? (!p5 ? Q_M7B5 : b7 ? Q_MIN7 : Q_MIN) : Q_7SUS);
        return c;
    }
    for (i = 0; i < pr->n; i++)
        tot += pr->c[i].len;
    h = (int)((bar * 2u + (uint32_t)half) % (uint32_t)tot);
    for (i = 0; i < pr->n - 1 && h >= pr->c[i].len; i++)
        h -= pr->c[i].len;
    c.root = (uint8_t)((g.c->root + pr->c[i].semi) % 12);
    c.q = pr->c[i].q;
    return c;
}
static chord_t chord_step(int step) { return chord_at(bb, step >= 8); }
static chord_t chord_after(int step) { return step >= 8 ? chord_at(bb + 1u, 0) : chord_at(bb, 1); }
/* the notes a melody may use over a chord: its tones, and the key's that don't sit a semitone above one */
static uint16_t mel_mask(chord_t c, int pent)
{
    uint16_t cm = chord_mask(c), km = key_mask();
    if (pent)
        km &= rot12(g.c->minor ? PENT_MIN : PENT_MAJ, g.c->root);
    return (uint16_t)(cm | (km & ~rot12(cm, 1)));
}
static int in_mask(int note, uint16_t m) { return m >> (((note % 12) + 12) % 12) & 1; }
static int ladder(int note, int n, uint16_t m)   /* n allowed notes up (or down) from note */
{
    int d = n < 0 ? -1 : 1, guard = 0;
    if (!m)
        return note;
    while (!in_mask(note, m) && guard++ < 12)
        note += d ? d : 1;
    n = n < 0 ? -n : n;
    while (n > 0 && note > 0 && note < 127) {
        note += d;
        if (in_mask(note, m))
            n--;
    }
    return gclamp(note, 0, 127);
}
static int nearest(int note, uint16_t m)
{
    int d;
    for (d = 0; d < 7; d++) {
        if (in_mask(note - d, m))
            return note - d;
        if (in_mask(note + d, m))
            return note + d;
    }
    return note;
}
static int root_note(chord_t c, int base) { return base + ((c.root - base % 12) + 12) % 12; }

/* ---------------------------------------------------------------- drums --- */
static int lane_late(int l)
{
    const style_t *s = sty();
    return l == L_KICK ? s->lk : l == L_SNARE || l == L_CLAP || l == L_RIM || l == L_SIM2 ? s->ls : s->lh;
}
static int lane_ch(int l)
{
    return (l == L_SIM1 || l == L_SIM2) && g.c->sim_chan ? g.c->sim_chan - 1 : g.c->part[GP_DRUMS].chan;
}
static int lane_sent(int l)
{
    int sim = l == L_SIM1 || l == L_SIM2;
    switch (g.c->dset) {
    case DS_KIT: return !sim;
    case DS_SIM: return sim;
    case DS_KICK: return l == L_KICK;
    case DS_NOKICK: return l != L_KICK;
    default: return 1;
    }
}
static int is_hat(int l) { return l == L_CHH || l == L_OHH; }
/* the hats' share of a swing: Hat swing takes some of it off */
static int hat_sw(int sw) { return sw * (100 - gclamp(g.c->hat_straight, 0, 100)) / 100; }
/* a drum lane's sixteenth: as tpos, with the lane's swing */
static int dpos(int l, int step, int late, int jit, rng_t *r)
{
    return tpos_sw(step, is_hat(l) ? hat_sw(swing_sub()) : swing_sub(), late, jit, r);
}
/* Pocket (0.3.4): Cut-A-Dor's per-position pocket (its Boom Bap's) for the styles' own drums, in 96ths of a quarter,
 * scaled like its Human by Feel (8: as shipped): off-beat kicks a little late, the backbeat late, ghost snares
 * early, off-sixteenth hats late and the ones between beats early, claps late, percussion around the step */
static int style_pocket(int l, int s, int v)
{
    int f;
    if (l == L_KICK)
        f = s % 4 == 0 ? 0 : 1;
    else if (l == L_SNARE)
        f = v < 65 ? -2 : 3;
    else if (is_hat(l))
        f = s % 2 ? 2 : (s % 4 == 2 ? -3 : -1);
    else if (l == L_CLAP)
        f = 4;
    else
        f = (s * 3 + l * 5 + (int)(g.c->seed & 3u)) % 7 - 3;
    return f * (g.c->feel * 65 / 8) / 100;
}
static void drum_put(int l, int pos, int vel)
{
    if (!lane_sent(l))
        return;
    gput_ev(GP_DRUMS, pos, EV_NOTE, g.c->lane_note[l], gclamp(vel, 1, 127), 8, lane_ch(l));
    wb->lanes[l] |= (uint16_t)(1u << (gclamp(pos, 0, (int)GEN_BAR - 1) / (int)GEN_STEP));
}
/* a hit; Flam: a grace note before some of the snare's loud ones (which ones: by the seed, the bar and the place) */
static void drum_hit(int l, int pos, int vel)
{
    if (l == L_SNARE && g.c->flam && vel >= 96 && pos >= 8 &&
        (int)(gh(pseed(GP_DRUMS, 0x900u), bb * 512u + (uint32_t)pos) % 127u) < g.c->flam)
        drum_put(l, pos - 4 - g.c->flam / 40, vel * 45 / 100);
    drum_put(l, pos, vel);
}
/* Hat density: the closed hat's steps (0: as the pattern has them) */
static int hat_keep(int s)
{
    return g.c->hat_dens == 1 ? (s & 3) == 0 : g.c->hat_dens == 2 ? (s & 1) == 0 : 1;
}
/* the fill: is this bar one? At the phrase's end, never, or every 1, 2, 4, 8, 16 bars */
static int fill_bar(uint32_t bar)
{
    int pl = phrase_len(), fe = g.c->fill_every;
    uint32_t n;
    if (fe == 1)
        return 0;
    if (fe == 0)
        return pl > 1 && (int)(bar % (uint32_t)pl) == pl - 1;
    n = 1u << (fe - 2);
    return bar % n == n - 1u;
}
/* which of Cut-A-Dor's styles the drums are (-1: none): the drum part's mode, or the style's own */
static int drum_cad(void)
{
    int m = g.c->part[GP_DRUMS].mode;
    return m >= DM_CAD0 && m < NDM ? m - DM_CAD0 : m == DM_STYLE && sty()->cad ? sty()->cad - 1 : -1;
}
/* its two bars, from the seed, the drum part's Density, Space; Mutate: the last four of the changes made as the
 * phrase repeats, so it moves on a little at a time. Kept until a setting it is made from changes. */
static struct { cad_pat_t pat; uint32_t key; uint8_t ok; } cadc;
static const cad_pat_t *cad_pat(int cs)
{
    int dens = gclamp(g.c->part[GP_DRUMS].dens * 100 / 104, 0, 100), space = gclamp(g.c->space, 0, 100);
    uint32_t seed = pseed(GP_DRUMS, 0xCADu), ep = epoch(), e;
    uint32_t key = gh(gh(seed, (uint32_t)cs * 7919u + (uint32_t)dens * 131u + (uint32_t)space), ep);
    if (!cadc.ok || cadc.key != key) {
        cad_generate(&cadc.pat, cs, dens, space, seed);
        for (e = ep > 3u ? ep - 3u : 1u; e <= ep; e++)
            cad_mutate(&cadc.pat, cs, space, seed + e * 0x9e3779b9u);
        cadc.key = key;
        cadc.ok = 1;
    }
    return &cadc.pat;
}
/* a lane's step in the style: 0 no hit, else its velocity. The same for the drum part and the melodic kick. */
static int style_hit(int l, int step, int dens)
{
    const char *str = sty()->lane[l];
    int ghost, w, lev;
    uint32_t pb = bb % (uint32_t)phrase_len();
    rng_t r;
    if (!str)
        return 0;
    w = wdec(str[step], &ghost);
    if (!w)
        return 0;
    lev = wlevel(w);
    if (w < 9) {                         /* the variation: which of the lighter hits; and a little per bar */
        lev += (int)(gh(pseed(GP_DRUMS, 0x100u + (uint32_t)l), (uint32_t)step) % 81u) - 40;
        lev += (int)(gh(pseed(GP_DRUMS, 0x200u + (uint32_t)l * 16u + (uint32_t)step), pb + epoch() * 64u) % 25u) - 12;
    }
    if (lev < 255 - dens * 2)
        return 0;
    r.s = gh(pseed(GP_DRUMS, 0x300u + (uint32_t)l), bb * 16u + (uint32_t)step);
    if (ghost)
        return 30 + (int)rnd(&r, 20);
    return (w >= 8 ? 108 : 82) + (int)rnd(&r, 14) + ((step & 3) == 0 ? 4 : 0);
}
static uint32_t grids_level(uint32_t step, uint32_t ch, uint32_t x, uint32_t y);
/* Turing mode's shift register for lane k (0 kick … 4 rim) this bar: sixteen bits, a few flipped each bar of the
 * phrase (Mutate: more of them). The drum part and kick_src make the same one. */
static const uint8_t TURING_PCT[5] = {26, 14, 62, 8, 12};       /* percent of steps on */
static uint16_t turing_reg(int k, int dens)
{
    int pb = (int)(bb % (uint32_t)phrase_len()), s, b;
    uint16_t reg = 0;
    rng_t q;
    q.s = pseed(GP_DRUMS, 0x400u + (uint32_t)k);
    for (s = 0; s < 16; s++)
        if ((int)rnd(&q, 100) < TURING_PCT[k] * (40 + dens) / 104)
            reg |= (uint16_t)(1u << s);
    if (k == 0)
        reg |= 1u;
    if (k == 1)
        reg |= 1u << 4 | 1u << 12;
    for (b = 0; b <= pb; b++) {
        int flips = b ? 1 + g.c->mutate / 3 : 0;
        q.s = gh(pseed(GP_DRUMS, 0x500u + (uint32_t)k), (uint32_t)b + epoch() * 64u);
        while (flips--)
            reg ^= (uint16_t)(1u << rnd(&q, 16));
    }
    return reg;
}
/* 0.5.3: a lane's density group, and the drums' Density for it (the global one plus the group's offset) */
static int lane_grp(int l)
{
    return l == L_KICK ? GEN_GRP_KICK : l == L_SNARE || l == L_CLAP || l == L_RIM ? GEN_GRP_SNARE : GEN_GRP_PERC;
}
static int grp_dens(int l, int dens) { return gclamp(dens + g.c->dens_grp[lane_grp(l)], 0, 127); }
/* Grids (0.5.3, the module's controls): how many slots a bar holds at this clock, and their length in subticks;
 * the map's 32 steps are 32nds at the normal clock; Euclidean mode counts sixteenths (eighths at half speed, 32nds
 * at double) */
static int grids_slots(int *len)
{
    int r = g.c->g_res > 2 ? 1 : g.c->g_res, u = g.c->g_euclid ? (r == 0 ? 48 : r == 1 ? 24 : 12) : (r == 0 ? 24 : r == 1 ? 12 : 6);
    *len = u;
    return (int)GEN_BAR / u;
}
/* a Grids voice (0 BD, 1 SD, 2 HH) on slot u of this bar: 0, or its velocity; *open: a hat for the open hat */
static int grids_at(int ch, int u, uint32_t pert, int dens, int *open)
{
    uint32_t x = (uint32_t)gclamp(g.c->gx, 0, 127) * 2u, y = (uint32_t)gclamp(g.c->gy, 0, 127) * 2u, lev, idx;
    int len, n = grids_slots(&len), acc = 84 + g.c->g_acc[ch];
    uint32_t rb = bb - (bb >= g.c->g_origin ? g.c->g_origin : 0u);    /* (bars since Reset) */
    *open = 0;
    if (g.c->g_euclid) {                                 /* the module's table (lut_res_euclidean), 0.5.4 */
        int L = gclamp(g.c->g_len[ch], 1, 32), rot = g.c->g_rot[ch] % L, j, first = -1;
        int a = (int)((rb * (uint32_t)n + (uint32_t)u) % (uint32_t)L);
        uint32_t pat = GRIDS_EUCLID[(L - 1) * 32 + (dens * 2 >> 3)];
        if (!(pat >> ((a + rot) % L) & 1u))
            return 0;
        for (j = 0; j < L && first < 0; j++)             /* the cycle's first hit carries the accent */
            if (pat >> ((j + rot) % L) & 1u)
                first = j;
        return a == first ? gclamp(acc, 1, 127) : 84;
    }
    idx = g.c->g_res == 0 ? (rb & 1u) * 16u + (uint32_t)u : (uint32_t)u & 31u;
    lev = grids_level(idx, (uint32_t)ch, x, y);
    lev = lev + pert > 255u ? 255u : lev + pert;
    if (lev <= (uint32_t)(255 - dens * 2))
        return 0;
    *open = ch == 2 && g.c->g_ohat && lev > 235u;
    return lev > 192u ? gclamp(acc, 1, 127) : 84;
}
/* A Grids voice (0 kick, 1 snare) on a sixteenth this bar (0, or its velocity): the drum part's draws and map */
static int grids_voice(int ch, int step, int dens)
{
    uint32_t pert = 0;
    int len, n = grids_slots(&len), u = step * (int)GEN_STEP / len, o, i;
    rng_t r;
    r.s = gh(pseed(GP_DRUMS, 1), bb % (uint32_t)phrase_len() + epoch() * 64u);
    for (i = 0; i <= ch; i++)
        pert = rnd(&r, (uint32_t)g.c->chaos / 2u + 1u);
    if (step * (int)GEN_STEP % len || u >= n)
        return 0;                                        /* (at half speed: only on its eighths) */
    return grids_at(ch, u, pert, grp_dens(ch ? L_SNARE : L_KICK, dens), &o);
}
static int grids_kick(int step, int dens) { return grids_voice(0, step, dens); }
/* the kick this bar, step by step (0, or its velocity), for the parts that go with it (the bass's coupling, the
 * melodic kick): the kick the drum part's mode plays (Cut-A-Dor, Grids, Turing), else the style's lane, at this
 * density; whether the drum part is on or not */
static int kick_src(int step, int dens)
{
    int cs = drum_cad(), m = g.c->part[GP_DRUMS].mode;
    if (cs >= 0)
        return cad_pat(cs)->vel[C_KICK][(int)(bb & 1u) * 16 + step];
    if (m == DM_GRIDS)
        return grids_kick(step, dens);
    if (m == DM_TURING)
        return (turing_reg(0, grp_dens(L_KICK, dens)) >> step & 1u) ? ((step & 3) == 0 ? 105 : 80) : 0;
    return style_hit(L_KICK, step, grp_dens(L_KICK, dens));
}
/* The snare that plays on a sixteenth this bar, as kick_src (0.5.4: Marble's rhythm models listen to both) */
static int snare_src(int step, int dens)
{
    int cs = drum_cad(), m = g.c->part[GP_DRUMS].mode;
    if (cs >= 0)
        return cad_pat(cs)->vel[C_SNARE][(int)(bb & 1u) * 16 + step];
    if (m == DM_GRIDS)
        return grids_voice(1, step, dens);
    if (m == DM_TURING)
        return (turing_reg(1, grp_dens(L_SNARE, dens)) >> step & 1u) ? 90 : 0;
    return style_hit(L_SNARE, step, grp_dens(L_SNARE, dens));
}

static void drums_fill(int f, int from, rng_t *r)
{
    int s, i, fl = g.c->part[GP_DRUMS].flair;
    switch (f) {
    case FILL_SNARE:
        for (s = from; s < 16; s++)
            if (s >= 13 || (s & 1) == 0)
                drum_hit(L_SNARE, tpos(s, 0, 1, r), 60 + (s - from) * 60 / (16 - from));
        break;
    case FILL_TOMS:
        for (s = from; s < 16; s++)
            if ((s & 1) == 0 || s >= 14) {
                int pos = tpos(s, 0, 1, r);
                drum_hit(s < (from + 16) / 2 ? L_TOMH : L_TOML, pos, 90 + (int)rnd(r, 20));
            }
        break;
    case FILL_ELECTRO:
        for (s = from; s < 16; s++) {
            int l = s < 14 ? L_SNARE : L_TOML, pos = tpos(s, 0, 0, r);
            drum_hit(l, pos, 85 + (int)rnd(r, 25));
            if (s >= 14 || chance(r, fl / 3))    /* a roll in 32nds */
                drum_hit(l, tpos(s, 0, 0, r) + 12, 75);
        }
        drum_hit(L_SIM1, tpos(chance(r, 50) ? 14 : 15, 0, 0, r), 110);
        break;
    case FILL_LIVE:
        for (s = from; s < 16; s++)
            if (chance(r, 55)) {
                int pos = tpos(s, sty()->ls, 2, r);
                drum_hit(L_SNARE, pos, s == 15 ? 100 : 35 + (int)rnd(r, 30));
            }
        drum_hit(L_OHH, tpos(14, 0, 1, r), 95);
        drum_hit(L_KICK, tpos(15, 0, 1, r), 85);
        break;
    case FILL_DROP:
        drum_hit(L_CLAP, tpos(14, 0, 0, r), 105);
        if (chance(r, 50))
            drum_hit(L_KICK, tpos(15, 0, 0, r), 95);
        break;
    case FILL_DNB:
        for (s = from; s < 16; s++) {
            int n = s >= 14 ? 3 : chance(r, 40) ? 2 : 1;
            for (i = 0; i < n; i++) {
                int l = chance(r, 25) ? L_KICK : L_SNARE, pos = tpos(s, 0, 0, r) + i * (int)GEN_STEP / n;
                drum_hit(l, pos, 70 + (int)rnd(r, 45));
            }
        }
        break;
    case FILL_BUSY:
        for (s = from; s < 16; s++) {
            int pos = tpos(s, 0, 0, r);
            drum_hit((s & 1) ? L_SNARE : L_KICK, pos, 90 + (int)rnd(r, 25));
            if (chance(r, 40))
                drum_hit(L_SIM2, tpos(s, 0, 0, r), 100);
        }
        break;
    case FILL_CAD:                                       /* Cut-A-Dor's: quiet sixteenths, percussion and snare by turns, */
        for (s = from; s < 16; s++)                      /* over the pattern, which goes on under them */
            drum_hit((s & 1) ? L_SNARE : L_CONGH, tpos(s, 0, 0, r), 50 + (s * (int)GEN_STEP) % 31);
        break;
    default: break;
    }
    if (f != FILL_ELECTRO && f != FILL_NONE && f != FILL_DROP && f != FILL_CAD && chance(r, 30 + fl / 3))
        drum_hit(L_SIM1, tpos(15, 0, 0, r), 105);          /* the laser tom: a zap at the phrase's end */
}

static uint32_t grids_mix(uint32_t a, uint32_t b, uint32_t bal) { return (a * (255u - bal) + b * bal) >> 8; }
static uint32_t grids_level(uint32_t step, uint32_t ch, uint32_t x, uint32_t y)
{
    static const uint8_t MAP[5][5] = {{10, 8, 0, 9, 11}, {15, 7, 13, 12, 6}, {18, 14, 4, 5, 3}, {23, 16, 21, 1, 2},
                                      {24, 19, 17, 20, 22}};
    uint32_t i = x >> 6, j = y >> 6, off = ch * 32u + (step & 31u), xb = (x << 2) & 255u, yb = (y << 2) & 255u;
    const uint8_t *a = GRIDS_NODE[MAP[i][j]], *b = GRIDS_NODE[MAP[i + 1u][j]];
    const uint8_t *c = GRIDS_NODE[MAP[i][j + 1u]], *d = GRIDS_NODE[MAP[i + 1u][j + 1u]];
    return grids_mix(grids_mix(a[off], b[off], xb), grids_mix(c[off], d[off], xb), yb);
}

/* Ghosts (0.5.3): soft snare hits, now and then the rim, in the sixteenths this bar left empty that are not next to a
 * kick or a snare, up to step `to`; how many by the knob. A pure function of seed and bar; in every drum mode, after
 * the pattern is made */
static void drum_ghosts(int to)
{
    const gev_t *e = &wb->ev[GEV_OFF[GP_DRUMS]];
    uint32_t busy = 0, seed = pseed(GP_DRUMS, 0x6057u);
    int i, s, n = wb->n[GP_DRUMS];
    int kn = g.c->lane_note[L_KICK], sn = g.c->lane_note[L_SNARE], kc = lane_ch(L_KICK), sc = lane_ch(L_SNARE);
    for (i = 0; i < n; i++)
        if (e[i].kind == EV_NOTE && ((e[i].a == kn && e[i].ch == kc) || (e[i].a == sn && e[i].ch == sc))) {
            int st16 = ((int)e[i].pos + (int)GEN_STEP / 2) / (int)GEN_STEP;   /* the step and its neighbours */
            busy |= (1u << (st16 + 1)) | (1u << st16) | (st16 ? 1u << (st16 - 1) : 0u);
        }
    for (s = 0; s < to && s < 15; s++) {             /* (not on the last sixteenth: the next downbeat's neighbour) */
        uint32_t hsh = gh(seed, bb * 16u + (uint32_t)s);
        if (busy >> s & 1u || (int)(hsh % 127u) >= g.c->ghosts)   /* (127: every free gap) */
            continue;
        drum_put((hsh >> 8) % 3u ? L_SNARE : L_RIM, s * (int)GEN_STEP + ((s & 1) ? swing_sub() : 0), 28 + (int)(hsh >> 12) % 18);
    }
}

static void build_drums(void)
{
    const gen_part_t *pt = &g.c->part[GP_DRUMS];
    const style_t *st = sty();
    int pl = phrase_len(), pb = (int)(bb % (uint32_t)pl), l, s, from = 16, cs = drum_cad();
    int ftype = cs >= 0 ? FILL_CAD : pt->mode == DM_STYLE ? st->fill : FILL_NONE;
    int sp = g.c->space >= 75 ? 8 : g.c->space >= 35 ? 4 : 0;
    int gap = sp && (pl == 1 || pb == pl - 1) ? 16 - sp : 16;    /* Space: hats and percussion stop here */
    int rat = g.c->ratchet ? g.c->ratchet * 70 / 127 : st->rat * pt->flair / 127;
    uint16_t hats = 0;                                   /* the closed hat's steps so far */
    rng_t r, fr;
    r.s = gh(pseed(GP_DRUMS, 1), (uint32_t)pb + epoch() * 64u);
    fr.s = gh(pseed(GP_DRUMS, 2), (uint32_t)pb + epoch() * 64u);
    if (ftype != FILL_NONE && fill_bar(bb))
        from = chance(&fr, 25 + pt->flair / 4) ? 8 : 12;
    if (cs >= 0) {
        /* Cut-A-Dor's two bars: this one's half, each hit in its pocket (Feel: 8 its Human as shipped), swing as
         * the Swing setting or its own 58 % */
        static const uint8_t CL[CAD_TRACKS] = {L_KICK, L_SNARE, L_CHH, L_OHH, L_CLAP, L_CONGH, L_RIM, L_CLAVE};
        const cad_pat_t *q = cad_pat(cs);
        int half = (int)(bb & 1u) * 16, human = g.c->feel * 65 / 8, t, sw0 = g.c->swing ? swing_sub() : feel(4);
        uint32_t seed = pseed(GP_DRUMS, 0xCADu);
        for (t = 0; t < CAD_TRACKS; t++) {
            l = CL[t];
            for (s = 0; s < 16; s++) {
                int v = q->vel[t][half + s], pos, gd = g.c->dens_grp[lane_grp(l)];
                if (!v || (t == C_HAT && !hat_keep(s)))
                    continue;
                if (gd < 0 && (int)(gh(seed ^ 0xD5u, bb * 256u + (uint32_t)(t * 32 + s)) % 64u) >= 64 + gd)
                    continue;                            /* (0.5.3: the group thinner; Cut-A-Dor's patterns are fixed) */
                if (g.c->space > 60 && bb % 8u >= 4u && t >= C_HAT && s >= 8)
                    continue;                            /* Space, further: the second half of every other four bars */
                pos = s * (int)GEN_STEP + ((s & 1) ? (is_hat(l) ? hat_sw(sw0) : sw0) : 0) +
                      cad_pocket(q, cs, human, seed, t, half + s);
                pos = gclamp(pos, 0, (int)GEN_BAR - 1);
                drum_hit(l, pos, v);
                if (t == C_HAT) {
                    hats |= (uint16_t)(1u << s);
                    if (g.c->ratchet && v > 40 && chance(&r, rat / 2))
                        drum_hit(l, pos + 12, v - 12);
                }
            }
        }
        if (g.c->hat_dens)                               /* Hat density: the steps the pattern left empty */
            for (s = 0; s < 16; s++)
                if (hat_keep(s) && !(hats >> s & 1u) && !cad_gap(g.c->space, C_HAT, half + s) &&
                    !(g.c->space > 60 && bb % 8u >= 4u && s >= 8))   /* (the same gaps as the pattern's hats) */
                    drum_hit(L_CHH, gclamp(s * (int)GEN_STEP + ((s & 1) ? hat_sw(sw0) : 0), 0, (int)GEN_BAR - 1),
                             (s & 3) == 0 ? 78 : (s & 3) == 2 ? 51 : 35);
    } else if (pt->mode == DM_GRIDS) {
        static const uint8_t GL[3] = {L_KICK, L_SNARE, L_CHH};
        int ch, ulen, n = grids_slots(&ulen);
        int own = g.c->g_swing ? gclamp(g.c->g_swing, 1, 25) * 2 * (int)GEN_STEP / 100 : -1;   /* its own swing */
        for (ch = 0; ch < 3; ch++) {
            uint32_t pert = rnd(&r, (uint32_t)g.c->chaos / 2u + 1u);
            int dens = grp_dens(GL[ch], pt->dens);       /* (the module's BD / SD / HH fill) */
            for (s = 0; s < n; s++) {
                int open, v = grids_at(ch, s, pert, dens, &open), st16 = s * ulen / (int)GEN_STEP, frac = s * ulen % (int)GEN_STEP;
                int hl, pos;
                if (!v)
                    continue;
                hl = open ? L_OHH : GL[ch];
                pos = own < 0 ? dpos(hl, st16, lane_late(GL[ch]), st->jit, &r)
                              : tpos_sw(st16, is_hat(hl) ? hat_sw(own) : own, lane_late(GL[ch]), st->jit, &r);
                pos += frac;
                if (ch == 2 && g.c->hat_dens && (frac || !hat_keep(st16)))
                    continue;
                if (ch == 2 && st16 >= gap)
                    continue;
                drum_hit(hl, pos, v);
                if (hl == L_CHH)
                    hats |= (uint16_t)(1u << st16);
            }
        }
    } else if (pt->mode == DM_TURING) {
        /* shift registers: sixteen bits a lane, a few flipped each bar of the phrase (Mutate: more of them) */
        static const uint8_t TL[5] = {L_KICK, L_SNARE, L_CHH, L_OHH, L_RIM};
        int k;
        for (k = 0; k < 5; k++) {
            uint16_t reg = turing_reg(k, grp_dens(TL[k], pt->dens));
            for (s = 0; s < 16; s++)
                if (reg >> s & 1u) {
                    int pos = dpos(TL[k], s, lane_late(TL[k]), st->jit, &r), v = ((s & 3) == 0 ? 105 : 80) + (int)rnd(&r, 12);
                    if ((TL[k] == L_CHH && !hat_keep(s)) || (k >= 2 && s >= gap))
                        continue;
                    drum_hit(TL[k], pos, v);
                    if (TL[k] == L_CHH)
                        hats |= (uint16_t)(1u << s);
                }
        }
    } else {
        for (l = 0; l < GEN_NLANES; l++) {
            int perc = l != L_KICK && l != L_SNARE && l != L_CLAP && l != L_SIM1 && l != L_SIM2;
            if (l == L_SIM2 && !st->lane[L_SIM2]) {          /* the second Simmons lane by its role */
                if (g.c->sim2 == 0 && from > 12)
                    drum_hit(l, tpos(12, st->ls, st->jit, &r), 96);
                else if (g.c->sim2 == 1 && pb == 0 && bb)
                    drum_hit(l, 0, 110);
                else if (g.c->sim2 == 2)
                    for (s = 0; s < from; s++)
                        if ((s == 3 || s == 6 || s == 11 || s == 14) && chance(&r, 30 + pt->dens / 2)) {
                            int pos = tpos(s, st->lh, st->jit, &r);
                            drum_hit(l, pos, 70 + (int)rnd(&r, 30));
                        }
                continue;
            }
            for (s = 0; s < from; s++) {
                int v, pos, loud;
                if ((perc && s >= gap) || (l == L_CHH && !hat_keep(s)))
                    continue;
                v = style_hit(l, s, grp_dens(l, pt->dens));
                if (!v)
                    continue;
                loud = v > 60;
                if (l == L_CHH) {                            /* the hat in three levels: the beat, the eighth, the sixteenth */
                    hats |= (uint16_t)(1u << s);
                    if (loud)
                        v -= (s & 3) == 2 ? 10 : (s & 1) ? 22 : 0;
                }
                pos = dpos(l, s, lane_late(l), st->jit, &r);
                if (g.c->pocket)
                    pos = gclamp(pos + style_pocket(l, s, v), 0, (int)GEN_BAR - 1);
                drum_hit(l, pos, v);
                if ((l == L_CHH || l == L_SNARE) && loud && chance(&r, rat)) {
                    int n = chance(&r, 40) ? 3 : 2, i;       /* a ratchet */
                    for (i = 1; i < n; i++)
                        drum_hit(l, pos + i * (int)GEN_STEP / n, v - 15 - (int)rnd(&r, 15));
                }
            }
        }
    }
    if (cs < 0 && g.c->hat_dens)                         /* Hat density: the steps the pattern left empty */
        for (s = 0; s < (from < gap ? from : gap); s++)
            if (hat_keep(s) && !(hats >> s & 1u))
                drum_hit(L_CHH, dpos(L_CHH, s, st->lh, 0, &r), (s & 3) == 0 ? 100 : (s & 1) == 0 ? 84 : 62);
    if (g.c->ghosts)                                     /* 0.5.3: Ghosts, in every mode */
        drum_ghosts(from < gap ? from : gap);
    if (from < 16)
        drums_fill(ftype, from, &fr);
    else if (g.c->ratchet && (int)(gh(pseed(GP_DRUMS, 0xA00u), bb) % 254u) < g.c->ratchet)
        for (s = 0; s < 4; s++)                              /* a snare rush into the next bar, in 32nds */
            drum_put(L_SNARE, 14 * (int)GEN_STEP + s * 12, 52 + s * 16);
    if (bb && fill_bar(bb - 1u) && g.c->fill_every != 2 && ftype != FILL_NONE && ftype != FILL_DROP && ftype != FILL_CAD &&
        pt->dens >= 40)
        drum_hit(L_CYM, 0, 100);                             /* a crash after the fill */
}

/* --------------------------------------------------------------- chords --- */
static int voicing(chord_t c, int type, int base, int poly, uint8_t *out)
{
    static const uint8_t ORD_RICH[6] = {1, 3, 4, 2, 0, 5}, ORD_7[6] = {1, 3, 0, 2, 4, 5}, ORD_3[6] = {0, 1, 2, 3, 4, 5};
    const uint8_t *ord = (type == V_RICH && QN[c.q] >= 5) || type == V_TIGHT ? ORD_RICH : type == V_TRIAD ? ORD_3 : ORD_7;
    int want = type == V_TRIAD ? 3 : type == V_TIGHT && QN[c.q] == 4 ? 3 : 4, n = 0, i, j, open = 0;
    uint8_t iv[6];
    if (type == V_OCT) {                                 /* the root in octaves */
        iv[0] = 0, iv[1] = 12, iv[2] = 24;
        n = 3;
        open = 1;
        base -= 12;
    } else if (type == V_WIDE) {                         /* root and fifth low, the seventh and the third above them */
        iv[0] = 0, iv[1] = 7, iv[2] = (uint8_t)(QN[c.q] >= 4 ? QIV[c.q][3] : 12), iv[3] = (uint8_t)(QIV[c.q][1] + 12);
        n = 4;
        open = 1;
        base -= 12;
    } else if (type == V_POWER || c.q == Q_P5) {
        iv[0] = 0, iv[1] = 7, iv[2] = 12;
        n = 3;
        open = 1;
    } else if (type == V_QUARTAL) {                      /* fourths under minor and sus chords, fifths under major */
        int min = QIV[c.q][1] == 3 || QIV[c.q][1] == 5;
        iv[0] = 0, iv[1] = (uint8_t)(min ? 5 : 7), iv[2] = (uint8_t)(min ? 10 : 14);
        n = 3;
    } else {
        for (i = 0; i < 6 && n < want; i++)
            if (ord[i] < QN[c.q])
                iv[n++] = QIV[c.q][ord[i]];
    }
    if (n > poly)
        n = poly;
    for (i = 0; i < n; i++) {                            /* each tone in the octave from base: close voicings that */
        int nt = c.root + iv[i];                         /* move little from chord to chord */
        nt = open ? root_note(c, base) + iv[i] : base + ((nt - base % 12) % 12 + 12) % 12;
        out[i] = (uint8_t)gclamp(nt, 0, 127);
    }
    for (i = 1; i < n; i++) {                            /* low to high */
        uint8_t t = out[i];
        for (j = i; j > 0 && out[j - 1] > t; j--)
            out[j] = out[j - 1];
        out[j] = t;
    }
    return n;
}

static void build_chords(void)
{
    const gen_part_t *pt = &g.c->part[GP_CHORD];
    const style_t *st = sty();
    const gdev_t *d = devp(GP_CHORD);
    const char *comp = pt->mode == CM_PAD ? "9000000000000000" : pt->mode == CM_STABS ? "9000009000900300" : st->comp;
    int len = pt->mode == CM_PAD ? 0 : pt->mode == CM_STABS ? 1 : st->comp_len;
    int pl = phrase_len(), pb = (int)(bb % (uint32_t)pl), s, i, hit[16], hp[16], nh = 0, base = 52 + 12 * pt->oct;
    int vt = g.c->voice == 1 ? V_TIGHT : g.c->voice == 2 ? V_WIDE : g.c->voice == 3 ? V_OCT : st->voicing;
    chord_t c0 = chord_step(0), c1 = chord_step(8);
    rng_t r;
    r.s = gh(pseed(GP_CHORD, 1), (uint32_t)pb + epoch() * 64u);
    for (s = 0; s < 16; s++) {
        int gho, w = wdec(comp[s], &gho), lev = wlevel(w);
        if (w && w < 9)
            lev += (int)(gh(pseed(GP_CHORD, 0x100u), (uint32_t)(s + 16 * (pb & 1))) % 81u) - 40;
        if ((w && lev >= 255 - pt->dens * 2) || (s == 8 && (c0.root != c1.root || c0.q != c1.q)))
            hit[nh++] = s;
    }
    for (i = 0; i < nh; i++)
        hp[i] = tpos(hit[i], 0, st->jit / 2, &r);
    for (i = 0; i < nh; i++) {
        uint8_t nt[6];
        chord_t c = chord_step(hit[i]);
        int k, n, pos = hp[i], dur, vel, end = i + 1 < nh ? hp[i + 1] : (int)GEN_BAR;
        if (hit[i] < 8 && (i + 1 >= nh || hit[i + 1] > 8) && (c0.root != c1.root || c0.q != c1.q))
            end = 8 * (int)GEN_STEP;
        n = voicing(c, vt, base, d->poly, nt);
        dur = end - pos - 3;
        if (len && dur > len * (int)GEN_STEP - 8)
            dur = len * (int)GEN_STEP - 8;
        vel = (hit[i] & 3) == 0 ? 96 : 84;
        for (k = 0; k < n; k++)
            gput_note(GP_CHORD, pos, nt[k], gclamp(vel + (int)rnd(&r, 10) - k * 2, 1, d->vmax), dur);
        if (pt->flair && d->cc) {
            int depth = pt->flair / 2;
            if (!len && dur >= 6 * (int)GEN_STEP && chance(&r, pt->flair * 100 / 127)) {   /* a pad opens */
                ramp_cc(GP_CHORD, pos, pos + dur - 12, d->rest, gclamp(d->rest + depth, 0, 127), 8);
                gput_cc(GP_CHORD, pos + dur, d->rest);
            } else if (len && chance(&r, pt->flair * 60 / 127)) {                          /* a stab barks */
                gput_cc(GP_CHORD, pos, gclamp(d->rest + depth, 0, 127));
                gput_cc(GP_CHORD, pos + dur, d->rest);
            }
        }
    }
}

/* ------------------------------------------------------------------ arp --- */
/* the harp run (SFX_HARP, LA beats): on the phrase's first beat, 32nds up two octaves of the chord (with its top
 * note when the chord has fewer than four tones), rising in velocity; on a polyphonic device they ring into beat 2 */
static void harp_run(int base)
{
    const gdev_t *d = devp(GP_ARP);
    uint8_t nt[6];
    int n = voicing(chord_step(0), V_SEVENTH, base, 4, nt), m, i, t = (int)GEN_STEP / 2;
    if (!n)
        return;
    m = 2 * n < 8 ? 2 * n : 8;
    for (i = 0; i <= m && i < 8; i++) {
        int note = i < m ? nt[i % n] + 12 * (i / n) : nt[0] + 24;
        int dur = d->poly > 1 ? 2 * 4 * (int)GEN_STEP - i * t : t - 2;
        gput_note(GP_ARP, i * t, note, gclamp(72 + i * 4, 1, d->vmax), dur);
    }
}
static void build_arp(void)
{
    const gen_part_t *pt = &g.c->part[GP_ARP];
    const style_t *st = sty();
    int div = st->arp_div ? st->arp_div : 1, s, base = 60 + 12 * pt->oct;
    int harp = (STYLE_META[g.c->style % GEN_NSTYLES].fx & SFX_HARP) && bb % (uint32_t)phrase_len() == 0u;
    rng_t r;
    r.s = gh(pseed(GP_ARP, 1), bb % (uint32_t)phrase_len() + epoch() * 64u);
    if (div == 3)
        div = 1;
    if (harp)
        harp_run(base);
    for (s = harp ? 4 : 0; s < 16; s += div) {
        uint8_t nt[6], seq[12];
        chord_t c = chord_step(s);
        int n = voicing(c, st->voicing == V_QUARTAL || st->voicing == V_POWER ? V_SEVENTH : st->voicing, base, 4, nt);
        int m = 0, i, idx, per, pos, vel, dur, reps;
        uint32_t k = (bb * 16u + (uint32_t)s) / (uint32_t)div;   /* counted from the start: the shape goes on */
        if (!n)
            continue;
        for (i = 0; i < n; i++)
            seq[m++] = nt[i];
        for (i = 0; i < n && m < 12; i++)
            seq[m++] = (uint8_t)gclamp(nt[i] + 12, 0, 127);
        switch (pt->mode) {
        case AM_DOWN: idx = m - 1 - (int)(k % (uint32_t)m); break;
        case AM_UPDOWN:
            per = m > 1 ? 2 * m - 2 : 1;
            idx = (int)(k % (uint32_t)per);
            idx = idx < m ? idx : per - idx;
            break;
        case AM_RANDOM: idx = (int)(gh(pseed(GP_ARP, 7), k % (16u * (uint32_t)phrase_len())) % (uint32_t)m); break;
        case AM_CONVERGE:
            idx = (int)(k % (uint32_t)m);
            idx = (idx & 1) ? m - 1 - idx / 2 : idx / 2;
            break;
        case AM_PLAYED: {
            static const uint8_t P[8] = {0, 2, 1, 3, 2, 4, 3, 5};
            idx = P[k & 7] % m;
            break;
        }
        default: idx = (int)(k % (uint32_t)m); break;
        }
        if ((int)(gh(pseed(GP_ARP, 3), (uint32_t)s + 16u * (bb % (uint32_t)phrase_len())) % 127u) > pt->dens + 26 && s)
            continue;                                    /* Density: steps left out */
        pos = tpos(s, 0, st->jit / 2, &r);
        vel = ((s & 3) == 0 ? 100 : 84) + (int)rnd(&r, 10);
        dur = div * (int)GEN_STEP * 6 / 10;
        reps = chance(&r, pt->flair / 4) ? 2 : 1;        /* Flair: ratchets */
        for (i = 0; i < reps; i++)
            gput_note(GP_ARP, pos + i * div * (int)GEN_STEP / 2, seq[idx], vel - i * 12, dur / reps);
    }
}

/* ------------------------------------------------- bass and lead: mono --- */
#define MF_ACC 1u
#define MF_SLIDE 2u
#define MF_GHOST 4u
#define MF_END 8u                        /* the phrase's last note */
typedef struct { int16_t pos, len; uint8_t note, fl, step, rsv; } mnote_t;

/* a line for one voice, as the device takes it: velocities, accents, slides as overlaps, ghost notes; and the
 * flourishes: filter moves on the device's controller, scoops and falls on the pitch bend */
static void render_mono(int p, mnote_t *m, int n, rng_t *r, int wobble)
{
    const gen_part_t *pt = &g.c->part[p];
    const gdev_t *d = devp(p);
    int i, fl = pt->flair, hi = gclamp(d->rest + 40 + fl / 2, 0, 127);
    for (i = 0; i < n; i++) {
        int pos = m[i].pos, gap = i + 1 < n ? m[i + 1].pos - pos : (int)GEN_BAR + 48 - pos, dur = m[i].len, vel;
        int slide = (m[i].fl & MF_SLIDE) && i + 1 < n && (d->flags & DF_LEGATO);
        if (slide)
            dur = gap + 4;                               /* into the next note: legato, the 303's slide */
        else if (dur > gap - 3)
            dur = gap - 3;
        if (dur < 4)
            dur = 4;
        vel = 86 + (int)rnd(r, 16);
        if (vel > d->vmax)
            vel = d->vmax;
        if (m[i].fl & MF_ACC)
            vel = d->acc;
        if (m[i].fl & MF_GHOST) {
            if (d->flags & DF_NOVEL)
                dur = 5;
            else
                vel = 40 + (int)rnd(r, 12), dur = dur > 10 ? 10 : dur;
        }
        gput_note(p, pos, m[i].note, vel, dur);
        if (!fl)
            continue;
        if (d->cc) {
            if (wobble && dur >= 2 * (int)GEN_STEP) {    /* a filter wobble through a long note */
                int rate = (int)GEN_STEP * (chance(r, 50) ? 2 : chance(r, 50) ? 4 : 3), t;
                for (t = 0; t < dur; t += rate / 2)
                    gput_cc(p, pos + t, (t / (rate / 2)) & 1 ? d->rest : hi);
                gput_cc(p, pos + dur, d->rest);
                g.moved[p] |= 1u;
            } else if ((m[i].fl & MF_ACC) && ((d->flags & DF_NOVEL) || chance(r, fl * 100 / 127))) {
                ramp_cc(p, pos, pos + (dur < 20 ? dur : 20), 127, d->rest + (127 - d->rest) / 5, 4);   /* a quack */
                gput_cc(p, pos + (dur < 22 ? dur + 1 : 22), d->rest);
                g.moved[p] |= 1u;
            } else if (dur >= 3 * (int)GEN_STEP && chance(r, fl * 70 / 127)) {                       /* a swell */
                ramp_cc(p, pos, pos + dur - 6, d->rest, hi, 7);
                gput_cc(p, pos + dur, d->rest);
                g.moved[p] |= 1u;
            }
        }
        if (bend_range(p) && !slide && !(m[i].fl & MF_GHOST)) {
            int bq = bend_range(p) * 4;
            if ((m[i].fl & MF_END) && dur >= 2 * (int)GEN_STEP && chance(r, fl * 80 / 127)) {       /* a fall */
                ramp_bend(p, pos + dur - 20, pos + dur - 1, 0, -(bq < 12 ? bq : 12), 6);
                gput_bend(p, pos + dur + 2, 0);
                g.moved[p] |= 2u;
            } else if (dur >= (int)GEN_STEP && chance(r, fl * 30 / 127)) {                          /* a scoop */
                ramp_bend(p, pos, pos + 10, -(bq < 6 ? bq : chance(r, 50) ? 4 : 6), 0, 5);
                g.moved[p] |= 2u;
            }
        }
    }
}

typedef struct { uint8_t kind, fl; } bstep_t;
enum { BK_NONE, BK_ROOT, BK_OCT, BK_FIFTH, BK_COLOR, BK_UP, BK_DOWN, BK_APPR };

static void bass_riff(int mode, uint32_t seed, int dens, bstep_t *o)
{
    const bass_t *b = &BASS[mode];
    rng_t r;
    int s;
    r.s = seed;
    for (s = 0; s < 16; s++) {
        int gho, w = wdec(b->rhy[s], &gho), lev = wlevel(w), x;
        o[s].kind = BK_NONE;
        o[s].fl = 0;
        if (w && w < 9)
            lev += (int)rnd(&r, mode == BM_ACID ? 141 : 81) - (mode == BM_ACID ? 70 : 40);
        x = (int)rnd(&r, 100);                           /* (drawn whether it plays or not: Density keeps the rest) */
        if (!w || lev < 255 - dens * 2)
            continue;
        o[s].kind = s == 0 ? BK_ROOT : x < b->oct ? BK_OCT : x < b->oct + b->fifth ? BK_FIFTH
                    : x < b->oct + b->fifth + b->color ? BK_COLOR
                    : x < b->oct + b->fifth + b->color + b->pass ? (chance(&r, 50) ? BK_UP : BK_DOWN) : BK_ROOT;
        if (mode == BM_OCTAVE && (s & 3) == 2)
            o[s].kind = BK_OCT;
        if (mode == BM_CUMBIA)                           /* the pickup steps up from the root; beat 3 is the fifth */
            o[s].kind = s == 6 ? BK_UP : s == 8 ? BK_FIFTH : BK_ROOT;
        if (chance(&r, b->acc) || (s == 0 && mode == BM_BOOTSY))
            o[s].fl |= MF_ACC;
        if (chance(&r, b->slide))
            o[s].fl |= MF_SLIDE;
        if (s && !(o[s].fl & MF_ACC) && chance(&r, b->ghost))
            o[s].fl |= MF_GHOST;
    }
}

static void build_bass(void)
{
    const gen_part_t *pt = &g.c->part[GP_BASS];
    int mode = pt->mode == BM_STYLE ? sty()->bass : pt->mode % NBM;
    const bass_t *b = &BASS[mode];
    int pl = phrase_len(), pb = (int)(bb % (uint32_t)pl), last = pl > 1 && pb == pl - 1, s, n = 0, prev = -1;
    int base = 36 + 12 * pt->oct, cp = g.c->couple ? g.c->couple - 1 : sty()->couple;
    bstep_t rf[16], alt[16];
    mnote_t m[16];
    rng_t r;
    uint32_t ep = epoch();
    if (STYLE_META[g.c->style % GEN_NSTYLES].fx & SFX_HOLD) {   /* (0.5.2) one riff for each 8 bars, its own Mutate */
        pb = 0;
        last = 0;
        ep = g.c->mutate ? bb / 8u / (uint32_t)(9 - gclamp(g.c->mutate, 1, 8)) : 0u;
    }
    r.s = gh(pseed(GP_BASS, 1), (uint32_t)pb + ep * 64u);
    if (mode == BM_TURING) {                             /* a register of scale steps, turned as the phrase goes */
        for (s = 0; s < 16; s++) {
            uint32_t h = gh(pseed(GP_BASS, 0x600u), (uint32_t)((s + pb * (1 + g.c->mutate / 2)) & 15));
            rf[s].kind = (uint8_t)((int)(h % 127u) < pt->dens + 20 || s == 0 ? BK_ROOT : BK_NONE);
            rf[s].fl = (uint8_t)((h >> 8) % 100u < b->acc ? MF_ACC : (h >> 16) % 100u < b->slide ? MF_SLIDE : 0);
        }
    } else {
        bass_riff(mode, pseed(GP_BASS, 0x10u + (uint32_t)(pb % b->riff)), pt->dens, rf);
        if (last) {                                      /* the phrase's last bar: its second half goes elsewhere */
            bass_riff(mode, pseed(GP_BASS, 0x20u) + epoch(), gclamp(pt->dens + 15, 0, 127), alt);
            for (s = 8; s < 16; s++)
                rf[s] = alt[s];
        }
    }
    if (chord_step(0).root != chord_step(8).root && rf[8].kind == BK_NONE) {   /* a chord on the half bar: its root */
        rf[8].kind = BK_ROOT;
        rf[8].fl = 0;
    }
    if (cp != CP_FREE)                                   /* with the kick: a note on each of its hits; locked: only there */
        for (s = 0; s < 16; s++) {
            int k = kick_src(s, g.c->part[GP_DRUMS].dens) > 60;
            if (k && rf[s].kind == BK_NONE) {
                rf[s].kind = BK_ROOT;
                rf[s].fl = 0;
            } else if (!k && cp == CP_LOCK) {
                rf[s].kind = BK_NONE;
            }
        }
    for (s = 0; s < 16 && n < 16; s++) {
        chord_t c = chord_step(s), cn = chord_after(s);
        uint16_t mm = mel_mask(c, 0);
        int root = root_note(c, base), nt, k, nexthit = 16, changes;
        if (rf[s].kind == BK_NONE)
            continue;
        for (k = s + 1; k < 16; k++)
            if (rf[k].kind != BK_NONE) {
                nexthit = k;
                break;
            }
        changes = (s < 8 ? nexthit >= 8 : nexthit >= 16) && cn.root != c.root;
        switch (rf[s].kind) {
        case BK_OCT: nt = root + 12; break;
        case BK_FIFTH: nt = root + 7; break;
        case BK_COLOR:                                   /* the seventh under the root, the fifth under it, the third */
            k = (int)rnd(&r, 10);
            nt = k < 6 ? root + (QN[c.q] >= 4 ? QIV[c.q][3] : 10) - 12 : k < 8 ? root - 5 : root + QIV[c.q][1];
            break;
        case BK_UP: nt = ladder(prev < 0 ? root : prev, 1, mm); break;
        case BK_DOWN: nt = ladder(prev < 0 ? root : prev, -1, mm); break;
        default: nt = root; break;
        }
        if (mode == BM_TURING) {
            uint32_t h = gh(pseed(GP_BASS, 0x700u), (uint32_t)((s + pb * (1 + g.c->mutate / 2)) & 15));
            nt = ladder(root, (int)(h % 8u) - 2, mm);
        }
        if (s == 8 && c.root != chord_step(0).root)      /* the chord changes on the half bar: its root */
            nt = root;
        if (changes && s > 0 && s != 8 && chance(&r, b->appr))   /* into the next chord from a semitone away */
            nt = root_note(cn, base) + (chance(&r, 75) ? -1 : 1);
        if (nt > base + 19)
            nt -= 12;
        m[n].step = (uint8_t)s;
        m[n].pos = (int16_t)tpos(s, cp == CP_LOCK ? sty()->lk : b->late, sty()->jit / 2, &r);
        m[n].note = (uint8_t)gclamp(nt, 0, 127);
        m[n].fl = rf[s].fl;
        k = (nexthit - s) * (int)GEN_STEP;
        m[n].len = (int16_t)(b->gate == 0 ? 11 : b->gate == 1 ? k * 7 / 10 : k);
        if (m[n].len > 8 * (int)GEN_STEP)
            m[n].len = 8 * (int)GEN_STEP;
        prev = nt;
        n++;
    }
    if (last && n && n < 13 && cp != CP_LOCK && chance(&r, b->run * (40 + pt->flair) / 127)) {   /* a run into the next phrase */
        chord_t c = chord_step(15), cn = chord_at(bb + 1u, 0);
        uint16_t mm = mel_mask(c, 0);
        int target = root_note(cn, base), up = chance(&r, 50), k;
        while (n && m[n - 1].step >= 12)
            n--;
        for (k = 0; k < 4 && n < 16; k++) {
            m[n].step = (uint8_t)(12 + k);
            m[n].pos = (int16_t)tpos(12 + k, b->late, 0, &r);
            m[n].note = (uint8_t)ladder(target + (up ? 0 : 12), up ? k - 4 : 4 - k, mm);
            m[n].fl = (uint8_t)(k < 3 ? MF_SLIDE : 0);
            m[n].len = (int16_t)(GEN_STEP - 4);
            n++;
        }
    }
    if (last && n)
        m[n - 1].fl |= MF_END;
    render_mono(GP_BASS, m, n, &r, mode == BM_WOBBLE);
}

static void lead_motif(uint32_t seed, const char *rhy, int dens, int busy, uint8_t *hit, int8_t *deg)
{
    rng_t r;
    int s, cur = 0;
    r.s = seed;
    for (s = 0; s < 16; s++) {
        static const int8_t STEPS[12] = {0, 0, 0, -1, -1, 1, 1, -2, 2, 0, -1, 1};   /* riffs: small moves, repeats */
        int gho, w = wdec(rhy[s], &gho), lev = wlevel(w) + (int)rnd(&r, 121) - 60 + busy;
        int d = STEPS[rnd(&r, 12)];
        hit[s] = (uint8_t)(w && lev >= 255 - dens * 2);
        if (hit[s]) {
            cur = gclamp(cur + d, -4, 6);
            deg[s] = (int8_t)cur;
        }
    }
}

static void build_lead(void)
{
    const gen_part_t *pt = &g.c->part[GP_LEAD];
    const style_t *st = sty();
    int mode = pt->mode == LM_STYLE ? st->lead : pt->mode % NLM;
    int pl = phrase_len(), pb = (int)(bb % (uint32_t)pl), last = pb == pl - 1, s, n = 0;
    int base = st->lead_base + 12 * pt->oct, glide, which, anchor;
    const char *rhy = mode == LM_WHINE ? "9000000000700030" : mode == LM_SOLO ? "9597059790579507" : st->lead_rhy;
    uint8_t hit[16];
    int8_t deg[16];
    mnote_t m[16];
    rng_t r;
    r.s = gh(pseed(GP_LEAD, 1), (uint32_t)pb + epoch() * 64u);
    glide = mode == LM_WHINE ? 70 : mode == LM_SOLO ? 25 : 12;
    if (mode == LM_WHINE)
        base = (st->lead == LM_WHINE ? st->lead_base : 79) + 12 * pt->oct;
    /* the phrase: the hook, the hook again, and an answer in the last bar (in two bars: hook, answer) */
    which = pl == 1 ? 0 : last ? 1 : (pb & 3) == 1 && pl >= 4 ? 2 : 0;
    if (mode == LM_CALL && !(pb & 1) && pl > 1)
        which = -1;                                      /* the call is the others': the lead answers in the odd bars */
    if (which < 0) {
        memset(hit, 0, sizeof hit);
        if (chance(&r, 40))                              /* a pickup */
            hit[14] = 1, deg[14] = -1;
    } else if (mode == LM_TURING) {
        for (s = 0; s < 16; s++) {
            uint32_t h = gh(pseed(GP_LEAD, 0x600u), (uint32_t)((s + pb * (1 + g.c->mutate / 2)) & 15));
            hit[s] = (uint8_t)((int)(h % 127u) < pt->dens);
            deg[s] = (int8_t)((int)((h >> 8) % 9u) - 3);
        }
    } else {
        lead_motif(pseed(GP_LEAD, 0x10u + (uint32_t)(which == 2 ? 0 : which)) + (which == 1 ? epoch() : 0u), rhy,
                   pt->dens, mode == LM_SOLO ? 20 : 0, hit, deg);
        if (which == 2) {                                /* the hook with another ending */
            uint8_t h2[16];
            int8_t d2[16];
            lead_motif(pseed(GP_LEAD, 0x30u), rhy, pt->dens, 0, h2, d2);
            for (s = 10; s < 16; s++)
                hit[s] = h2[s], deg[s] = d2[s];
        }
    }
    anchor = (int)(pseed(GP_LEAD, 0x40u) % 3u);
    for (s = 0; s < 16 && n < 16; s++) {
        chord_t c = chord_step(s);
        uint16_t mm = mel_mask(c, st->pent), cm = chord_mask(c);
        int k, nexthit = 16, nt, a;
        if (!hit[s])
            continue;
        for (k = s + 1; k < 16; k++)
            if (hit[k]) {
                nexthit = k;
                break;
            }
        a = nearest(base + (anchor == 1 ? 3 : anchor == 2 ? -2 : 0), cm);   /* the line hangs from a chord tone */
        nt = ladder(a, deg[s], mm);
        if ((s & 3) == 0 || nexthit - s >= 4)            /* strong and long notes: chord tones */
            nt = nearest(nt, cm);
        if (last && nexthit == 16 && which >= 0)         /* the phrase ends on the chord's root or fifth */
            nt = nearest(nt, (uint16_t)(1u << c.root | 1u << ((c.root + 7) % 12)));
        while (nt > base + 14)                           /* within an octave and a bit of the centre */
            nt -= 12;
        while (nt < base - 10)
            nt += 12;
        m[n].step = (uint8_t)s;
        m[n].pos = (int16_t)tpos(s, mode == LM_WHINE ? 1 : 0, st->jit / 2, &r);
        m[n].note = (uint8_t)gclamp(nt, 0, 127);
        k = chance(&r, glide) && nexthit - s <= 4 ? MF_SLIDE : 0;   /* (one draw after the other: the same on every compiler) */
        m[n].fl = (uint8_t)(k | ((s & 3) == 0 && chance(&r, 30) ? MF_ACC : 0));
        k = (nexthit - s) * (int)GEN_STEP;
        m[n].len = (int16_t)(mode == LM_WHINE || nexthit - s >= 3 ? k * 9 / 10 : k * 6 / 10);
        if (m[n].len > (mode == LM_WHINE ? 14 : 8) * (int)GEN_STEP)
            m[n].len = (int16_t)((mode == LM_WHINE ? 14 : 8) * (int)GEN_STEP);
        n++;
    }
    if (last && n)
        m[n - 1].fl |= MF_END;
    render_mono(GP_LEAD, m, n, &r, 0);
}

/* -------------------------------------------------------- melodic kick --- */
static void build_kick(void)
{
    const gen_part_t *pt = &g.c->part[GP_KICK];
    int s, hits[16], pos[16], nh = 0, i, lo = gclamp(g.c->mb_lo, 12, 96);
    rng_t r;
    r.s = gh(pseed(GP_KICK, 1), bb % (uint32_t)phrase_len() + epoch() * 64u);
    for (s = 0; s < 16; s++) {
        int v = pt->mode == KK_FOUR ? ((s & 3) == 0 ? 110 : 0) : kick_src(s, pt->mode == KK_808 ? pt->dens / 2 : pt->dens);
        if (v > 60)
            hits[nh++] = s;
    }
    for (i = 0; i < nh; i++)
        pos[i] = tpos(hits[i], sty()->lk, 0, &r);
    for (i = 0; i < nh; i++) {
        chord_t c = chord_step(hits[i]), cn = chord_after(hits[i]);
        int nt = c.root, dur, cap = (pt->mode == KK_808 ? 12 : 6) * (int)GEN_STEP;
        if (i == nh - 1 && hits[i] >= 12 && cn.root != c.root && chance(&r, pt->flair * 100 / 127))
            nt = cn.root + 11;                           /* into the next chord from below */
        else if (hits[i] && (hits[i] & 3) && chance(&r, pt->flair * 40 / 127))
            nt = c.root + 7;                             /* the fifth, off the beat */
        nt = lo + ((nt - lo % 12) % 12 + 12) % 12 + g.c->mb_xpose;
        dur = (i + 1 < nh ? pos[i + 1] : (int)GEN_BAR) - pos[i] - 3;
        gput_ev(GP_KICK, pos[i], EV_NOTE, gclamp(nt, 0, 127), (hits[i] & 3) == 0 ? 118 : 100, dur < cap ? dur : cap,
               pt->chan);
    }
}

/* --------------------------------------------------------------- marble --- */
/* After Mutable Instruments' Marbles: random notes drawn against the chord. A bar of them loops; Chance (the
 * part's Flair) is how many are drawn afresh each time round: 0 the loop exactly, 127 never the same twice.
 * Density is how many of the steps sound; the mode what the notes are held to: the chord, the pentatonic, the key;
 * Spread how many scale steps they range over. */
/* Marble (0.5.4: the upgrade after Mutable Instruments' Marbles: the ideas of its t_generator.cc,
 * random_sequence.h, quantizer.cc and distributions.h, written anew in integers). Every default plays as before.
 * A pure function of seed and bar: memory looks back over a bounded number of bars.
 *   Marbles: Copyright 2015 Emilie Gillet. Permission is hereby granted, free of charge, to any person obtaining a
 *   copy of this software and associated documentation files (the "Software"), to deal in the Software without
 *   restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute,
 *   sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do
 *   so, subject to the following conditions: The above copyright notice and this permission notice shall be
 *   included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT
 *   WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 *   FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR
 *   ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT
 *   OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE. */
static int marble_fresh(uint32_t a) { return (int)(gh(pseed(GP_RAND, 2), a) % 127u) < g.c->part[GP_RAND].flair; }
/* the random word behind absolute step a: the loop's own (by loop position), or a fresh draw; with Memory, the last
 * fresh draw made at this loop position, up to 16 bars back */
static uint32_t marble_word(uint32_t a)
{
    uint32_t L = (uint32_t)gclamp(g.c->m_len, 1, 16), p = a % L, k, it = a / L;
    if (marble_fresh(a))
        return gh(pseed(GP_RAND, 3), a);
    if (g.c->m_memory)
        for (k = 1; k <= 256u / L && k <= it; k++)
            if (marble_fresh(a - k * L))
                return gh(pseed(GP_RAND, 3), a - k * L);
    if (g.c->m_shuffle && (int)(gh(pseed(GP_RAND, 5), a) % 127u) < g.c->m_shuffle)
        p = gh(pseed(GP_RAND, 6), it * 16u + p) % L;     /* the loop's notes, replayed out of order */
    return gh(pseed(GP_RAND, 4), p);
}
/* where in the spread the note falls (0..65535) from Bias and Shape; Shape 64 is the even draw from before */
static uint32_t marble_place(uint32_t h)
{
    int sh = g.c->m_shape, b = g.c->m_bias * 65535 / 127;
    int u1 = (int)(h >> 8 & 0xFFFFu), u2 = (int)(gh(h, 0x51u) & 0xFFFFu), v;
    if (sh < 64) {                                       /* a bell around Bias, narrower down to one note at 0 */
        v = b + ((u1 + u2) / 2 - 32768) * sh / 64;
    } else {                                             /* even at 64, then more and more to the edges */
        int e = (sh - 64) * 512 / 63, x = u1 - 32768;     /* (e in 512ths) */
        v = 32768 + x + (x > 0 ? (32767 - x) : -(32768 + x)) * e / 512;
    }
    return (uint32_t)gclamp(v, 0, 65535);
}
static int bits12(uint16_t m)
{
    int i, n = 0;
    for (i = 0; i < 12; i++)
        n += m >> i & 1u;
    return n;
}
/* Steps: the pool, thinned to its heavier degrees counted from the chord's root (root, fifth, thirds, the rest) */
static uint16_t marble_steps(uint16_t m, int root)
{
    static const uint16_t KEEP[4] = {0x001u, 0x081u, 0x099u, 0xFFFu};   /* (intervals above the root) */
    int lvl = g.c->m_steps >= 127 ? 3 : g.c->m_steps / 32, i;
    uint16_t k = KEEP[lvl > 3 ? 3 : lvl], out = 0;
    for (i = 0; i < 12; i++)
        if (m >> i & 1u && k >> ((i - root + 12) % 12) & 1u)
            out |= (uint16_t)(1u << i);
    return out ? out : m;
}
static void build_marble(void)
{
    const gen_part_t *pt = &g.c->part[GP_RAND];
    int s, base = 60 + 12 * pt->oct, div = pt->dens < 40 ? 2 : 1, sp = g.c->spread ? g.c->spread : 11, silent = 0;
    int ddens = g.c->part[GP_DRUMS].dens, pc = 30 + pt->dens / 2;
    uint16_t played = 0;
    rng_t r;
    r.s = gh(pseed(GP_RAND, 1), bb);
    for (s = 0; s < 16; s += div) {
        chord_t c = chord_step(s);
        uint16_t m = pt->mode == RM_CHORD ? chord_mask(c) : mel_mask(c, pt->mode == RM_PENT);
        uint32_t a = bb * 16u + (uint32_t)s, h = g.c->m_len == 16 && !g.c->m_memory && !g.c->m_shuffle
            ? (marble_fresh(a) ? gh(pseed(GP_RAND, 3), a) : gh(pseed(GP_RAND, 4), (uint32_t)s)) : marble_word(a);
        int len = div * (int)GEN_STEP, idx, chance = pc;
        if (g.c->m_rhythm) {                             /* the drums that play as the other channel */
            int ks = kick_src(s, ddens) || snare_src(s, ddens);
            if (g.c->m_rhythm == 1)                      /* Answer: the gaps */
                chance = ks ? pc / 4 : gclamp(pc * 3 / 2, 0, 95);
            else {                                       /* Markov, within the bar */
                if (s >= 8 && played >> (s - 8) & 1u)
                    chance += 25;
                if (ks)
                    chance -= 30;
                if (s >= div && played >> (s - div) & 1u)
                    chance -= 20;
                if (s >= 4 && (kick_src(s - 4, ddens) || snare_src(s - 4, ddens)))
                    chance += 20;
                if (silent >= 6)
                    chance = 100;
            }
        }
        if ((int)(h % 100u) >= chance) {
            silent += div;
            continue;
        }
        silent = 0;
        played |= (uint16_t)(1u << s);
        idx = g.c->m_shape == 64 ? (int)((h >> 8) % (uint32_t)sp) : (int)(marble_place(h) * (uint32_t)sp >> 16);
        idx -= sp * 3 / 11;
        if (g.c->m_steps < 127) {                        /* fewer degrees: fewer steps, so the range stays */
            uint16_t t = marble_steps(m, c.root);
            idx = idx * bits12(t) / (bits12(m) ? bits12(m) : 1);
            m = t;
        }
        gput_note(GP_RAND, tpos(s, 0, sty()->jit, &r), ladder(root_note(c, base), idx, m),
                  60 + (int)((h >> 16) % 50u), len * (30 + (int)((h >> 24) % 60u)) / 100);
    }
}

/* Chords make room for the lead (0.5.4): a chord note that would sound while a lead note starts is shortened to
 * end there (room 1), or one that overlaps a lead note at all is left out (room 2). The chords and lead of this bar
 * only, after both are made: a pure function of the bar. */
static void make_room(void)
{
    gev_t *c = &wb->ev[GEV_OFF[GP_CHORD]];
    const gev_t *l = &wb->ev[GEV_OFF[GP_LEAD]];
    int nc = wb->n[GP_CHORD], nl = wb->n[GP_LEAD], i, j, k = 0;
    for (i = 0; i < nc; i++) {
        int keep = 1;
        if (c[i].kind == EV_NOTE)
            for (j = 0; j < nl; j++) {
                int cs = c[i].pos, ce = c[i].pos + c[i].dur, ls = l[j].pos, le = l[j].pos + l[j].dur;
                if (l[j].kind != EV_NOTE || le <= cs || ls >= ce)
                    continue;
                if (g.c->room == 2 || ls <= cs) {
                    keep = 0;
                    break;
                }
                c[i].dur = (uint16_t)(ls - cs);          /* (the first lead note inside it decides) */
            }
        if (keep)
            c[k++] = c[i];
    }
    wb->n[GP_CHORD] = k;
}

/* ------------------------------------------------------------- playing --- */
static void gsend(uint32_t st, uint32_t ch, uint32_t a, uint32_t b)
{
    if (g.out)
        g.out(st | (ch & 15u), a & 127u, b & 127u);
}
void gen_part_off(int p)
{
    int i;
    for (i = 0; i < GEN_MAXON; i++)
        if (gon[p][i].left) {
            gsend(0x80u, gon[p][i].ch, gon[p][i].note, 0);
            gon[p][i].left = 0;
        }
    if (g.moved[p] & 1u)
        gsend(0xB0u, g.c->part[p].chan, devp(p)->cc, devp(p)->rest);
    if (g.moved[p] & 2u)
        gsend(0xE0u, g.c->part[p].chan, 0, 64);
    g.moved[p] = 0;
}
static void note_on(int p, const gev_t *e)
{
    int i, free = -1, oldest = 0;
    for (i = 0; i < GEN_MAXON; i++) {
        gon_t *o = &gon[p][i];
        if (o->left && o->note == e->a && o->ch == e->ch) {   /* the same note again: its off first */
            gsend(0x80u, o->ch, o->note, 0);
            o->left = 0;
        }
        if (!o->left)
            free = i;
        else if (o->left < gon[p][oldest].left || !gon[p][oldest].left)
            oldest = i;
    }
    if (free < 0) {                                      /* full: the note nearest its end makes room */
        gsend(0x80u, gon[p][oldest].ch, gon[p][oldest].note, 0);
        free = oldest;
    }
    gon[p][free].note = e->a;
    gon[p][free].ch = e->ch;
    gon[p][free].left = e->dur;
    gsend(0x90u, e->ch, e->a, e->b);
}
static uint32_t cfg_sig(void)
{
    const uint8_t *b = (const uint8_t *)g.c;
    uint32_t h = 2166136261u, i;
    for (i = 0; i < sizeof *g.c; i++)
        h = (h ^ b[i]) * 16777619u;
    return h ^ (uint32_t)(g.man_pc + 1) * 0x9E3779B9u;
}
static int cur_good(uint32_t sig) { return g.run && gb[g.cur].ok && gb[g.cur].bar == g.bar && gb[g.cur].sig == sig; }
/* the buffer becomes the one playing; late: mid bar, from where the bar is (within its first eighth of a beat:
 * from its top, a little late rather than without its downbeat) */
static void swap_in(int b, int late)
{
    int p;
    g.cur = (uint8_t)b;
    for (p = 0; p < GEN_NPARTS; p++) {
        uint16_t i = 0;
        if (late && g.pos > 12u)
            while (i < gb[b].n[p] && gb[b].ev[GEV_OFF[p] + i].pos < g.pos)
                i++;
        g.i[p] = i;
    }
}
/* The main loop's part: keeps the bar playing made from the settings as they are, and the next bar ready
 * (stopped: the first). All the building is here; the lock is held only to look and to swap. */
/* Chopped and screwed's chop: in the last bar of each phrase the second half replays the first, every part. A pure
 * function of the bar, so parts recorded one after another still line up. Notes reaching past the half bar are cut
 * there, so a mono part's note never overlaps its own copy. */
static void chop(int p)
{
    gev_t *e = &wb->ev[GEV_OFF[p]];
    int n = wb->n[p], cap = GEV_OFF[p + 1] - GEV_OFF[p], i, k = 0, half = (int)GEN_BAR / 2;
    if (!(STYLE_META[g.c->style % GEN_NSTYLES].fx & SFX_CHOP) || (int)(bb % (uint32_t)phrase_len()) != phrase_len() - 1)
        return;
    for (i = 0; i < n; i++)                              /* keep the first half */
        if (e[i].pos < half) {
            e[k] = e[i];
            if (e[k].pos + e[k].dur > half)
                e[k].dur = (uint16_t)(half - e[k].pos);
            k++;
        }
    n = k;
    for (i = 0; i < k && n < cap; i++) {                 /* and play it again */
        e[n] = e[i];
        e[n++].pos = (uint16_t)(e[i].pos + half);
    }
    wb->n[p] = (uint16_t)n;
    if (p == GP_DRUMS)
        for (i = 0; i < GEN_NLANES; i++)
            wb->lanes[i] = (uint16_t)((wb->lanes[i] & 0xFFu) | (wb->lanes[i] & 0xFFu) << 8);
}
/* The melodic cowbell. The drum's tune CC goes out half a sixteenth before each cowbell hit (the Tanzbär needs about
 * 40 ms: that is 62 ms at 120 BPM, 40 at 187). A style with a written bell line (batch [D], BELL_LINES) plays its
 * scale degrees; any other style, with Cowbell pitch on Melodic, plays chord tones (root, fifth or third by the seed,
 * the bar and the step, the bar's first hit on the key's root). The bar's first hit is tuned at the end of the bar
 * before, so each bar is still a pure function of the bar. A pitch the drum can't play becomes its fifth, then its
 * fourth, then the key's third. */
#define BELL_LEAD (GEN_STEP / 2)
enum { BL_PLAIN, BL_DRIFT, BL_B7 };      /* as written; one note moving every 4th bar; the last hit b7 every 4th bar */
/* the scale degree (1..7, in the progression's mode) of the cowbell hit on each step; two bars, '.' no hit */
static const struct { uint8_t style, kind; const char *bar[2]; } BELL_LINES[] = {
    {GS_BELLFUNK, BL_PLAIN, {"1..1..3.1..4.3..", "1..1..3.5..4.7.."}},
    {GS_MIAMIBELL, BL_PLAIN, {"5.5...1.5.5...3.", "5.5...1.4.3...1."}},
    {GS_DETBELL, BL_DRIFT, {"1..3..5.4..3..1.", "1..3..5.4..3..1."}},
    {GS_AFROBELL, BL_B7, {"1.5.15.5.5.51.5.", "1.5.15.5.5.51.5."}},
};
static int bell_line_of(int style)
{
    int i;
    for (i = 0; i < (int)(sizeof BELL_LINES / sizeof BELL_LINES[0]); i++)
        if (BELL_LINES[i].style == style)
            return i;
    return -1;
}
static int scale_semis(int deg)          /* a scale degree's semitones above the key's root, in the current mode */
{
    uint16_t m = SC_MASK[cur_prog()->scale];
    int i, n = 0;
    for (i = 0; i < 12; i++)
        if ((m >> i & 1u) && ++n == deg)
            return i;
    return 0;
}
static int nth_hit(const char *l, int k) /* the step of the k-th written hit (k >= 1), -1 if fewer */
{
    int i;
    for (i = 0; i < 16; i++)
        if (l[i] != '.' && k-- == 0)
            return i;
    return -1;
}
static int hits_in(const char *l)
{
    int i, n = 0;
    for (i = 0; i < 16; i++)
        n += l[i] != '.';
    return n;
}
/* The degree of the written line's hit at `step` in `bar` (a hit the variation added takes the written hit before
 * it). Mutate changes at most one note a bar, never the first. */
static int bell_deg(int li, uint32_t bar, int step)
{
    const char *l = BELL_LINES[li].bar[bar & 1u];
    int i, d = 1, n = hits_in(l), last = nth_hit(l, n - 1);
    uint32_t e;
    for (i = 0; i <= step && i < 16; i++)
        if (l[i] != '.')
            d = l[i] - '0';
    if (BELL_LINES[li].kind == BL_DRIFT && n > 1) {      /* the last four moves (one every 4th bar) still stand */
        static const uint8_t PENT[5] = {1, 3, 4, 5, 7};
        uint32_t now = bar / 4u, from = now > 3u ? now - 3u : 1u;
        for (e = from; e <= now && now; e++)
            if (step == nth_hit(l, 1 + (int)(gh(g.c->seed, 0xD71Fu + e) % (uint32_t)(n - 1))))
                d = PENT[gh(g.c->seed, 0xD720u + e) % 5u];
    }
    if (BELL_LINES[li].kind == BL_B7 && bar % 4u == 3u && step == last)
        d = 7;
    if (g.c->mutate && n > 1) {
        uint32_t ep = bar / (uint32_t)phrase_len() / (uint32_t)(9 - gclamp(g.c->mutate, 1, 8));
        uint32_t h = gh(g.c->seed + ep * 2u + (bar & 1u), 0xB3E1u);
        if (ep && step == nth_hit(l, 1 + (int)(h % (uint32_t)(n - 1))))
            d = (h >> 16 & 1u) ? d % 7 + 1 : (d + 5) % 7 + 1;   /* a scale step up or down */
    }
    return d;
}
static int bell_val(int pc)
{
    const int off[4] = {0, 7, 5, g.c->minor ? 3 : 4};
    int i;
    for (i = 0; i < 4; i++)
        if (g.c->bell_tab[(pc + off[i]) % 12] < 128u)
            return g.c->bell_tab[(pc + off[i]) % 12];
    return -1;
}
static void bell_line(void)
{
    const gev_t *e = &wb->ev[GEV_OFF[GP_DRUMS]];
    int n = wb->n[GP_DRUMS], i, v, note = g.c->lane_note[L_COWB], ch = lane_ch(L_COWB), li = bell_line_of(g.c->style);
    for (i = 0; i < n; i++) {
        int step, pc;
        if (e[i].kind != EV_NOTE || e[i].a != note || e[i].pos < BELL_LEAD)
            continue;
        step = (e[i].pos + GEN_STEP / 4) / GEN_STEP;     /* (a hit a little early still counts as its step) */
        if (step > 15)
            step = 15;
        if (li >= 0) {
            pc = g.c->root + scale_semis(bell_deg(li, bb, step));
        } else {
            chord_t c = chord_step(step);
            uint32_t k = gh(g.c->seed + bb * 16u + (uint32_t)step, 0xBE11u) % 5u;
            pc = c.root + (k < 2u ? 0 : k < 4u ? QIV[c.q][2] : QIV[c.q][1]);
        }
        v = bell_val(pc % 12);
        if (v >= 0)
            gput_ev(GP_DRUMS, e[i].pos - BELL_LEAD, EV_CC, g.c->bell_cc, v, 1, ch);
    }
    v = bell_val((g.c->root + (li >= 0 ? scale_semis(bell_deg(li, bb + 1u, 0)) : 0)) % 12);
    if (v >= 0)                                          /* the next bar's first hit */
        gput_ev(GP_DRUMS, (int)GEN_BAR - BELL_LEAD, EV_CC, g.c->bell_cc, v, 1, ch);
}
/* Sound locks (0.5.1), like an Elektron's parameter locks: in the styles below, single drum hits get their own
 * sound, a CC half a sixteenth before the hit, and the style's value back before the next hit of that voice (or at the
 * bar's end): the kick's decay longer on the bar's last kick (the 808 boom), the snare's tone looser on ghost notes,
 * the open hat choked in fill bars, the clap's decay a little different in each bar. Bar events, a pure function of
 * the bar; off (gen_cfg_t.locks 0) changes nothing. The CCs and the style's values come from the engine (the Tanzbär
 * profile); a voice without a CC is left alone. */
#define LOCK_LEAD ((int)GEN_STEP / 2)
#define LK(v) (1u << (v))
static const struct { uint8_t style, mask; } LOCK_STYLES[] = {
    {GS_MEMPHIS, LK(GEN_LOCK_KICK)},
    {GS_MIAMI, LK(GEN_LOCK_KICK) | LK(GEN_LOCK_CLAP)},
    {GS_JERSEY, LK(GEN_LOCK_KICK) | LK(GEN_LOCK_CLAP)},
    {GS_DILLA, LK(GEN_LOCK_SNARE)},
    {GS_NATIVE, LK(GEN_LOCK_SNARE)},
    {GS_BROKEN, LK(GEN_LOCK_SNARE)},
    {GS_FRENCH, LK(GEN_LOCK_OHAT)},
    {GS_ITALO, LK(GEN_LOCK_OHAT)},
    {GS_JACK, LK(GEN_LOCK_CLAP)},
    {GS_BALLROOM, LK(GEN_LOCK_CLAP)},
};
static const uint8_t LOCK_LANE[GEN_NLOCK] = {L_KICK, L_SNARE, L_OHH, L_CLAP};
static int lock_mask(int style)
{
    int i;
    for (i = 0; i < (int)(sizeof LOCK_STYLES / sizeof LOCK_STYLES[0]); i++)
        if (LOCK_STYLES[i].style == style)
            return LOCK_STYLES[i].mask;
    return 0;
}
/* the voice's value on a hit (its base when the hit keeps the style's sound) */
static int lock_val(int k, const gev_t *e, int last_kick, int clap_ofs)
{
    int b = g.c->lock_base[k];
    switch (k) {
    case GEN_LOCK_KICK: return e->pos == last_kick ? gclamp(b + 30, 0, 127) : b;
    case GEN_LOCK_SNARE: return e->b < 80 ? gclamp(b - 25, 0, 127) : b;
    case GEN_LOCK_OHAT: return fill_bar(bb) ? gclamp(b - 35, 0, 127) : b;
    default: return gclamp(b + clap_ofs, 0, 127);
    }
}
static void sound_locks(void)
{
    const gev_t *e = &wb->ev[GEV_OFF[GP_DRUMS]];
    int n = wb->n[GP_DRUMS], mask = lock_mask(g.c->style), k, i, j;
    int clap_ofs = (int)(gh(g.c->seed, 0xC1A9u + bb) % 25u) - 12;
    if (!g.c->locks || !mask)
        return;
    for (k = 0; k < GEN_NLOCK; k++) {
        int note = g.c->lane_note[LOCK_LANE[k]], ch = lane_ch(LOCK_LANE[k]), cc = g.c->lock_cc[k], base, last = -1;
        if (!(mask >> k & 1) || !cc || cc > 127)
            continue;
        base = g.c->lock_base[k];
        for (i = 0; i < n; i++)                          /* (events aren't in time order yet) */
            if (e[i].kind == EV_NOTE && e[i].a == note && e[i].ch == ch && e[i].pos > last)
                last = e[i].pos;
        for (i = 0; i < n; i++) {
            int v, next = (int)GEN_BAR, nv = base;
            if (e[i].kind != EV_NOTE || e[i].a != note || e[i].ch != ch || (v = lock_val(k, &e[i], last, clap_ofs)) == base)
                continue;
            gput_ev(GP_DRUMS, e[i].pos > LOCK_LEAD ? e[i].pos - LOCK_LEAD : 0, EV_CC, cc, v, 1, ch);
            for (j = 0; j < n; j++)                      /* the voice's next hit: back to the style's value before it */
                if (e[j].kind == EV_NOTE && e[j].a == note && e[j].ch == ch && e[j].pos > e[i].pos && e[j].pos < next) {
                    next = e[j].pos;
                    nv = lock_val(k, &e[j], last, clap_ofs);
                }
            if (nv == base)
                gput_ev(GP_DRUMS, next - LOCK_LEAD > e[i].pos ? next - LOCK_LEAD : e[i].pos + 1, EV_CC, cc, base, 1, ch);
        }
    }
}
#undef LK

/* The Simmons lanes only in some bars: a grid is one bar, these designs want it rarer. bars: bit n = the bar n+1 of
 * every 4 keeps the grid's hits; answer: in the bars not kept, another pattern instead (Electro's laser answer). */
static const struct { uint8_t style, lane, bars; const char *answer; } ZAP_RULES[] = {
    {GS_BELLFUNK, L_SIM1, 0x8, 0},
    {GS_MIAMIBELL, L_SIM1, 0xA, 0},
    {GS_ELECTRO, L_SIM1, 0x7, "0070000000707077"},
    {GS_ELECTRO2, L_SIM1, 0xA, 0},
    {GS_GATED, L_SIM2, 0x8, 0},
    {GS_ITALO, L_SIM1, 0xA, 0},
    {GS_MIAMI, L_SIM1, 0xA, 0},
    {GS_FREESTYLE, L_SIM1, 0xA, 0},
    {GS_FREESTYLE, L_SIM2, 0x8, 0},
};
static int is_grid_hit(int lane, int pos)  /* a hit where the style's grid has one (not a fill's) */
{
    int step = (pos + GEN_STEP / 4) / GEN_STEP;
    return step < 16 && sty()->lane[lane] && sty()->lane[lane][step] != '0';
}
static void zap_bars(void)
{
    gev_t *e = &wb->ev[GEV_OFF[GP_DRUMS]];
    int i, k, n, r, s, note, ch;
    for (r = 0; r < (int)(sizeof ZAP_RULES / sizeof ZAP_RULES[0]); r++) {
        if (ZAP_RULES[r].style != g.c->style || (ZAP_RULES[r].bars >> (bb % 4u) & 1u))
            continue;
        note = g.c->lane_note[ZAP_RULES[r].lane];
        ch = lane_ch(ZAP_RULES[r].lane);
        for (i = 0, k = 0, n = wb->n[GP_DRUMS]; i < n; i++)
            if (!(e[i].kind == EV_NOTE && e[i].a == note && e[i].ch == ch && is_grid_hit(ZAP_RULES[r].lane, e[i].pos)))
                e[k++] = e[i];
        wb->n[GP_DRUMS] = (uint16_t)k;
        if (ZAP_RULES[r].answer)
            for (s = 0; s < 16; s++)
                if (ZAP_RULES[r].answer[s] != '0')
                    drum_put(ZAP_RULES[r].lane, s * (int)GEN_STEP + ((s & 1) ? swing_sub() : 0), 100);
    }
}
/* The pump (SFX_PUMP, LA beats): the chord part's expression (CC 11) dips on each kick and swells back by the next
 * 8th (or the next kick), the sidechain sound on the chords. Bar events on the chord part's channel, from the kicks
 * the drum part plays this bar (built before the chords), or from the style's grid when the drums are off. */
#define PUMP_LOW 40
static void pump(void)
{
    const gev_t *e = &wb->ev[GEV_OFF[GP_DRUMS]];
    int kp[16], nk = 0, i, j, ch = g.c->part[GP_CHORD].chan, eighth = 2 * (int)GEN_STEP;
    if (!(STYLE_META[g.c->style % GEN_NSTYLES].fx & SFX_PUMP))
        return;
    if (g.c->part[GP_DRUMS].on) {
        int note = g.c->lane_note[L_KICK], kch = lane_ch(L_KICK);
        for (i = 0; i < wb->n[GP_DRUMS] && nk < 16; i++)
            if (e[i].kind == EV_NOTE && e[i].a == note && e[i].ch == kch && (!nk || e[i].pos >= kp[nk - 1] + 6))
                kp[nk++] = e[i].pos;
    } else if (sty()->lane[L_KICK]) {
        for (i = 0; i < 16; i++) {
            int gho, w = wdec(sty()->lane[L_KICK][i], &gho);
            if (w >= 7 && !gho)
                kp[nk++] = i * (int)GEN_STEP;
        }
    }
    for (i = 0; i < nk; i++) {
        int a = kp[i], z = (a / eighth + 1) * eighth;
        if (i + 1 < nk && kp[i + 1] - 1 < z)
            z = kp[i + 1] - 1;
        if (z > (int)GEN_BAR - 1)
            z = (int)GEN_BAR - 1;
        gput_ev(GP_CHORD, a, EV_CC, 11, PUMP_LOW, 1, ch);
        if (z - a >= 3)
            for (j = 1; j <= 3; j++)
                gput_ev(GP_CHORD, a + (z - a) * j / 3, EV_CC, 11, PUMP_LOW + (127 - PUMP_LOW) * j / 3, 1, ch);
        else
            gput_ev(GP_CHORD, gclamp(a + 1, 0, (int)GEN_BAR - 1), EV_CC, 11, 127, 1, ch);
    }
}
/* The filter sweep (SFX_SWEEP, HSE): the chord part's cutoff (the device's CC, 74 without one) follows a triangle over
 * 16 bars, closed at the phrase boundary and open in the middle, four steps a bar; it replaces the chords' own Flair
 * moves on that CC. Dub techno's is narrower and wanders a little bar by bar (from the seed). A pure function of the
 * bar, so recorded parts and SPP line up. */
static int part_cut_cc(int p, int *rest)          /* a part's cutoff CC: its device's, 74 without one */
{
    const gdev_t *d = devp(p);
    if (rest)
        *rest = d->cc ? d->rest : 127;
    return d->cc ? d->cc : 74;
}
int gen_sweep_cc(int *rest) { return part_cut_cc(GP_CHORD, rest); }
int gen_wobble_cc(int *rest) { return part_cut_cc(GP_BASS, rest); }
/* Break edits (SFX_EDIT, Jungle; drums only): in every bar but the phrase's first, the seed picks one or two beats
 * and replaces each with another beat of the bar, with a snare roll (16ths, rising), or with itself played backwards
 * (its four steps in reverse order). Done after the drums are built and before the bass, so the kick the bass and
 * melodic kick follow is the edited one. A pure function of seed and bar, like chop(). */
static void break_edits(void)
{
    gev_t *e = &wb->ev[GEV_OFF[GP_DRUMS]];
    int n = wb->n[GP_DRUMS], cap = GEV_OFF[GP_DRUMS + 1] - GEV_OFF[GP_DRUMS], B = 4 * (int)GEN_STEP, S = (int)GEN_STEP;
    int i, j, k, l, ne, m, tgt[2], op[2], src = 0, cut = 0;
    uint32_t r;
    if (!(STYLE_META[g.c->style % GEN_NSTYLES].fx & SFX_EDIT) || bb % (uint32_t)phrase_len() == 0u)
        return;
    r = gh(pseed(GP_DRUMS, 0xED17u), bb);
    ne = 1 + (int)(r & 1u);
    tgt[0] = (int)(r >> 1 & 3u);
    tgt[1] = (tgt[0] + 1 + (int)(r >> 3 & 0xFFu) % 3) & 3;
    op[0] = (int)(r >> 11 & 0xFFu) % 3;                 /* 0 another beat, 1 a snare roll, 2 backwards */
    op[1] = (int)(r >> 19 & 0xFFu) % 3;
    for (m = (int)(r >> 27 & 3u); ; m = (m + 1) & 3)   /* the beat copied: one not edited */
        if (m != tgt[0] && (ne < 2 || m != tgt[1])) {
            src = m;
            break;
        }
    for (k = 0; k < ne; k++) {
        if (op[k] == 2) {
            for (i = 0; i < n; i++)
                if ((int)e[i].pos / B == tgt[k]) {
                    int o = (int)e[i].pos - tgt[k] * B, st = o / S;
                    e[i].pos = (uint16_t)(tgt[k] * B + (3 - st) * S + (o - st * S));
                }
        } else
            cut |= 1 << tgt[k];
    }
    for (i = 0, j = 0; i < n; i++)                      /* the replaced beats go */
        if (!(cut >> ((int)e[i].pos / B & 3) & 1) || (int)e[i].pos >= (int)GEN_BAR)
            e[j++] = e[i];
    n = j;
    for (k = 0; k < ne; k++)
        if (op[k] == 0)
            for (i = 0; i < j && n < cap; i++)
                if ((int)e[i].pos / B == src) {
                    e[n] = e[i];
                    e[n++].pos = (uint16_t)(e[i].pos + (tgt[k] - src) * B);
                }
    wb->n[GP_DRUMS] = (uint16_t)n;
    for (k = 0; k < ne; k++)
        if (op[k] == 1)
            for (i = 0; i < 4; i++)
                drum_put(L_SNARE, tgt[k] * B + i * S, 60 + i * 15);
    for (l = 0; l < GEN_NLANES; l++)                    /* the lanes' steps, as played now */
        wb->lanes[l] = 0;
    for (i = 0; i < (int)wb->n[GP_DRUMS]; i++)
        for (l = 0; l < GEN_NLANES; l++)
            if (e[i].kind == EV_NOTE && e[i].a == g.c->lane_note[l] && e[i].ch == lane_ch(l)) {
                wb->lanes[l] |= (uint16_t)(1u << (gclamp(e[i].pos, 0, (int)GEN_BAR - 1) / S));
                break;
            }
}
/* Polymeter (SFX_POLY, Braindance; Style drum mode): the closed hat, tom and claves loop at their own lengths,
 * Euclidean patterns whose lengths and hits the seed picks once (hat 10-14 steps with 7 hits in 12's proportion,
 * tom 9-11 with 3, claves 5-7 with 3), counted from the phrase start, so every lane lines up again with the kick and
 * snare at each phrase. Replaces those lanes as the grid wrote them; in a fill bar the last beat keeps its fill.
 * A pure function of seed and bar. */
static int euclid_hit(int len, int hits, int i)  /* hits spread evenly over len steps (Bresenham), step i */
{
    return (i * hits) % len < hits;
}
static void polymeter(void)
{
    static const uint8_t PL[3] = {L_CHH, L_TOML, L_CLAVE};
    gev_t *e = &wb->ev[GEV_OFF[GP_DRUMS]];
    const style_t *st = sty();
    int n = wb->n[GP_DRUMS], S = (int)GEN_STEP, i, j, k, len[3], hits[3], end = 16, base;
    uint32_t h;
    rng_t r;
    if (!(STYLE_META[g.c->style % GEN_NSTYLES].fx & SFX_POLY) || g.c->part[GP_DRUMS].mode != DM_STYLE)
        return;
    h = gh(pseed(GP_DRUMS, 0x9E1u), 0u);
    len[0] = 10 + (int)(h % 5u), hits[0] = (len[0] * 7 + 6) / 12;
    len[1] = 9 + (int)(h / 5u % 3u), hits[1] = 3;
    len[2] = 5 + (int)(h / 15u % 3u), hits[2] = 3;
    if (fill_bar(bb))
        end = 12;
    for (i = 0, j = 0; i < n; i++) {                    /* those lanes, as the grid wrote them, go */
        int drop = 0, st16 = ((int)e[i].pos + S / 2) / S;
        for (k = 0; k < 3; k++)
            drop |= e[i].kind == EV_NOTE && e[i].a == g.c->lane_note[PL[k]] && e[i].ch == lane_ch(PL[k]) && st16 < end;
        if (!drop)
            e[j++] = e[i];
    }
    wb->n[GP_DRUMS] = (uint16_t)j;
    r.s = gh(pseed(GP_DRUMS, 0x9E2u), bb);
    base = (int)(bb % (uint32_t)phrase_len()) * 16;
    for (k = 0; k < 3; k++) {
        int l = PL[k];
        wb->lanes[l] &= (uint16_t)~((1u << end) - 1u);
        for (i = 0; i < end; i++) {
            int a = (base + i) % len[k];
            if (!euclid_hit(len[k], hits[k], a) || (l == L_CHH && !hat_keep(i)))
                continue;
            drum_hit(l, dpos(l, i, lane_late(l), st->jit, &r), a == 0 ? 112 : l == L_CHH && (i & 1) ? 64 : 88);
        }
    }
}
/* Warble (SFX_WARBLE, Boards of Canada): the chords' and lead's pitch bend drifts up to 25 cents either way like worn
 * tape, a triangle of 3 bars with a slower one of 7 bars mixed in so it never quite repeats, eight bends a bar.
 * Bar events, a pure function of the bar; nothing on a device without pitch bend, nor in a bar where the part
 * bends by itself (the lead's slides). */
static void warble(int p)
{
    int t, rg;
    if (!(STYLE_META[g.c->style % GEN_NSTYLES].fx & SFX_WARBLE) || (p != GP_CHORD && p != GP_LEAD))
        return;
    rg = bend_range(p);
    if (!rg)
        return;
    for (t = 0; t < wb->n[p]; t++)                     /* a bar where the part bends by itself keeps its own */
        if (wb->ev[GEV_OFF[p] + t].kind == EV_BEND)
            return;
    for (t = 0; t < (int)GEN_BAR; t += (int)GEN_BAR / 8) {
        long a3 = (long)(bb % 3u) * GEN_BAR + t, a7 = (long)(bb % 7u) * GEN_BAR + t;
        int x3 = (int)(a3 * 200 / (3L * GEN_BAR)), x7 = (int)(a7 * 200 / (7L * GEN_BAR));
        int c3 = (x3 <= 100 ? x3 : 200 - x3) - 50, c7 = (x7 <= 100 ? x7 : 200 - x7) - 50;  /* -50..50 each */
        int cents = (c3 * 2 + c7) / 6, v = gclamp(8192 + cents * 8192 / (100 * rg), 0, 16383);
        gput_ev(p, t, EV_BEND, (uint8_t)(v & 127), (uint8_t)(v >> 7), 1, (uint8_t)g.c->part[p].chan);
    }
}
/* Drift (SFX_DRIFT, Autechre; Style drum mode): for this bar the drums play the Grids map at an X/Y that glides
 * between seeded points every 8 bars, so the groove morphs and does not come back; set before the parts are built
 * (so the bass follows the same kick) and put back after. A pure function of seed and bar. */
static int drift_xy(int axis)
{
    uint32_t k = bb / 8u, f = bb % 8u, s = pseed(GP_DRUMS, 0xD1F7u + (uint32_t)axis);
    int a = 16 + (int)(gh(s, k) % 96u), z = 16 + (int)(gh(s, k + 1u) % 96u);
    return a + (z - a) * (int)f / 8;
}
/* The bounce (SFX_TRIP, Jersey club): the kicks on steps 11, 13 and 15 (counting from 1) play as 8th-note triplets
 * across the last beat and a half, so the second lands a third of a 16th late and the third two thirds late. Kick
 * events only; the parts coupled to the kick keep the straight steps. A pure function of the bar. */
static void kick_triplets(void)
{
    gev_t *e = &wb->ev[GEV_OFF[GP_DRUMS]];
    int i, S = (int)GEN_STEP, note = g.c->lane_note[L_KICK], ch = lane_ch(L_KICK);
    if (!(STYLE_META[g.c->style % GEN_NSTYLES].fx & SFX_TRIP))
        return;
    for (i = 0; i < wb->n[GP_DRUMS]; i++)
        if (e[i].kind == EV_NOTE && e[i].a == note && e[i].ch == ch) {
            int st = ((int)e[i].pos + S / 2) / S;
            if (st == 12 || st == 14)
                e[i].pos = (uint16_t)gclamp((int)e[i].pos + (st - 10) * S / 3, 0, (int)GEN_BAR - 1);
        }
}
/* Sections (SFX_SECT, Grime): 16 bars in two halves of 8, counted from the song start: the first half plays the bass
 * and chord stabs with the drums (no lead), the second drops them for the lead; the last beat of each half is a stop,
 * only a kick and a clap on its first 16th. A pure function of the bar. */
static void sections(int p)
{
    int second = (int)(bb % 16u) >= 8, i, j, B = 3 * 4 * (int)GEN_STEP;
    if (!(STYLE_META[g.c->style % GEN_NSTYLES].fx & SFX_SECT))
        return;
    if ((second && (p == GP_BASS || p == GP_CHORD)) || (!second && p == GP_LEAD))
        wb->n[p] = 0;
    if (p == GP_DRUMS && bb % 8u == 7u) {
        gev_t *e = &wb->ev[GEV_OFF[GP_DRUMS]];
        for (i = 0, j = 0; i < wb->n[GP_DRUMS]; i++)
            if ((int)e[i].pos < B - (int)GEN_STEP / 2)
                e[j++] = e[i];
        wb->n[GP_DRUMS] = (uint16_t)j;
        for (i = 0; i < GEN_NLANES; i++)
            wb->lanes[i] &= 0x0FFFu;
        drum_put(L_KICK, B, 110);
        drum_put(L_CLAP, B, 110);
    }
}
/* The wobble (SFX_WOBBLE, Dubstep): the bass part's cutoff (its device's CC, 74 without one) rides a triangle LFO, its
 * rate picked by the seed each bar (1/4, 1/8, 1/8 triplet, 1/16), a CC every 32nd triplet; it replaces the bass's
 * own Flair moves on that CC. Bar events on the bass part's channel. */
static void wobble(void)
{
    static const uint8_t PER[4] = {96, 48, 32, 24};     /* subticks: a quarter, an 8th, an 8th triplet, a 16th */
    gev_t *e = &wb->ev[GEV_OFF[GP_BASS]];
    int cc = part_cut_cc(GP_BASS, 0), ch = g.c->part[GP_BASS].chan, i, n = 0, t, per;
    if (!(STYLE_META[g.c->style % GEN_NSTYLES].fx & SFX_WOBBLE))
        return;
    for (i = 0; i < wb->n[GP_BASS]; i++)
        if (!(e[i].kind == EV_CC && e[i].a == cc && e[i].ch == ch))
            e[n++] = e[i];
    wb->n[GP_BASS] = (uint16_t)n;
    if (g.c->style == GS_APHEX || g.c->style == GS_ACIDTECH) {   /* the 303's cutoff drifting over 8 bars (Acid
                                                                    * techno: 4), 40..110 */
        int nb = g.c->style == GS_ACIDTECH ? 4 : 8;
        for (t = 0; t < (int)GEN_BAR; t += 48) {
            int a = (int)(bb % (uint32_t)nb) * (int)GEN_BAR + t, x = a * 254 / (nb * (int)GEN_BAR);
            int v = x <= 127 ? x : 254 - x;
            gput_ev(GP_BASS, t, EV_CC, (uint8_t)cc, (uint8_t)(40 + v * 70 / 127), 1, (uint8_t)ch);
        }
        return;
    }
    if (g.c->style == GS_DNB) {                         /* the Reese: one slow sweep over two bars, 30..100 */
        for (t = 0; t < (int)GEN_BAR; t += 32) {
            int a = (int)(bb & 1u) * (int)GEN_BAR + t, x = a * 254 / (2 * (int)GEN_BAR), v = x <= 127 ? x : 254 - x;
            gput_ev(GP_BASS, t, EV_CC, (uint8_t)cc, (uint8_t)(30 + v * 70 / 127), 1, (uint8_t)ch);
        }
        return;
    }
    per = PER[gh(pseed(GP_BASS, 0x30Bu), bb) % 4u];
    for (t = 0; t < (int)GEN_BAR; t += 8) {
        int x = (t % per) * 254 / per, v = x <= 127 ? x : 254 - x;
        gput_ev(GP_BASS, t, EV_CC, (uint8_t)cc, (uint8_t)(24 + v * 86 / 127), 1, (uint8_t)ch);
    }
}
static void sweep(void)
{
    gev_t *e = &wb->ev[GEV_OFF[GP_CHORD]];
    int cc = gen_sweep_cc(0), ch = g.c->part[GP_CHORD].chan, i, n = 0, j, lo = 24, hi = 112, wob = 0;
    if (!(STYLE_META[g.c->style % GEN_NSTYLES].fx & SFX_SWEEP))
        return;
    for (i = 0; i < wb->n[GP_CHORD]; i++)                       /* the Flair's moves on that CC give way */
        if (!(e[i].kind == EV_CC && e[i].a == cc && e[i].ch == ch))
            e[n++] = e[i];
    wb->n[GP_CHORD] = (uint16_t)n;
    if (g.c->style == GS_DUBTECH) {
        lo = 36, hi = 88;
        wob = (int)(gh(pseed(GP_CHORD, 7), bb) % 13u) - 6;
    }
    for (j = 0; j < 4; j++) {
        int x = (int)(bb % 16u) * 4 + j, v = lo + (hi - lo) * (32 - (x > 32 ? x - 32 : 32 - x)) / 32;
        gput_ev(GP_CHORD, j * 4 * (int)GEN_STEP, EV_CC, (uint8_t)cc, (uint8_t)gclamp(v + wob, 0, 127), 1, (uint8_t)ch);
    }
}
void gen_service(void)
{
    uint32_t sig, target, f;
    int b, p, drift;
    uint8_t gx0 = 0, gy0 = 0;
    if (!g.c)
        return;
    sig = cfg_sig();
    f = GEN_LOCK();
    b = 1 - g.cur;
    target = !g.run ? 0u : cur_good(sig) ? g.bar + 1u : g.bar;
    if (gb[b].ok && gb[b].bar == target && gb[b].sig == sig) {
        GEN_UNLOCK(f);
        return;
    }
    gb[b].ok = 0;                                        /* claimed: the tick leaves it alone */
    GEN_UNLOCK(f);
    wb = &gb[b];
    bb = target;
    memset(wb->n, 0, sizeof wb->n);
    memset(wb->lanes, 0, sizeof wb->lanes);
    drift = (STYLE_META[g.c->style % GEN_NSTYLES].fx & SFX_DRIFT) && g.c->part[GP_DRUMS].mode == DM_STYLE;
    if (drift) {
        gx0 = g.c->gx, gy0 = g.c->gy;
        g.c->part[GP_DRUMS].mode = DM_GRIDS;
        g.c->gx = (uint8_t)drift_xy(0), g.c->gy = (uint8_t)drift_xy(1);
    }
    for (p = 0; p < GEN_NPARTS; p++) {
        if (!g.c->part[p].on)
            continue;
        switch (p) {
        case GP_DRUMS: build_drums(); break;
        case GP_CHORD: build_chords(); break;
        case GP_ARP: build_arp(); break;
        case GP_BASS: build_bass(); break;
        case GP_LEAD: build_lead(); break;
        case GP_KICK: build_kick(); break;
        default: build_marble(); break;
        }
        chop(p);
        sections(p);
        warble(p);
        if (p == GP_DRUMS) {
            break_edits();
            polymeter();
            kick_triplets();
            zap_bars();
        }
        if (p == GP_BASS)
            wobble();
        if (p == GP_DRUMS && g.c->bell && g.c->bell_cc)
            bell_line();
        if (p == GP_DRUMS)
            sound_locks();
        if (p == GP_CHORD) {
            pump();
            sweep();
        }
        sort_ev(p);
    }
    if (drift) {
        g.c->part[GP_DRUMS].mode = DM_STYLE;
        g.c->gx = gx0, g.c->gy = gy0;
    }
    if (g.c->room && g.c->part[GP_CHORD].on && g.c->part[GP_LEAD].on)
        make_room();
    wb->bar = target;
    wb->sig = sig;
    f = GEN_LOCK();
    wb->ok = 1;
    if (g.run && g.bar == target && !cur_good(sig))
        swap_in(b, 1);
    GEN_UNLOCK(f);
}
/* The tick's part: note offs counted down, the events due sent, the bar turned over. No building, no sorting. */
void gen_subtick(void)
{
    const gbuf_t *c;
    int p, i;
    if (!g.run || !g.c)
        return;
    c = &gb[g.cur];
    for (p = 0; p < GEN_NPARTS; p++) {
        if (!g.c->part[p].on) {
            if (g.moved[p] || gen_part_heard(p))
                gen_part_off(p);
            continue;
        }
        for (i = 0; i < GEN_MAXON; i++)
            if (gon[p][i].left && --gon[p][i].left == 0)
                gsend(0x80u, gon[p][i].ch, gon[p][i].note, 0);
        while (c->ok && g.i[p] < c->n[p] && c->ev[GEV_OFF[p] + g.i[p]].pos <= g.pos) {
            const gev_t *e = &c->ev[GEV_OFF[p] + g.i[p]++];
            if (e->kind == EV_NOTE)
                note_on(p, e);
            else
                gsend(e->kind == EV_CC ? 0xB0u : 0xE0u, e->ch, e->a, e->b);
        }
    }
    if (++g.pos >= GEN_BAR) {
        g.pos = 0;
        g.bar++;
        if (gb[1 - g.cur].ok && gb[1 - g.cur].bar == g.bar)
            swap_in(1 - g.cur, 0);
        else
            gb[g.cur].ok = 0;                            /* not ready (the main loop was held up): gen_service catches up */
    }
}
void gen_start(void)
{
    gen_stop();
    g.pos = 0;
    g.bar = 0;
    if (gb[1 - g.cur].ok && gb[1 - g.cur].bar == 0u)     /* (gen_service keeps the first bar ready while stopped) */
        swap_in(1 - g.cur, 0);
    else
        gb[g.cur].ok = 0;
    if (g.c && g.c->part[GP_KICK].on && g.c->mb_tune)    /* the MBase's pitch, so its notes land in tune */
        gsend(0xB0u, g.c->part[GP_KICK].chan, 101, g.c->mb_tune);
    g.run = 1;
}
void gen_continue(void) { g.run = 1; }
/* Song Position Pointer: the next Continue starts this many sixteenths from the top. The bar there is made fresh
 * (a pure function of the bar, so it is the bar the song had there) and joined partway through. */
void gen_locate(uint32_t sixteenths)
{
    int p;
    if (g.c)
        for (p = 0; p < GEN_NPARTS; p++)
            gen_part_off(p);
    g.bar = sixteenths / (GEN_BAR / GEN_STEP);
    g.pos = (sixteenths % (GEN_BAR / GEN_STEP)) * GEN_STEP;
    gb[0].ok = 0;                                        /* both: the one made ahead may hold this very bar, and */
    gb[1].ok = 0;                                        /* gen_service would then see nothing to do */
}
void gen_stop(void)
{
    int p;
    g.run = 0;
    gb[g.cur].ok = 0;
    if (!g.c)
        return;
    for (p = 0; p < GEN_NPARTS; p++)
        gen_part_off(p);
}
void gen_chord_key(int pc) { g.man_pc = (int8_t)(((pc % 12) + 12) % 12); }
int gen_running(void) { return g.run; }
int gen_bar(void) { return g.c ? (int)(g.bar % (uint32_t)phrase_len()) : 0; }
uint32_t gen_song_bar(void) { return g.bar; }
int gen_step(void) { return g.pos / (int)GEN_STEP; }
int gen_phrase_bars(void) { return g.c ? phrase_len() : 1; }
int gen_part_heard(int p)
{
    int i;
    for (i = 0; i < GEN_MAXON; i++)
        if (gon[p][i].left)
            return 1;
    return 0;
}
uint16_t gen_lane_steps(int lane) { return gb[g.cur].ok ? gb[g.cur].lanes[lane % GEN_NLANES] : 0u; }
void gen_chord_text(char *b)
{
    chord_t c;
    const char *s;
    uint32_t keep = bb;
    if (!g.c) {
        *b = 0;
        return;
    }
    c = chord_at(g.run ? g.bar : 0u, g.run && g.pos >= GEN_BAR / 2u);
    bb = keep;
    for (s = PC_NAME[c.root]; *s;)
        *b++ = *s++;
    for (s = QNAME[c.q]; *s;)
        *b++ = *s++;
    *b = 0;
}

/* ------------------------------------------------------------ settings --- */
void gen_defaults(gen_cfg_t *c)
{
    static const uint8_t DDEV[GEN_NPARTS] = {DV_TANZBAR, DV_JV1010, DV_VOLCAFM, DV_MB33, DV_MONOLOGUE, DV_MBASE,
                                             DV_JV1010};
    int i;
    memset(c, 0, sizeof *c);
    c->style = GS_PFUNK;
    c->root = 4;                                         /* E */
    c->minor = 1;
    c->phrase = 2;                                       /* four bars */
    c->feel = 8;
    c->seed = 1;
    c->gx = c->gy = 64;
    c->g_acc[0] = c->g_acc[1] = c->g_acc[2] = 28;   /* (0.5.3: the Grids controls as before) */
    c->g_len[0] = c->g_len[1] = c->g_len[2] = 16;
    c->g_res = 1;
    c->g_ohat = 1;
    c->m_bias = c->m_shape = 64;                    /* (0.5.4: Marble as before) */
    c->m_steps = 127;
    c->m_len = 16;
    c->bend = 2;
    c->mb_lo = 36;
    c->dset = DS_KIT;                                    /* (the Simmons' notes are the Tanzbar's snare and kick) */
    for (i = 0; i < GEN_NLANES; i++)
        c->lane_note[i] = LANE_DEFAULT[i];
    for (i = 0; i < GEN_NPARTS; i++) {
        c->part[i].dev = DDEV[i];
        c->part[i].dens = 64;
        c->part[i].flair = 48;
    }
    c->part[GP_ARP].dens = 100;
}
int gen_valid(const gen_cfg_t *c)
{
    int i;
    if (c->style >= GEN_NSTYLES || c->root > 11u || c->minor > 1u || c->phrase > 3u || c->feel > 16u || c->mutate > 8u ||
        c->prog > 1u || c->swing > 26u || c->dset >= GEN_NDSET || c->gx > 127u || c->gy > 127u || c->chaos > 127u || c->sim2 > 2u ||
        c->sim_chan > 16u || c->bend < 1u || c->bend > 12u || c->mb_xpose < -24 || c->mb_xpose > 24 || c->mb_tune > 127u ||
        c->mb_lo < 12u || c->mb_lo > 96u || c->hat_dens > 3u || c->flam > 127u || c->fill_every > 6u ||
        c->hat_straight > 100u || c->space > 100u || c->spread > 24u || c->couple > 3u || c->voice > 3u || c->ratchet > 127u ||
        c->bell > 1u || c->bell_cc > 127u || c->pocket > 1u || c->locks > 1u ||
        c->g_euclid > 1u || c->g_swing > 25u || c->g_res > 2u || c->g_ohat > 1u || c->ghosts > 127u ||
        c->m_memory > 1u || c->m_len < 1u || c->m_len > 16u || c->m_rhythm > 2u || c->room > 2u)
        return 0;
    for (i = 0; i < GEN_NLANES; i++)
        if (c->lane_note[i] > 127u)
            return 0;
    for (i = 0; i < GEN_NPARTS; i++) {
        const gen_part_t *p = &c->part[i];
        if (p->on > 1u || p->chan > 15u || p->dev >= GEN_NDEV || p->dens > 127u || p->flair > 127u ||
            p->mode >= NMODES[i] || p->oct < -2 || p->oct > 2)
            return 0;
    }
    return 1;
}
void gen_init(gen_cfg_t *c, gen_out_t out)
{
    memset(&g, 0, sizeof g);
    memset(gb, 0, sizeof gb);
    memset(gon, 0, sizeof gon);
    cadc.ok = 0;
    wb = &gb[1];
    g.c = c;
    g.out = out;
    g.man_pc = -1;
}
const char *gen_style_name(int s) { return STYLE[s % GEN_NSTYLES].name; }
int gen_style_cat(int s) { return STYLE_META[s % GEN_NSTYLES].cat; }
void gen_style_bpm(int s, int *lo, int *hi)
{
    *lo = STYLE_META[s % GEN_NSTYLES].lo;
    *hi = STYLE_META[s % GEN_NSTYLES].hi;
}
int gen_style_tanz(int s) { return STYLE_META[s % GEN_NSTYLES].tanz; }
int gen_style_bell(int s) { return (STYLE_META[s % GEN_NSTYLES].fx & SFX_BELL) != 0; }
int gen_style_pump(int s) { return (STYLE_META[s % GEN_NSTYLES].fx & SFX_PUMP) != 0; }
int gen_style_sweep(int s) { return (STYLE_META[s % GEN_NSTYLES].fx & SFX_SWEEP) != 0; }
int gen_style_wobble(int s) { return (STYLE_META[s % GEN_NSTYLES].fx & SFX_WOBBLE) != 0; }
int gen_style_warble(int s) { return (STYLE_META[s % GEN_NSTYLES].fx & SFX_WARBLE) != 0; }
const char *gen_style_ref(int s) { return STYLE_META[s % GEN_NSTYLES].ref ? STYLE_META[s % GEN_NSTYLES].ref : ""; }
const char *gen_cat_name(int c) { return CAT_NAME[c % GEN_NCATS]; }
const char *gen_cat_code(int c) { return CAT_CODE[c % GEN_NCATS]; }
const char *gen_dev_name(int d) { return DEV[d % GEN_NDEV].name; }
const char *gen_part_name(int p) { return PART_NAME[p % GEN_NPARTS]; }
const char *gen_lane_name(int l) { return LANE_NAME[l % GEN_NLANES]; }
const char *gen_dset_name(int d) { return DSET_NAME[d % GEN_NDSET]; }
int gen_nmodes(int p) { return NMODES[p % GEN_NPARTS]; }
const char *gen_mode_name(int p, int m)
{
    switch (p) {
    case GP_DRUMS: return m % NDM < DM_CAD0 ? DM_NAME[m % NDM] : CAD_NAME[m % NDM - DM_CAD0];
    case GP_CHORD: return CM_NAME[m % NCM];
    case GP_ARP: return AM_NAME[m % NAM];
    case GP_BASS: return BM_NAME[m % NBM];
    case GP_LEAD: return LM_NAME[m % NLM];
    case GP_KICK: return KK_NAME[m % NKK];
    default: return RM_NAME[m % NRM];
    }
}
