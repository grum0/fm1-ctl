/* SPDX-License-Identifier: MPL-2.0
 * Ported in part from CTL-1's engine.c, Copyright (C) 2026 Charles Vestal. See gen1_engine.h. */
#include <string.h>
#include "gen1_engine.h"
#include "tanz_data.h"                          /* (generated: tools/tanzbar/make_tanz.py) */
#include "jv_data.h"                            /* (generated: tools/jv1010/make_jv.py) */

static const uint8_t RATE_TICKS[NRATE] = {24, 12, 8, 6, 4, 3};
#define SEQ_TICKS 6u                     /* a sixteenth */
#define SEQ_GATE 3u
#define MS 1000000LL
#define EXT_LOST_NS (300 * MS)               /* the followed clock counts as gone after this long without a tick */
#define EXT_OUT_DELAY_DEF 5                  /* ms: following, what a tick makes is stamped this long after it arrived */
#define EXT_OUT_DELAY_NS ((int64_t)E.c.out_delay * MS)   /* (finding 8; F_OUT_DELAY sets it, by ear against the MPC) */
#define SONG_NONE 0xFFFFFFFFu

typedef struct { uint8_t steps, hits, rot, note, mute; uint16_t pat; } trk_t;
typedef struct {
    uint16_t tempo;                      /* 40..240 */
    uint8_t clock, keys_chan, vel, human;
    uint8_t arp_on, arp_mode, arp_rate, arp_oct, arp_latch;
    uint8_t seq_ch, out_delay;
    trk_t trk[NTRACKS];
    int8_t tanz_ofs[TANZ_NOFS];          /* 0.4.1: the player's offsets on the style's Tanzbär sound */
} ecfg_t;

static struct {
    eng_sink_t sink;
    void *ud;
    int64_t now, t_cur, last_when;       /* the time of the call; of the message being made; of the last one sent */
    ecfg_t c;
    gen_cfg_t g;
    uint8_t running, start_pending, clk_on, ext_lost;   /* ext_lost: the followed clock went quiet, its notes ended */
    int64_t next_sub;                    /* internal clock: when the next subtick sounds */
    uint32_t sub_n;                      /* ... and its number (a tick every fourth) */
    int64_t ext_last, ext_per;           /* external clock: when the last tick came, and the length of a tick */
    uint32_t song_next, song_wait;       /* host-driven and internal clock: the song subtick expected next, and the
                                          * one a locate waits for (SONG_NONE: not playing) */
    uint32_t ticks, seq_tick, seq_step;
    uint8_t seq_note[NTRACKS], seq_left[NTRACKS];
    uint8_t pool[16], npool;
    uint32_t held[4];
    int arp_pos;
    uint8_t arp_note, arp_left;
    uint32_t rnd;
} E;

/* ------------------------------------------------------------------ output --- */
static void put(const uint8_t *m, int n)         /* the engine's own messages never go out of order */
{
    int64_t t = E.t_cur < E.last_when ? E.last_when : E.t_cur;
    E.last_when = t;
    if (E.sink)
        E.sink(E.ud, t, m, n);
}
static void send3(uint32_t st, uint32_t d1, uint32_t d2)
{
    uint8_t m[3];
    m[0] = (uint8_t)st;
    m[1] = (uint8_t)(d1 & 127u);
    m[2] = (uint8_t)(d2 & 127u);
    put(m, 3);
}
static void send1(uint8_t b) { put(&b, 1); }

/* ---------------------------------------------------------------- settings --- */
uint16_t euclid(int steps, int hits, int rot);   /* (below) */
static const uint8_t TRK_DEFAULT[NTRACKS][4] = {{16, 4, 0, 36}, {16, 2, 4, 37}, {16, 8, 0, 43}, {12, 3, 2, 41}};

uint16_t euclid(int steps, int hits, int rot)    /* hits spread as evenly as possible over steps, turned right by rot */
{
    uint16_t p = 0;
    int i;
    if (steps < 1 || steps > 16)
        return 0;
    if (hits > steps)
        hits = steps;
    for (i = 0; i < steps; i++)
        if ((i * hits) % steps < hits)
            p |= (uint16_t)(1u << ((i + rot) % steps));
    return p;
}

static void ecfg_defaults(void)
{
    int k;
    memset(&E.c, 0, sizeof E.c);
    E.c.tempo = 120;
    E.c.clock = CLK_INT;                 /* (the phone as the clock, silent, until the player picks Follow) */
    E.c.vel = 100;
    E.c.arp_rate = RATE_16;
    E.c.arp_oct = 1;
    E.c.seq_ch = 9;
    E.c.out_delay = EXT_OUT_DELAY_DEF;
    for (k = 0; k < NTRACKS; k++) {
        trk_t *t = &E.c.trk[k];
        t->steps = TRK_DEFAULT[k][0];
        t->hits = TRK_DEFAULT[k][1];
        t->rot = TRK_DEFAULT[k][2];
        t->note = TRK_DEFAULT[k][3];
        t->pat = euclid(t->steps, t->hits, t->rot);
        t->mute = 1;                     /* a track plays once unmuted */
    }
}

