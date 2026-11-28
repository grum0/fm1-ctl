/* SPDX-License-Identifier: GPL-3.0-or-later */
/* GEN: the generators (gen.c). A harmony core (key, style, progression, seed) that seven parts follow: drums,
 * chords, arpeggio, bass, lead, a melodic kick and Marble. Everything a part plays is a pure function of the
 * settings, the seed and the bar number, so parts recorded one after another line up bar for bar. */
#pragma once
#include <stdint.h>

#define GEN_STEP 24u                     /* subticks a sixteenth: four a MIDI clock tick */
#define GEN_BAR 384u                     /* subticks a bar (4/4) */

enum { GP_DRUMS, GP_CHORD, GP_ARP, GP_BASS, GP_LEAD, GP_KICK, GP_RAND, GEN_NPARTS };
enum { L_KICK, L_SNARE, L_RIM, L_CLAP, L_CHH, L_OHH, L_TOML, L_TOMH, L_CONGL, L_CONGH, L_COWB, L_CLAVE, L_CYM,
       L_SIM1, L_SIM2, GEN_NLANES };
/* sound locks' voices (0.5.1): kick decay, snare tone, open-hat decay, clap decay */
enum { GEN_LOCK_KICK, GEN_LOCK_SNARE, GEN_LOCK_OHAT, GEN_LOCK_CLAP, GEN_NLOCK };
/* the drum lanes' density groups (0.5.3) */
enum { GEN_GRP_KICK, GEN_GRP_SNARE, GEN_GRP_PERC, GEN_NGRP };
enum { DV_GENERIC, DV_MB33, DV_HADES, DV_MONOLOGUE, DV_JV1010, DV_VOLCAFM, DV_MBASE, DV_TANZBAR, GEN_NDEV };
enum { GS_DILLA, GS_SOULQ, GS_GFUNK, GS_BOMB, GS_QUINCY, GS_JUSTIFIED, GS_MPLS, GS_NPG, GS_NJS, GS_BOOGIE, GS_PFUNK,
       GS_JAZZFUNK, GS_ELECTRO, GS_ELECTRO2, GS_WONKY, GS_BIGBEAT, GS_BRAINDANCE, GS_DETROIT, GS_808, GS_GLITCH,
       GS_HALFTIME, GS_FLAT, GS_RAVE, GS_ACIDDREAM,
       /* (new styles go at the end: a saved style number keeps its meaning) */
       GS_BOOMBAP, GS_NEP_MIN, GS_NEP_CLICK, GS_NEP_CLUB, GS_NEP_SYNTH, GS_NEP_DARK, GS_NEP_STOMP, GS_NERD, GS_DUB,
       GS_MEMPHIS, GS_TRAP, GS_NATIVE, GS_WUTANG, GS_MADLIB, GS_ABSTRACT, GS_OLDSCHOOL, GS_BOUNCE, GS_CHOPPED,
       GS_BELLFUNK, GS_MIAMIBELL, GS_DETBELL, GS_AFROBELL,
       GS_TALKBOX, GS_MODBOOGIE, GS_GOGO, GS_BLAX,
       GS_TIMBALAND, GS_GATED, GS_KRAFTWERK, GS_ITALO, GS_MIAMI,
       GS_LABEATS, GS_TRIPHOP, GS_BROKEN, GS_MONTREAL,
       GS_FRENCH, GS_DUBTECH,
       GS_JUNGLE, GS_UKG, GS_DUBSTEP,
       GS_REPHLEX,
       GS_DANCEHALL, GS_REGGAETON, GS_REGGAETON2, GS_AFROBEAT, GS_BAILE,
       GS_GRIME, GS_DNB, GS_JACK, GS_JUKE,
       GS_APHEX, GS_BOC, GS_AUTECHRE, GS_WARP,
       GS_BALLROOM, GS_JERSEY, GS_GQOM, GS_KUDURO,
       GS_TRIBAL, GS_GHETTOTECH, GS_CUMBIA, GS_FREESTYLE,
       GS_PUNKFUNK, GS_ACIDTECH, GS_DISCO,
       GEN_NSTYLES };
/* the styles' families, for a picker in two steps and a three-letter prefix */
enum { CAT_HIP, CAT_FNK, CAT_RNB, CAT_ELC, CAT_BRK, CAT_HSE, CAT_BAS, CAT_IDM, CAT_WLD, GEN_NCATS };
enum { DS_ALL, DS_KIT, DS_SIM, DS_KICK, DS_NOKICK, GEN_NDSET };   /* which drum lanes the drum part sends */