void eng_range(int id, int *lo, int *hi)
{
    int l = 0, h = 127;
    if (id >= F_X0) {
        switch (id) {
        case F_HAT_DENS: case F_COUPLE: case F_VOICE: h = 3; break;
        case F_FILL_EVERY: h = 6; break;
        case F_HAT_STRAIGHT: case F_SPACE: h = 100; break;
        case F_SPREAD: l = 1; h = 24; break;
        case F_OUT_DELAY: h = 40; break;
        case F_BELL: h = 1; break;
        case F_POCKET: h = 1; break;
        case F_LOCKS: case F_G_EUCLID: case F_G_OHAT: h = 1; break;
        case F_G_SWING: h = 25; break;
        case F_G_RES: h = 2; break;
        case F_GHOSTS: break;
        default:
            if (id >= F_DENS_GRP0 && id < F_DENS_GRP0 + GEN_NGRP)
                l = -64, h = 63;
            else if (id >= F_G_ACC0 && id < F_G_ACC0 + 3)
                h = 43;
            else if (id >= F_G_LEN0 && id < F_G_LEN0 + 3)
                l = 1, h = 32;
            else if (id >= F_TANZ_OFS0)  /* the Tanzbär offsets */
                l = -127;
            break;                       /* flam, ratchets: 0..127 */
        }
    } else if (id >= F_TRK0) {
        switch ((id - F_TRK0) % 5) {
        case TF_STEPS: l = 1; h = 16; break;
        case TF_HITS: h = 16; break;
        case TF_ROT: h = 15; break;
        case TF_MUTE: h = 1; break;
        default: break;
        }
    } else if (id >= F_PART0) {
        int p = (id - F_PART0) / 8;
        switch ((id - F_PART0) % 8) {
        case PF_ON: h = 1; break;
        case PF_CHAN: h = 15; break;
        case PF_DEV: h = GEN_NDEV - 1; break;
        case PF_VAR: h = 99; break;
        case PF_MODE: h = gen_nmodes(p) - 1; break;
        case PF_OCT: l = -2; h = 2; break;
        default: break;                  /* density, flair: 0..127 */
        }
    } else {
        switch (id) {
        case F_STYLE: h = GEN_NSTYLES - 1; break;
        case F_ROOT: h = 11; break;
        case F_MINOR: case F_PROG: case F_ARP_ON: case F_ARP_LATCH: h = 1; break;
        case F_PHRASE: h = 3; break;
        case F_FEEL: h = 16; break;
        case F_MUTATE: h = 8; break;
        case F_DSET: h = GEN_NDSET - 1; break;
        case F_SEED: h = 65535; break;
        case F_SIM2: h = 2; break;
        case F_SIM_CHAN: h = 16; break;
        case F_BEND: l = 1; h = 12; break;
        case F_MB_XPOSE: l = -24; h = 24; break;
        case F_MB_LO: l = 12; h = 96; break;
        case F_SWING: h = 26; break;
        case F_TEMPO: l = 40; h = 240; break;
        case F_CLOCK: h = NCLK - 1; break;
        case F_KEYS_CHAN: case F_SEQ_CH: h = 15; break;
        case F_VEL: l = 1; break;
        case F_HUMAN: h = 40; break;
        case F_ARP_MODE: h = NARP - 1; break;
        case F_ARP_RATE: h = NRATE - 1; break;
        case F_ARP_OCT: l = 1; h = 3; break;
        default: break;                  /* the rest, and the lanes' notes: 0..127 */
        }
    }
    *lo = l;
    *hi = h;
}

int eng_get(int id)
{
    if (id < 0 || id >= F_COUNT)
        return 0;
    if (id >= F_X0) {
        switch (id) {
        case F_HAT_DENS: return E.g.hat_dens;
        case F_FLAM: return E.g.flam;
        case F_FILL_EVERY: return E.g.fill_every;
        case F_HAT_STRAIGHT: return E.g.hat_straight;
        case F_SPACE: return E.g.space;
        case F_SPREAD: return E.g.spread;
        case F_COUPLE: return E.g.couple;
        case F_VOICE: return E.g.voice;
        case F_OUT_DELAY: return E.c.out_delay;
        case F_BELL: return E.g.bell;
        case F_POCKET: return E.g.pocket;
        case F_LOCKS: return E.g.locks;
        case F_G_EUCLID: return E.g.g_euclid;
        case F_G_SWING: return E.g.g_swing;
        case F_G_RES: return E.g.g_res;
        case F_G_OHAT: return E.g.g_ohat;
        case F_GHOSTS: return E.g.ghosts;
        default:
            if (id >= F_DENS_GRP0 && id < F_DENS_GRP0 + GEN_NGRP)
                return E.g.dens_grp[id - F_DENS_GRP0];
            if (id >= F_G_ACC0 && id < F_G_ACC0 + 3)
                return E.g.g_acc[id - F_G_ACC0];
            if (id >= F_G_LEN0 && id < F_G_LEN0 + 3)
                return E.g.g_len[id - F_G_LEN0];
            return id >= F_TANZ_OFS0 ? E.c.tanz_ofs[id - F_TANZ_OFS0] : E.g.ratchet;
        }
    }
    if (id >= F_TRK0) {
        const trk_t *t = &E.c.trk[(id - F_TRK0) / 5];
        switch ((id - F_TRK0) % 5) {
        case TF_STEPS: return t->steps;
        case TF_HITS: return t->hits;
        case TF_ROT: return t->rot;
        case TF_NOTE: return t->note;
        default: return t->mute;
        }
    }
    if (id >= F_PART0) {
        const gen_part_t *p = &E.g.part[(id - F_PART0) / 8];
        switch ((id - F_PART0) % 8) {
        case PF_ON: return p->on;
        case PF_CHAN: return p->chan;
        case PF_DEV: return p->dev;
        case PF_VAR: return p->var;
        case PF_DENS: return p->dens;
        case PF_FLAIR: return p->flair;
        case PF_MODE: return p->mode;
        default: return p->oct;
        }
    }
    if (id >= F_LANE0)
        return E.g.lane_note[id - F_LANE0];
    switch (id) {
    case F_STYLE: return E.g.style;
    case F_ROOT: return E.g.root;
    case F_MINOR: return E.g.minor;
    case F_PHRASE: return E.g.phrase;
    case F_FEEL: return E.g.feel;
    case F_MUTATE: return E.g.mutate;
    case F_PROG: return E.g.prog;
    case F_DSET: return E.g.dset;
    case F_SEED: return E.g.seed;
    case F_GX: return E.g.gx;
    case F_GY: return E.g.gy;
    case F_CHAOS: return E.g.chaos;
    case F_SIM2: return E.g.sim2;
    case F_SIM_CHAN: return E.g.sim_chan;
    case F_BEND: return E.g.bend;
    case F_MB_XPOSE: return E.g.mb_xpose;
    case F_MB_TUNE: return E.g.mb_tune;
    case F_MB_LO: return E.g.mb_lo;
    case F_SWING: return E.g.swing;
    case F_TEMPO: return E.c.tempo;
    case F_CLOCK: return E.c.clock;
    case F_KEYS_CHAN: return E.c.keys_chan;
    case F_VEL: return E.c.vel;
    case F_HUMAN: return E.c.human;
    case F_ARP_ON: return E.c.arp_on;
    case F_ARP_MODE: return E.c.arp_mode;
    case F_ARP_RATE: return E.c.arp_rate;
    case F_ARP_OCT: return E.c.arp_oct;
    case F_ARP_LATCH: return E.c.arp_latch;
    default: return E.c.seq_ch;
    }
}