typedef struct {
    uint8_t on;                          /* the part plays */
    uint8_t chan;                        /* its MIDI channel 0..15 */
    uint8_t dev;                         /* DV_*: the instrument's profile (range, accents, slides, controllers) */
    uint8_t var;                         /* its variation: another pattern on the same progression */
    uint8_t dens;                        /* 0..127: how busy */
    uint8_t flair;                       /* 0..127: flourishes (filter, bends, slides, ratchets); 0: notes only */
    uint8_t mode;                        /* the part's own modes (gen_mode_name) */
    int8_t oct;                          /* -2..+2 */
} gen_part_t;

typedef struct {
    uint8_t style;                       /* GS_* */
    uint8_t root;                        /* the key 0..11 */
    uint8_t minor;                       /* 1 minor, 0 major */
    uint8_t phrase;                      /* 0..3: 1, 2, 4, 8 bars */
    uint8_t feel;                        /* 0..16: the style's swing and looseness, 8 as written, 0 straight */
    uint8_t mutate;                      /* 0..8: 0 the phrase loops exactly; more: it changes as it repeats */
    uint8_t prog;                        /* 0: the style's progression; 1: the keys set the chord */
    uint8_t dset;                        /* DS_* */
    uint16_t seed;                       /* the song: progression and patterns */
    uint8_t gx, gy, chaos;               /* Grids: the map's X and Y, chaos (0..127) */
    uint8_t sim2;                        /* Simmons lane 2: 0 snare layer, 1 crash, 2 percussion */
    uint8_t sim_chan;                    /* the Simmons lanes' channel 1..16, 0: the drum part's */
    uint8_t bend;                        /* the lead's pitch bend range in semitones (the Monologue's patch) */
    int8_t mb_xpose;                     /* MBase: transpose in semitones */
    uint8_t mb_tune;                     /* MBase: CC 101 sent at start (0: not sent) */
    uint8_t mb_lo;                       /* the melodic kick's lowest note (an octave from here) */
    uint8_t swing;                       /* 0: the style's; 1..26: 50..75 %, as an MPC counts swing */
    /* (the next five were reserved, and zero: zero is each one's "as before") */
    uint8_t hat_dens;                    /* the closed hat: 0 with the kit (Density), 1 quarters, 2 eighths, 3 sixteenths */
    uint8_t flam;                        /* 0..127: how many of the snare's backbeats get a grace note before them */
    uint8_t fill_every;                  /* the fill: 0 at the phrase's end, 1 never, 2..6 every 1, 2, 4, 8, 16 bars */
    uint8_t hat_straight;                /* 0..100: percent of the swing taken off the hats */
    uint8_t lane_note[GEN_NLANES];       /* the drum lanes' notes */
    uint8_t space;                       /* 0..100: hats and percussion leave the end of the phrase open (35: a beat, 75: two) */
    gen_part_t part[GEN_NPARTS];
    uint8_t spread;                      /* Marble: how many scale steps its notes range over, 1..24 (0: 11) */
    uint8_t couple;                      /* the bass and the kick: 0 the style's, 1 free, 2 the bass follows the kick, 3 locked to it */
    uint8_t voice;                       /* the chords' voicing: 0 the style's, 1 tight (rootless, close), 2 wide, 3 octaves */
    uint8_t ratchet;                     /* 0: the style's (by Flair); 1..127: 32nd-note hat ratchets and snare rushes */
    uint8_t bell;                        /* 1: the cowbell lane plays a line (a tune CC goes out before each hit) */
    uint8_t bell_cc;                     /* that CC (the Tanzbär's cowbell tune: 84); 0: none */
    uint8_t bell_tab[12];                /* its value for each pitch class, from C; above 127: the drum can't play it */
    uint8_t pocket;                      /* 1: Cut-A-Dor's pocket on the styles' drums (Style mode), by Feel */
    /* 0.5.1, sound locks: per-hit CCs on the drum machine, written by the style as bar events (GEN_LOCK_*) */
    uint8_t locks;                       /* 1: on (the styles in LOCK_STYLES) */
    uint8_t lock_cc[GEN_NLOCK];          /* each lock voice's CC on the drums' device (0: it has none) */
    uint8_t lock_base[GEN_NLOCK];        /* its value as the style's sound sets it (the engine's Tanzbär profile) */
    /* 0.5.3: the lane groups' densities, the Grids module's controls, Ghosts (defaults: as before) */
    int8_t dens_grp[GEN_NGRP];           /* kick / snare, clap, rim / percussion: added to the drums' Density (0) */
    uint8_t g_acc[3];                    /* Grids: an accent's velocity above 84, per voice BD SD HH (28) */
    uint8_t g_euclid;                    /* Grids: 1 = Euclidean mode, the voices' Density their number of hits */
    uint8_t g_len[3];                    /* its loop lengths, 1..32 steps (16) */
    uint8_t g_swing;                     /* Grids' own swing above 50 %, 1..25 (0: the global Swing) */
    uint8_t g_res;                       /* Grids' clock: 0 half speed, 1 as before, 2 double */
    uint8_t g_ohat;                      /* 1: the loudest hat steps go to the open hat (as before) */
    uint32_t g_origin;                   /* the bar Grids' Reset restarts the pattern on (0: the song's start) */
    uint8_t ghosts;                      /* soft snare and rim hits in the empty sixteenths, 0 none */
    /* 0.5.4 (gen1-grids-source-notes.md, gen1-priorities.md "Marble upgrade"; defaults: as before) */
    uint8_t g_rot[3];                    /* Grids Euclidean: rotation per voice, 0..31 (0) */
    uint8_t m_bias, m_shape;             /* Marble: where notes cluster (64), and how (0 one note .. 64 even, as
                                          * before .. 127 the edges) */
    uint8_t m_steps;                     /* Marble: 0..127, from the root only to the whole pool (127, as before) */
    uint8_t m_memory, m_shuffle, m_len;  /* Marble: Déjà vu memory (0/1), shuffle (0..127), loop length 1..16 (16) */
    uint8_t m_rhythm;                    /* Marble: 0 coin (as before), 1 answer, 2 Markov */
    uint8_t room;                        /* chords make room for the lead: 0 off, 1 shorten, 2 drop */
} gen_cfg_t;