/* ---------------------------------------------------------------- arpeggiator --- */
static uint32_t eng_rand(void)
{
    E.rnd ^= E.rnd << 13;
    E.rnd ^= E.rnd >> 17;
    E.rnd ^= E.rnd << 5;
    return E.rnd;
}
static uint32_t eng_vel(void)                    /* the velocity, spread by Humanize */
{
    int v = E.c.vel;
    if (E.c.human)
        v += (int)(eng_rand() % (2u * E.c.human + 1u)) - E.c.human;
    return (uint32_t)(v < 1 ? 1 : v > 127 ? 127 : v);
}
static int arp_seq(uint8_t *out)                 /* the notes in playing order, octaves included */
{
    uint8_t s[16];
    int n = E.npool, i, j, o, k = 0;
    for (i = 0; i < n; i++)
        s[i] = E.pool[i];
    if (E.c.arp_mode != ARP_PLAYED)              /* sorted, low to high */
        for (i = 1; i < n; i++)
            for (j = i; j > 0 && s[j - 1] > s[j]; j--) {
                uint8_t t = s[j];
                s[j] = s[j - 1];
                s[j - 1] = t;
            }
    for (o = 0; o < E.c.arp_oct; o++)
        for (i = 0; i < n; i++)
            if (s[i] + 12 * o < 128)
                out[k++] = (uint8_t)(s[i] + 12 * o);
    return k;
}
static void arp_off(void)
{
    if (E.arp_note != 0xFF) {
        send3(0x80u | E.c.keys_chan, E.arp_note, 0);
        E.arp_note = 0xFF;
    }
}
static void arp_step(void)
{
    uint8_t q[48];
    int n = arp_seq(q), i;
    arp_off();
    if (!n)
        return;
    switch (E.c.arp_mode) {
    case ARP_DOWN: i = n - 1 - E.arp_pos % n; break;
    case ARP_UPDOWN: {                           /* up, then down, the ends once */
        int per = n > 1 ? 2 * n - 2 : 1, p = E.arp_pos % per;
        i = p < n ? p : per - p;
        break;
    }
    case ARP_RANDOM: i = (int)(eng_rand() % (uint32_t)n); break;
    default: i = E.arp_pos % n; break;
    }
    E.arp_pos++;
    E.arp_note = q[i];
    E.arp_left = (uint8_t)(RATE_TICKS[E.c.arp_rate] / 2u);
    send3(0x90u | E.c.keys_chan, q[i], eng_vel());
}
static int held_has(int n) { return (int)(E.held[n >> 5] >> (n & 31) & 1u); }
static int held_any(void) { return (E.held[0] | E.held[1] | E.held[2] | E.held[3]) != 0; }
static void pool_keep_held(void)
{
    int i, k = 0;
    for (i = 0; i < E.npool; i++)
        if (held_has(E.pool[i]))
            E.pool[k++] = E.pool[i];
    E.npool = (uint8_t)k;
}
void eng_arp_press(int n)
{
    int i;
    if (n < 0 || n > 127)
        return;
    if (E.c.arp_latch && !held_any())            /* latched: a new chord replaces the last */
        E.npool = 0;
    if (!held_any() && !E.npool)
        E.arp_pos = 0;
    E.held[n >> 5] |= 1u << (n & 31);
    for (i = 0; i < E.npool && E.pool[i] != n; i++)
        ;
    if (i == E.npool && E.npool < 16)
        E.pool[E.npool++] = (uint8_t)n;
}
void eng_arp_release(int n)
{
    if (n < 0 || n > 127)
        return;
    E.held[n >> 5] &= ~(1u << (n & 31));
    if (!E.c.arp_latch)
        pool_keep_held();
}
void eng_arp_latch_changed(void)
{
    if (!E.c.arp_latch)
        pool_keep_held();
}
static void arp_set(int on)
{
    E.c.arp_on = (uint8_t)on;
    if (!on) {
        arp_off();
        E.npool = 0;
        memset(E.held, 0, sizeof E.held);
    }
}

/* ------------------------------------------------------------------ sequencer --- */
static void seq_offs(void)
{
    int t;
    for (t = 0; t < NTRACKS; t++)
        if (E.seq_note[t] != 0xFF) {
            send3(0x80u | E.c.seq_ch, E.seq_note[t], 0);
            E.seq_note[t] = 0xFF;
        }
}
static void seq_step(void)
{
    int t;
    for (t = 0; t < NTRACKS; t++) {
        const trk_t *k = &E.c.trk[t];
        uint32_t pos = E.seq_step % k->steps;
        if (k->mute || !(k->pat >> pos & 1u))
            continue;
        if (E.seq_note[t] != 0xFF)
            send3(0x80u | E.c.seq_ch, E.seq_note[t], 0);
        E.seq_note[t] = k->note;
        E.seq_left[t] = SEQ_GATE;
        send3(0x90u | E.c.seq_ch, k->note, eng_vel());
    }
    E.seq_step++;
}

/* ----------------------------------------------------------------- the clock --- */
static void gen_out(uint32_t st, uint32_t d1, uint32_t d2) { send3(st, d1, d2); }

static void clock_tick(void)                     /* one 24th of a beat; the generators' first subtick is here */
{
    int t;
    E.ticks++;
    if (E.arp_note != 0xFF && E.arp_left && --E.arp_left == 0)
        arp_off();
    for (t = 0; t < NTRACKS; t++)
        if (E.seq_note[t] != 0xFF && E.seq_left[t] && --E.seq_left[t] == 0) {
            send3(0x80u | E.c.seq_ch, E.seq_note[t], 0);
            E.seq_note[t] = 0xFF;
        }
    if (E.c.arp_on && E.npool) {                 /* on the sequencer's grid (from the start), or the clock's */
        if ((E.running ? E.seq_tick : E.ticks - 1u) % RATE_TICKS[E.c.arp_rate] == 0u)
            arp_step();
    } else if (!E.npool) {
        arp_off();
    }
    if (E.running) {
        gen_subtick();
        if (E.seq_tick % SEQ_TICKS == 0u)
            seq_step();
        E.seq_tick++;
    }
}
static void seq_start(int from_top)
{
    E.song_next = SONG_NONE;                     /* (the subtick code sets it once it plays) */
    if (from_top)
        E.g.g_origin = 0;                        /* (Grids' Reset counts from the top again) */
    if (from_top) {
        E.seq_tick = 0;
        E.seq_step = 0;
        gen_start();
    } else {
        gen_continue();
    }
    E.running = 1;
}
static void seq_stop(void)
{
    E.running = 0;
    E.song_next = SONG_NONE;
    gen_stop();
    seq_offs();
}

/* Every note the engine has sounding ends now: the parts', the arp's and the sequencer's (the transport stays as it is). */
static void notes_off(void)
{
    int p;
    arp_off();
    seq_offs();
    for (p = 0; p < GEN_NPARTS; p++)
        gen_part_off(p);
}

static int64_t sub_ns(void) { return 625000000LL / E.c.tempo; }   /* 96 subticks a quarter note */

/* Locate the song at song subtick pos (0.5.0): on the next sixteenth, as a Song Position Pointer does; the
 * subticks before it stay quiet. From stopped at the top this is a plain Start. */
static void song_locate(uint32_t pos)
{
    uint32_t s16 = (pos + 23u) / 24u;
    E.song_wait = s16 * 24u;
    if (!E.running && s16 == 0u) {
        seq_start(1);
        return;
    }
    arp_off();
    seq_offs();
    E.seq_tick = s16 * SEQ_TICKS;
    E.seq_step = s16;
    gen_locate(s16);
    if (!E.running)
        seq_start(0);
    gen_service();                               /* the bar it lands in, ready before it plays */
}

/* One subtick at time t: the one code path for the internal clock and the host-driven render. pos is the song
 * subtick while playing (96 a quarter note), else any count that is a multiple of 4 on the clock's ticks. */
static void subtick_at(int playing, uint32_t pos, int64_t t)
{
    E.t_cur = t;
    if (!playing) {
        if (E.running)
            seq_stop();
        if ((pos & 3u) == 0u)
            clock_tick();                        /* (the arpeggiator keeps its clock) */
        return;
    }
    if (!E.running || pos != E.song_next)
        song_locate(pos);
    E.song_next = pos + 1u;
    if (pos < E.song_wait)
        return;
    if ((pos & 3u) == 0u)
        clock_tick();
    else
        gen_subtick();
}

void eng_render(int playing, uint32_t pos, uint32_t n, int64_t t0, int64_t per_ns)
{
    uint32_t i;
    E.now = t0;
    E.t_cur = t0;
    gen_service();
    for (i = 0; i < n; i++) {
        if (i && (pos + i) % 24u == 0u)
            gen_service();                       /* (a block may hold more than a bar: the next one made in time) */
        subtick_at(playing, pos + i, t0 + (int64_t)i * per_ns);
    }
}

int64_t eng_pump(int64_t now)
{
    E.now = now;
    gen_service();                               /* the main loop's part: the bars the subticks play */
    if (E.c.clock == CLK_EXT) {
        E.clk_on = 0;
        /* no tick for 300 ms (cable out, the MPC gone) with no Stop: note lengths are counted in ticks, so end them here */
        if (E.ext_last && !E.ext_lost && now - E.ext_last > EXT_LOST_NS) {
            E.t_cur = now;
            notes_off();
            E.ext_lost = 1;
        }
        return now + 5 * MS;
    }
    if (!E.clk_on || E.next_sub < now - 100 * MS) {   /* just on, or held up so long that catching up would burst */
        if (!E.clk_on)
            E.sub_n = 0;
        /* (after a stall the count goes on where it was, so the clock's ticks and the generators' subticks stay in
         * step: a tick is every fourth subtick for both) */
        E.clk_on = 1;
        E.next_sub = now + MS;
    }
    while (E.next_sub <= now + ENG_LOOKAHEAD_NS) {
        int start = 0;
        E.t_cur = E.next_sub;
        if ((E.sub_n & 3u) == 0u) {
            if (E.start_pending) {
                E.start_pending = 0;
                start = 1;
                if (E.c.clock == CLK_INT_OUT) {
                    E.t_cur = E.next_sub - MS;   /* Start just before the tick it starts on */
                    send1(0xFA);
                    E.t_cur = E.next_sub;
                }
            }
            if (E.c.clock == CLK_INT_OUT)
                send1(0xF8);
        }
        /* the internal clock is the song's transport: playing from the top on Start, then subtick by subtick */
        if (start)
            subtick_at(1, 0u, E.next_sub);
        else if (E.running && E.song_next != SONG_NONE)
            subtick_at(1, E.song_next, E.next_sub);
        else
            subtick_at(E.running, E.sub_n, E.next_sub);
        E.sub_n++;
        E.next_sub += sub_ns();
    }
    return E.next_sub - ENG_LOOKAHEAD_NS / 2;
}