typedef void (*gen_out_t)(uint32_t status, uint32_t d1, uint32_t d2);

void gen_defaults(gen_cfg_t *c);
int gen_valid(const gen_cfg_t *c);
void gen_init(gen_cfg_t *c, gen_out_t out);
void gen_start(void);                    /* from the top */
void gen_continue(void);
void gen_locate(uint32_t sixteenths);   /* Song Position Pointer: where the next Continue starts */
void gen_stop(void);                     /* every note off, bends and controllers back */
void gen_subtick(void);                  /* 96 a quarter note while running (the tick: plays, builds nothing) */
void gen_service(void);                  /* the main loop: builds the bars the tick plays */
void gen_part_off(int p);                /* the part's notes off (before its channel or switch changes) */
void gen_chord_key(int pc);              /* prog = keys: the chord on this pitch class */
int gen_running(void);
int gen_bar(void);                       /* the bar of the phrase playing, 0.. */
uint32_t gen_song_bar(void);             /* the bar playing, counted from the song's start (0.5.3) */
int gen_step(void);                      /* the sixteenth playing, 0..15 */
int gen_phrase_bars(void);
void gen_chord_text(char *b);            /* "Cm9" */
int gen_part_heard(int p);               /* the part has a note on now */
uint16_t gen_lane_steps(int lane);       /* the drum part's hits this bar (for the screen) */

const char *gen_style_name(int s);
int gen_style_cat(int s);                /* CAT_* */
void gen_style_bpm(int s, int *lo, int *hi);   /* the tempos it sits at, as set on the MPC (half-time styles counted fast) */
int gen_style_tanz(int s);               /* 1: suits the Tanzbär Lite (not built on sampled breaks or a live kit) */
int gen_style_bell(int s);               /* 1: the cowbell plays a line in this style (Cowbell pitch: Style) */
int gen_style_pump(int s);               /* 1: the chord part's CC 11 dips on each kick in this style */
int gen_style_sweep(int s);              /* 1: the chord part's cutoff sweeps over 16 bars in this style */
int gen_sweep_cc(int *rest);             /* the CC the sweep moves on the chord device, and its value at rest */
int gen_style_wobble(int s);             /* 1: the bass part's cutoff wobbles in this style */
int gen_wobble_cc(int *rest);            /* the CC the wobble moves on the bass device, and its value at rest */
int gen_style_warble(int s);             /* 1: the chords' and lead's pitch bend drifts in this style */
const char *gen_style_ref(int s);        /* the artists or scene it comes from, "" if none */
const char *gen_cat_name(int c);         /* "Hip-hop" */
const char *gen_cat_code(int c);         /* "HIP" */
const char *gen_dev_name(int d);
const char *gen_part_name(int p);
const char *gen_lane_name(int l);
const char *gen_dset_name(int d);
int gen_nmodes(int p);
const char *gen_mode_name(int p, int m);