void eng_midi_clock(uint8_t b, int64_t at)
{
    if (E.c.clock != CLK_EXT)
        return;
    E.now = at;
    E.t_cur = at;
    if (b == 0xF8) {
        int64_t per = at - E.ext_last, nominal = 2500000000LL / E.c.tempo;
        int k;
        if (!E.ext_last || !E.ext_per || per > 150 * MS)   /* the first tick, or after a gap: the tempo's guess */
            E.ext_per = nominal;
        else if (per >= 3 * MS)                  /* otherwise the last ticks' length, smoothed over four; two ticks */
            E.ext_per = (E.ext_per * 3 + per) / 4;   /* closer than 3 ms (one USB packet) keep the last length */
        E.ext_last = at;
        E.ext_lost = 0;
        /* everything this tick makes is stamped a fixed delay after it arrived, the tick's own messages too, so
         * on-tick and in-between notes keep their spacing whatever the receiving delay was */
        E.t_cur = at + EXT_OUT_DELAY_NS;
        clock_tick();
        if (E.running)                           /* the other three subticks, spread over a tick's length */
            for (k = 1; k < 4; k++) {
                E.t_cur = at + EXT_OUT_DELAY_NS + E.ext_per * k / 4;
                gen_subtick();
            }
    } else if (b == 0xFA) {
        seq_start(1);
    } else if (b == 0xFB) {
        seq_start(0);
        gen_service();                           /* the bar it continues in, ready before the next tick */
    } else if (b == 0xFC) {
        seq_stop();
        arp_off();                               /* its note-off would wait for ticks the MPC may not send */
    }
}

/* Song Position Pointer (F2): the MPC was moved to `sixteenths` from the top while stopped; its Continue (FB) plays
 * from there. The bar count, the phrase rules and the sequencer follow. (While running it is taken as a jump.) */
void eng_midi_spp(int sixteenths, int64_t at)
{
    if (E.c.clock != CLK_EXT || sixteenths < 0)
        return;
    E.now = at;
    E.t_cur = at + EXT_OUT_DELAY_NS;
    arp_off();
    seq_offs();
    E.seq_tick = (uint32_t)sixteenths * SEQ_TICKS;
    E.seq_step = (uint32_t)sixteenths;
    gen_locate((uint32_t)sixteenths);
}

/* the cowbell line's tune CC and its value per pitch class (the Tanzbär profile's table; 255: not playable) */
void eng_bell_table(int cc, const uint8_t tab[12])
{
    int i;
    E.g.bell_cc = (uint8_t)(cc & 127);
    for (i = 0; i < 12; i++)
        E.g.bell_tab[i] = tab[i];
}

void eng_play(int on, int64_t now)
{
    E.now = now;
    E.t_cur = now;
    if (on) {
        if (E.c.clock == CLK_EXT)
            seq_start(1);                        /* the parts start; the clock is the other machine's */
        else
            E.start_pending = 1;                 /* on the next tick, so the clock does not stutter */
    } else {
        E.start_pending = 0;
        if (E.c.clock == CLK_INT_OUT)
            send1(0xFC);
        seq_stop();
    }
}

/* ------------------------------------------------------------ the screen's part --- */
void eng_send_now(uint8_t st, uint8_t d1, uint8_t d2, int64_t now)
{
    uint8_t m[3];
    m[0] = st;
    m[1] = d1 & 127u;
    m[2] = d2 & 127u;
    if (E.sink)
        E.sink(E.ud, now, m, (st & 0xF0u) == 0xC0u || (st & 0xF0u) == 0xD0u ? 2 : 3);
}

void eng_part_off(int p, int64_t now)
{
    if (p < 0 || p >= GEN_NPARTS)
        return;
    E.now = now;
    E.t_cur = now;
    gen_part_off(p);
}

void eng_chord_key(int pc, int64_t now)
{
    (void)now;
    gen_chord_key(pc);
}

void eng_panic(int64_t now)
{
    int p, ch;
    uint32_t tanz_chans = 0;
    E.now = now;
    E.t_cur = now;
    arp_off();
    seq_offs();
    for (p = 0; p < GEN_NPARTS; p++)
        gen_part_off(p);
    E.npool = 0;
    memset(E.held, 0, sizeof E.held);
    for (p = 0; p < GEN_NPARTS; p++)
        if (E.g.part[p].dev == DV_TANZBAR)
            tanz_chans |= 1u << E.g.part[p].chan;
    for (ch = 0; ch < 16; ch++) {                /* and whatever else is sounding, on every channel */
        send3(0xB0u | (uint32_t)ch, 123, 0);     /* all notes off */
        send3(0xB0u | (uint32_t)ch, 120, 0);     /* all sound off */
        if (!(tanz_chans & 1u << ch))           /* reset all controllers (not the Tanzbär's: its sound stays; the */
            send3(0xB0u | (uint32_t)ch, 121, 0); /* app sends the style's values again after a panic) */
        send3(0xE0u | (uint32_t)ch, 0, 64);      /* pitch bend to the middle */
    }
}

/* 0.4.1: the Tanzbär knobs as offsets on the style's sound. The arithmetic lives here (portable core); the app asks
 * for the value to send and for the offset a knob position means. */
static int clamp127(int v) { return v < 0 ? 0 : v > 127 ? 127 : v; }
int eng_tanz_mix(int slot, int base)
{
    if (base < 0)
        return -1;
    if (slot < 0 || slot >= TANZ_NOFS)
        return base;
    return clamp127(base + E.c.tanz_ofs[slot]);
}
int eng_tanz_offset(int slot, int base, int value)
{
    (void)slot;
    return clamp127(value) - base;
}

/* 0.5.0: the rest of the Tanzbär's sound per style and key, moved here from the app (Store.applyTanzSound), so a
 * plugin shell gets it too. The data is the generated tanz_data.h. */
static int style_now(void) { return eng_get(F_STYLE); }
int eng_bell_melodic(int bell_mode)
{
    int st = style_now();
    return bell_mode == BELL_LINE || (bell_mode == BELL_STYLE && st >= 0 && st < GEN_NSTYLES && gen_style_bell(st));
}
int eng_tanz_slot(int cc)
{
    int i;
    for (i = 0; i < TANZ_NOFS; i++)
        if (TANZ_CCS[i] == cc)
            return i;
    return -1;
}
int eng_tanz_base(int slot, int melodic)
{
    int st = style_now();
    if (slot < 0 || slot >= TANZ_NOFS || st < 0 || st >= GEN_NSTYLES)
        return -1;
    if (melodic && TANZ_CCS[slot] == 85)         /* the cowbell's short decay while it plays a line */
        return TANZ_BELL_DECAY[st];
    return TANZ_STYLE[st][slot];
}
/* the pitch nearest the key that the drum can play: the root, else its fifth, fourth, then the key's third */
static int key_tune(const int8_t tab[12])
{
    int i, r = eng_get(F_ROOT), off[4] = {0, 7, 5, 4};
    off[3] = eng_get(F_MINOR) ? 3 : 4;
    for (i = 0; i < 4; i++)
        if (tab[(r + off[i]) % 12] >= 0)
            return tab[(r + off[i]) % 12];
    return -1;
}
int eng_tanz_sound(int kick_key, int melodic, uint8_t *cc, uint8_t *val)
{
    int i, n = 0, st = style_now(), f;
#define TANZ_PUT(c, v) do { int v_ = (v); if (v_ >= 0) { cc[n] = (uint8_t)(c); val[n] = (uint8_t)v_; n++; } } while (0)
    if (st < 0 || st >= GEN_NSTYLES)
        return 0;
    for (i = 0; i < TANZ_NOFS; i++)              /* the style's values with the player's offsets */
        TANZ_PUT(TANZ_CCS[i], eng_tanz_mix(i, eng_tanz_base(i, melodic)));
    TANZ_PUT(TANZ_KICK_CC, kick_key ? key_tune(TANZ_KICK) : TANZ_KICK_STYLE[st]);
    f = TANZ_FOLLOW[st];
    if (f & 1)
        TANZ_PUT(TANZ_TOM_CC, key_tune(TANZ_TOM));
    if (f & 2)
        TANZ_PUT(TANZ_CONGA_CC, key_tune(TANZ_CONGA));
    if (f & 4)
        TANZ_PUT(TANZ_CLAVE_CC, key_tune(TANZ_CLAVE));
    if (!melodic)                                /* (a line tunes it hit by hit) */
        TANZ_PUT(TANZ_BELL_CC, key_tune(TANZ_BELL));
#undef TANZ_PUT
    return n;
}
int eng_style_tempo(int on)
{
    int st = style_now(), lo, hi, t = E.c.tempo;
    if (!on || E.c.clock == CLK_EXT || st < 0 || st >= GEN_NSTYLES)
        return t;
    gen_style_bpm(st, &lo, &hi);
    return t < lo ? lo : t > hi ? hi : t;
}
void eng_grids_reset(void)
{
    E.g.g_origin = gen_song_bar() + 1u;          /* (a pure function of the bar again from there) */
}
int eng_jv_hint(int style, int out[4])
{
    int i;
    for (i = 0; i < 4; i++)
        out[i] = style >= 0 && style < GEN_NSTYLES ? JV_HINT[style][i] : 0;
    return style >= 0 && style < GEN_NSTYLES;
}
const char *eng_jv_part(int part) { return part >= 0 && part <= 16 ? JV_PART[part] : ""; }

/* 0.5.1, sound locks: the CC of each lock voice and the style's value for it (with the player's offset), when the
 * drums play on the Tanzbär; no CCs otherwise (other devices once their profiles exist) */
static void locks_profile(void)
{
    static const uint8_t CC[GEN_NLOCK] = {64, 11, 90, 75};   /* kick decay, snare tone, open hat decay, clap decay */
    int k, st = eng_get(F_STYLE), on = E.g.part[GP_DRUMS].dev == DV_TANZBAR && st >= 0 && st < GEN_NSTYLES;
    for (k = 0; k < GEN_NLOCK; k++) {
        int slot = eng_tanz_slot(CC[k]);
        E.g.lock_cc[k] = (uint8_t)(on && slot >= 0 ? CC[k] : 0);
        E.g.lock_base[k] = (uint8_t)(on && slot >= 0 ? eng_tanz_mix(slot, TANZ_STYLE[st][slot]) : 0);
    }
}

/* the Tanzbär profile's cowbell table, as the line needs it (255: a pitch it cannot play) */
static void bell_profile(void)
{
    uint8_t t[12];
    int i;
    for (i = 0; i < 12; i++)
        t[i] = TANZ_BELL[i] < 0 ? 255u : (uint8_t)TANZ_BELL[i];
    eng_bell_table(TANZ_BELL_CC, t);
}
static void set_one(int id, int v, int64_t now);
static void locks_profile(void);
void eng_set(int id, int v, int64_t now)
{
    set_one(id, v, now);
    locks_profile();                             /* (the style, its offsets or the drums' device may have changed) */
}
static void set_one(int id, int v, int64_t now)
{
    int lo, hi;
    if (id < 0 || id >= F_COUNT)
        return;
    eng_range(id, &lo, &hi);
    if (id >= F_TRK0 && id < F_X0) {
        trk_t *t = &E.c.trk[(id - F_TRK0) / 5];
        int f = (id - F_TRK0) % 5;
        if (f == TF_HITS)
            hi = t->steps;
        else if (f == TF_ROT)
            hi = t->steps - 1;
    }
    v = v < lo ? lo : v > hi ? hi : v;
    E.now = now;
    E.t_cur = now;
    if (id >= F_X0) {
        switch (id) {
        case F_HAT_DENS: E.g.hat_dens = (uint8_t)v; break;
        case F_FLAM: E.g.flam = (uint8_t)v; break;
        case F_FILL_EVERY: E.g.fill_every = (uint8_t)v; break;
        case F_HAT_STRAIGHT: E.g.hat_straight = (uint8_t)v; break;
        case F_SPACE: E.g.space = (uint8_t)v; break;
        case F_SPREAD: E.g.spread = (uint8_t)v; break;
        case F_COUPLE: E.g.couple = (uint8_t)v; break;
        case F_VOICE: E.g.voice = (uint8_t)v; break;
        case F_OUT_DELAY: E.c.out_delay = (uint8_t)v; break;
        case F_BELL: E.g.bell = (uint8_t)v; break;
        case F_POCKET: E.g.pocket = (uint8_t)v; break;
        case F_LOCKS: E.g.locks = (uint8_t)v; break;
        case F_G_EUCLID: E.g.g_euclid = (uint8_t)v; break;
        case F_G_SWING: E.g.g_swing = (uint8_t)v; break;
        case F_G_RES: E.g.g_res = (uint8_t)v; break;
        case F_G_OHAT: E.g.g_ohat = (uint8_t)v; break;
        case F_GHOSTS: E.g.ghosts = (uint8_t)v; break;
        default:
            if (id >= F_DENS_GRP0 && id < F_DENS_GRP0 + GEN_NGRP)
                E.g.dens_grp[id - F_DENS_GRP0] = (int8_t)v;
            else if (id >= F_G_ACC0 && id < F_G_ACC0 + 3)
                E.g.g_acc[id - F_G_ACC0] = (uint8_t)v;
            else if (id >= F_G_LEN0 && id < F_G_LEN0 + 3)
                E.g.g_len[id - F_G_LEN0] = (uint8_t)v;
            else if (id >= F_TANZ_OFS0)
                E.c.tanz_ofs[id - F_TANZ_OFS0] = (int8_t)v;
            else
                E.g.ratchet = (uint8_t)v;
            break;
        }
        return;
    }
    if (id >= F_TRK0) {
        trk_t *t = &E.c.trk[(id - F_TRK0) / 5];
        switch ((id - F_TRK0) % 5) {
        case TF_STEPS: t->steps = (uint8_t)v; break;
        case TF_HITS: t->hits = (uint8_t)v; break;
        case TF_ROT: t->rot = (uint8_t)v; break;
        case TF_NOTE: t->note = (uint8_t)v; break;
        default: t->mute = (uint8_t)v; break;
        }
        if (t->hits > t->steps)
            t->hits = t->steps;
        if (t->rot >= t->steps)
            t->rot = (uint8_t)(t->steps - 1u);
        t->pat = euclid(t->steps, t->hits, t->rot);
        return;
    }
    if (id >= F_PART0) {
        int p = (id - F_PART0) / 8;
        gen_part_t *g = &E.g.part[p];
        switch ((id - F_PART0) % 8) {
        case PF_ON: g->on = (uint8_t)v; break;
        case PF_CHAN:
            if (g->chan != v)
                gen_part_off(p);                 /* its notes end on the channel they began on */
            g->chan = (uint8_t)v;
            break;
        case PF_DEV:
            if (g->dev != v)
                gen_part_off(p);
            g->dev = (uint8_t)v;
            break;
        case PF_VAR: g->var = (uint8_t)v; break;
        case PF_DENS: g->dens = (uint8_t)v; break;
        case PF_FLAIR: g->flair = (uint8_t)v; break;
        case PF_MODE: g->mode = (uint8_t)v; break;
        default: g->oct = (int8_t)v; break;
        }
        return;
    }
    if (id >= F_LANE0) {
        E.g.lane_note[id - F_LANE0] = (uint8_t)v;
        return;
    }
    switch (id) {
    case F_STYLE:
        if (gen_style_pump(E.g.style) && !gen_style_pump(v) && E.g.part[GP_CHORD].on)
            send3(0xB0u | E.g.part[GP_CHORD].chan, 11, 127);   /* leaving the pump: the chords' expression back up */
        if (gen_style_sweep(E.g.style) && !gen_style_sweep(v) && E.g.part[GP_CHORD].on) {
            int rest, cc = gen_sweep_cc(&rest);                   /* leaving the sweep: the chords' cutoff back */
            send3(0xB0u | E.g.part[GP_CHORD].chan, (uint8_t)cc, (uint8_t)rest);
        }
        if (gen_style_warble(E.g.style) && !gen_style_warble(v)) {   /* leaving the warble: bends centred */
            if (E.g.part[GP_CHORD].on)
                send3(0xE0u | E.g.part[GP_CHORD].chan, 0, 64);
            if (E.g.part[GP_LEAD].on)
                send3(0xE0u | E.g.part[GP_LEAD].chan, 0, 64);
        }
        if (gen_style_wobble(E.g.style) && !gen_style_wobble(v) && E.g.part[GP_BASS].on) {
            int rest, cc = gen_wobble_cc(&rest);                  /* leaving the wobble: the bass's cutoff back */
            send3(0xB0u | E.g.part[GP_BASS].chan, (uint8_t)cc, (uint8_t)rest);
        }
        E.g.style = (uint8_t)v;
        break;
    case F_ROOT: E.g.root = (uint8_t)v; break;
    case F_MINOR: E.g.minor = (uint8_t)v; break;
    case F_PHRASE: E.g.phrase = (uint8_t)v; break;
    case F_FEEL: E.g.feel = (uint8_t)v; break;
    case F_MUTATE: E.g.mutate = (uint8_t)v; break;
    case F_PROG: E.g.prog = (uint8_t)v; break;
    case F_DSET: E.g.dset = (uint8_t)v; break;
    case F_SEED: E.g.seed = (uint16_t)v; break;
    case F_GX: E.g.gx = (uint8_t)v; break;
    case F_GY: E.g.gy = (uint8_t)v; break;
    case F_CHAOS: E.g.chaos = (uint8_t)v; break;
    case F_SIM2: E.g.sim2 = (uint8_t)v; break;
    case F_SIM_CHAN: E.g.sim_chan = (uint8_t)v; break;
    case F_BEND: E.g.bend = (uint8_t)v; break;
    case F_MB_XPOSE: E.g.mb_xpose = (int8_t)v; break;
    case F_MB_TUNE: E.g.mb_tune = (uint8_t)v; break;
    case F_MB_LO: E.g.mb_lo = (uint8_t)v; break;
    case F_SWING: E.g.swing = (uint8_t)v; break;
    case F_TEMPO: E.c.tempo = (uint16_t)v; break;
    case F_CLOCK:
        if (E.c.clock != v) {                    /* a new clock: stop the transport and end the notes, none wait for it */
            E.now = now;
            E.t_cur = now;
            E.start_pending = 0;
            if (E.running && E.c.clock == CLK_INT_OUT)
                send1(0xFC);
            seq_stop();
            notes_off();
            E.clk_on = 0;
            E.ext_lost = 0;
            E.ext_last = 0;
            E.ext_per = 0;
        }
        E.c.clock = (uint8_t)v;
        break;
    case F_KEYS_CHAN:
        if (E.c.keys_chan != v)
            arp_off();
        E.c.keys_chan = (uint8_t)v;
        break;
    case F_VEL: E.c.vel = (uint8_t)v; break;
    case F_HUMAN: E.c.human = (uint8_t)v; break;
    case F_ARP_ON: arp_set(v); break;
    case F_ARP_MODE: E.c.arp_mode = (uint8_t)v; break;
    case F_ARP_RATE: E.c.arp_rate = (uint8_t)v; break;
    case F_ARP_OCT: E.c.arp_oct = (uint8_t)v; break;
    case F_ARP_LATCH:
        E.c.arp_latch = (uint8_t)v;
        eng_arp_latch_changed();
        break;
    default:
        if (E.c.seq_ch != v)
            seq_offs();
        E.c.seq_ch = (uint8_t)v;
        break;
    }
}

/* The app's own defaults over the generators': the Simmons lanes go to the MPC's CV track, whose pads A01 and A02
 * are notes 0 and 1 (not 36 and 37); Marble's spread is shown as a number (11: what the generators' 0 means) */
static void cv_pads(void)
{
    E.g.lane_note[L_SIM1] = 0;
    E.g.lane_note[L_SIM2] = 1;
    E.g.spread = 11;
}

void eng_init(eng_sink_t sink, void *ud)
{
    memset(&E, 0, sizeof E);
    E.sink = sink;
    E.ud = ud;
    E.arp_note = 0xFF;
    memset(E.seq_note, 0xFF, sizeof E.seq_note);
    E.rnd = 2463534242u;
    ecfg_defaults();
    gen_defaults(&E.g), cv_pads();
    bell_profile();
    locks_profile();
    E.song_next = SONG_NONE;
    gen_init(&E.g, gen_out);
}

void eng_defaults(int64_t now)
{
    eng_play(0, now);
    eng_panic(now);
    ecfg_defaults();
    gen_defaults(&E.g), cv_pads();
    bell_profile();                              /* (0.5.0: kept after the defaults too) */
    locks_profile();
}

void eng_status(int32_t *out, int64_t now)
{
    int p, mask = 0;
    for (p = 0; p < GEN_NPARTS; p++)
        if (gen_part_heard(p))
            mask |= 1 << p;
    out[0] = E.running;
    out[1] = gen_bar();
    out[2] = gen_step();
    out[3] = gen_phrase_bars();
    out[4] = E.ticks % 24u < 6u;
    out[5] = mask;
    out[7] = E.c.clock == CLK_EXT ? E.ext_last && now - E.ext_last < EXT_LOST_NS : 1;
    out[6] = E.c.clock == CLK_EXT && out[7] && E.ext_per ? (int32_t)(25000000000LL / E.ext_per) : E.c.tempo * 10;
}
