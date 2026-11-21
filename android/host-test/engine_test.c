/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The Android engine on a computer: a virtual clock, a sink that keeps every message with the time it was given.
 * Checks the clock (internal and followed), the transport, the channels, the arpeggiator and sequencer, the
 * settings, and that parts made apart fit each other.   cc -DGEN_STANDALONE ... engine_test.c   (run.sh) */
#define GEN_STANDALONE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../app/gen.c"
#include "../app/src/main/cpp/gen1_engine.c"

typedef struct { int64_t t, at; uint8_t m[3]; int n; } msg_t;
static msg_t *log_;
static int nlog, caplog;
static int64_t vnow;                             /* the virtual time of the call being made */
static int fails;

#define CHECK(c, ...) do { if (!(c)) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static void sink(void *ud, int64_t when, const uint8_t *m, int n)
{
    (void)ud;
    if (nlog == caplog) {
        caplog = caplog ? caplog * 2 : 4096;
        log_ = realloc(log_, (size_t)caplog * sizeof *log_);
    }
    log_[nlog].t = when;
    log_[nlog].at = vnow;
    log_[nlog].n = n;
    memcpy(log_[nlog].m, m, (size_t)n);
    nlog++;
}
static void fresh(void)
{
    nlog = 0;
    vnow = 1000 * MS;
    eng_init(sink, 0);
}
/* the internal clock's thread: a call every 4 ms (and a late one now and then) */
static void run_internal(int64_t ms)
{
    int64_t end = vnow + ms * MS;
    int k = 0;
    while (vnow < end) {
        eng_pump(vnow);
        vnow += (++k % 50 == 0 ? 17 : 4) * MS;   /* a thread that wakes up late sometimes, within the lookahead */
    }
}
/* the MPC: a clock at bpm, with jitter, for ms */
static uint32_t jr = 12345;
static int64_t jit(int64_t span) { jr = jr * 1664525u + 1013904223u; return (int64_t)(jr >> 8) % (2 * span + 1) - span; }
static void run_external(int64_t ms, int bpm, int64_t jitter)
{
    int64_t tick = 2500000000LL / bpm, end = vnow + ms * MS, t = vnow;
    int64_t base = vnow;
    int n = 0;
    while (t < end) {
        vnow = base + n * tick + jit(jitter);
        eng_pump(vnow);
        eng_midi_clock(0xF8, vnow);
        n++;
        t = base + n * tick;
    }
    vnow = t;
}
static int count(uint8_t st, int ch)
{
    int i, c = 0;
    for (i = 0; i < nlog; i++)
        if (log_[i].n == 3 && (log_[i].m[0] & 0xF0) == st && (ch < 0 || (log_[i].m[0] & 15) == ch))
            c++;
    return c;
}
static int count_rt(uint8_t b)
{
    int i, c = 0;
    for (i = 0; i < nlog; i++)
        c += log_[i].n == 1 && log_[i].m[0] == b;
    return c;
}
static void check_order(const char *what)
{
    int i;
    for (i = 1; i < nlog; i++)
        if (log_[i].t < log_[i - 1].t) {
            CHECK(0, "%s: message %d goes before the one before it (%lld < %lld)", what, i, (long long)log_[i].t, (long long)log_[i - 1].t);
            return;
        }
}
static void check_no_stuck(const char *what)
{
    static int held[16][128];
    int i, ch, n;
    memset(held, 0, sizeof held);
    for (i = 0; i < nlog; i++) {
        int k = log_[i].m[0] & 0xF0;
        if (log_[i].n != 3)
            continue;
        ch = log_[i].m[0] & 15;
        n = log_[i].m[1];
        if (k == 0x90 && log_[i].m[2])
            held[ch][n]++;
        else if (k == 0x80 || k == 0x90)
            held[ch][n]--;
    }
    for (ch = 0; ch < 16; ch++)
        for (n = 0; n < 128; n++)
            if (held[ch][n] != 0) {
                CHECK(0, "%s: note %d on channel %d left %s", what, n, ch + 1, held[ch][n] > 0 ? "on" : "off twice");
                return;
            }
}

/* ---------------------------------------------------------------------------- */
static void test_ranges(void)
{
    int id, lo, hi, v;
    fresh();
    for (id = 0; id < F_COUNT; id++) {
        eng_range(id, &lo, &hi);
        CHECK(lo <= hi, "range of %d is %d..%d", id, lo, hi);
        eng_set(id, lo - 1000, vnow);
        v = eng_get(id);
        CHECK(v >= lo && v <= hi, "id %d below its range gives %d (%d..%d)", id, v, lo, hi);
        eng_set(id, hi + 1000, vnow);
        v = eng_get(id);
        CHECK(v >= lo && v <= hi, "id %d above its range gives %d (%d..%d)", id, v, lo, hi);
        eng_set(id, hi, vnow);
        eng_set(id, lo, vnow);
        CHECK(eng_get(id) == lo, "id %d: set %d, got %d", id, lo, eng_get(id));
        CHECK(gen_valid(&E.g), "the generators' settings are invalid after setting id %d to %d", id, lo);
    }
    fresh();
    CHECK(gen_valid(&E.g), "defaults invalid");
    CHECK(eng_get(F_PART(GP_DRUMS, PF_DEV)) == DV_TANZBAR, "drums default device");
    CHECK(eng_get(F_LANE0 + L_KICK) == 36 && eng_get(F_LANE0 + L_SNARE) == 37, "lane note defaults");
    CHECK(eng_get(F_LANE0 + L_SIM1) == 0 && eng_get(F_LANE0 + L_SIM2) == 1, "the Simmons lanes: MPC CV pads A01, A02 = notes 0, 1");
    CHECK(eng_get(F_TRK(0, TF_STEPS)) == 16 && eng_get(F_TRK(0, TF_MUTE)) == 1, "track defaults");
    printf("settings: %d ids ok\n", F_COUNT);
}

static void setup_rig(void)
{
    /* the drums on channel 4 (the Tanzbar), chords 6, arp 5, bass 1, lead 3, kick 8, marble 7: all on */
    static const int CH[GEN_NPARTS] = {3, 5, 4, 0, 2, 7, 6};
    int p;
    for (p = 0; p < GEN_NPARTS; p++) {
        eng_set(F_PART(p, PF_ON), 1, vnow);
        eng_set(F_PART(p, PF_CHAN), CH[p], vnow);
    }
}

static void test_internal(void)
{
    int i, nclk = 0, p;
    int64_t first = 0, last = 0, worst = 0, sum = 0;
    static const int CH[GEN_NPARTS] = {3, 5, 4, 0, 2, 7, 6};
    int used[16] = {0};
    fresh();
    eng_set(F_CLOCK, CLK_INT_OUT, vnow);
    eng_set(F_TEMPO, 120, vnow);
    setup_rig();
    run_internal(300);                           /* stopped: the clock runs, nothing else */
    CHECK(count_rt(0xF8) > 10, "the clock should run while stopped (%d)", count_rt(0xF8));
    CHECK(count(0x90, -1) == 0, "nothing plays while stopped");
    nlog = 0;
    eng_play(1, vnow);
    run_internal(8000);                          /* four bars at 120 */
    eng_play(0, vnow);
    run_internal(100);
    check_order("internal");
    check_no_stuck("internal");
    CHECK(count_rt(0xFA) == 1 && count_rt(0xFC) == 1, "one start (%d) and one stop (%d)", count_rt(0xFA), count_rt(0xFC));
    for (i = 0; i < nlog; i++) {
        if (log_[i].n == 1 && log_[i].m[0] == 0xFA) {
            CHECK(i + 1 < nlog && log_[i + 1].n == 1 && log_[i + 1].m[0] == 0xF8, "a clock tick follows the start");
            CHECK(log_[i].t < log_[i + 1].t || log_[i].t == log_[i + 1].t, "start before its tick");
        }
        if (log_[i].n == 1 && log_[i].m[0] == 0xF8) {
            if (first && last) {
                int64_t d = log_[i].t - last;
                int64_t err = d > 20833333LL ? d - 20833333LL : 20833333LL - d;
                if (err > worst)
                    worst = err;
                sum += d;
            }
            if (!first)
                first = log_[i].t;
            last = log_[i].t;
            nclk++;
        }
        /* a message is never made late: it is given to the sink before its time (the lookahead) */
        if (log_[i].t < log_[i].at - 1 * MS)
            CHECK(0, "message %d is %lld us late", i, (long long)((log_[i].at - log_[i].t) / 1000));
    }
    CHECK(nclk > 8000 / 21 - 5, "clock ticks: %d", nclk);
    CHECK(worst < 2, "clock spacing off by %lld ns", (long long)worst);
    printf("internal: %d ticks, spacing off by at most %lld ns, mean %lld ns\n", nclk, (long long)worst, (long long)(sum / (nclk - 1)));
    for (i = 0; i < nlog; i++)
        if (log_[i].n == 3)
            used[log_[i].m[0] & 15] = 1;
    for (p = 0; p < GEN_NPARTS; p++)
        CHECK(used[CH[p]], "part %s sent nothing on channel %d", gen_part_name(p), CH[p] + 1);
    for (i = 0; i < 16; i++) {
        int mine = 0;
        for (p = 0; p < GEN_NPARTS; p++)
            mine |= CH[p] == i;
        CHECK(mine || !used[i], "something went out on channel %d, which no part has", i + 1);
    }
    {   /* the Tanzbar's channel carries its notes */
        int kick = 0;
        for (i = 0; i < nlog; i++)
            kick += log_[i].n == 3 && log_[i].m[0] == 0x93 && log_[i].m[1] == 36 && log_[i].m[2];
        CHECK(kick > 4, "the drums' kick (note 36, channel 4) played %d times", kick);
    }
}

static void test_clockless(void)
{
    fresh();
    eng_set(F_CLOCK, CLK_INT, vnow);             /* kept, not sent */
    setup_rig();
    eng_play(1, vnow);
    run_internal(2000);
    CHECK(count_rt(0xF8) == 0 && count_rt(0xFA) == 0, "Internal (quiet) sends no realtime messages");
    CHECK(count(0x90, -1) > 20, "but the parts play (%d)", count(0x90, -1));
}

static void test_external(void)
{
    int i, bars_seen = 0;
    fresh();
    eng_set(F_CLOCK, CLK_EXT, vnow);
    setup_rig();
    run_external(2000, 100, 0);                  /* a clock but no start: nothing plays */
    CHECK(count(0x90, -1) == 0 && nlog == 0, "no start, no notes (%d messages)", nlog);
    eng_midi_clock(0xFA, vnow);                  /* the MPC's start */
    run_external(8000, 100, 1500000);            /* 100 BPM, 1.5 ms of jitter */
    {
        int32_t st[8];
        eng_status(st, vnow);
        CHECK(st[7] == 1, "the clock is seen");
        CHECK(st[6] > 980 && st[6] < 1020, "tempo read from the clock: %d", st[6]);
        bars_seen = st[1];
    }
    eng_midi_clock(0xFC, vnow);
    check_order("external");
    check_no_stuck("external");
    CHECK(count(0x90, -1) > 100, "notes with a followed clock (%d)", count(0x90, -1));
    CHECK(count_rt(0xF8) == 0, "following, no clock goes out");
    /* the subticks of one tick are spread: a drum hit lands between ticks too */
    {
        int off_grid = 0;
        for (i = 0; i < nlog; i++)
            if (log_[i].n == 3 && (log_[i].m[0] & 0xF0) == 0x90 && log_[i].t > log_[i].at)
                off_grid++;
        CHECK(off_grid > 0, "some notes are scheduled ahead of the tick that made them");
    }
    printf("external: %d messages, bar %d of the phrase at the end\n", nlog, bars_seen);
    /* a play press while following starts the parts at once, on the clock's next tick */
    fresh();
    eng_set(F_CLOCK, CLK_EXT, vnow);
    setup_rig();
    eng_play(1, vnow);
    run_external(1500, 120, 0);
    CHECK(count(0x90, -1) > 5, "Play while following: notes (%d)", count(0x90, -1));
    eng_play(0, vnow);
    check_no_stuck("external play");
}

static int collect(int ch, uint8_t *buf, int cap, int64_t *tbuf, int64_t t0)
{
    int i, n = 0;
    for (i = 0; i < nlog && n < cap; i++)
        if (log_[i].n == 3 && (log_[i].m[0] & 15) == ch) {
            buf[n] = (uint8_t)(log_[i].m[0] & 0xF0);
            buf[n + 0] = log_[i].m[0];
            tbuf[n++] = log_[i].t - t0;
        }
    return n;
}
static void test_parts_fit(void)
{
    /* Parts recorded on separate passes must fit: the bass alone is the bass of the full band, event for event. */
    static uint8_t a[20000], b[20000];
    static int64_t ta[20000], tb[20000];
    int na, nb, i;
    int64_t t0;
    fresh();
    eng_set(F_CLOCK, CLK_INT, vnow);
    eng_set(F_SEED, 4242, vnow);
    setup_rig();
    eng_play(1, vnow);
    t0 = vnow;
    run_internal(8000);
    eng_play(0, vnow);
    na = collect(0, a, 20000, ta, t0);
    fresh();
    eng_set(F_CLOCK, CLK_INT, vnow);
    eng_set(F_SEED, 4242, vnow);
    setup_rig();
    for (i = 0; i < GEN_NPARTS; i++)
        if (i != GP_BASS)
            eng_set(F_PART(i, PF_ON), 0, vnow);
    eng_play(1, vnow);
    t0 = vnow;
    run_internal(8000);
    eng_play(0, vnow);
    nb = collect(0, b, 20000, tb, t0);
    CHECK(na > 30 && na == nb, "bass alone %d events, with the band %d", nb, na);
    for (i = 0; i < na && i < nb; i++)
        if (a[i] != b[i] || ta[i] / 1000 != tb[i] / 1000) {
            CHECK(0, "bass event %d differs apart and together", i);
            break;
        }
    printf("parts fit: bass %d events, the same alone and in the band\n", na);
}

static void test_channels_follow(void)
{
    int i;
    fresh();
    eng_set(F_CLOCK, CLK_INT, vnow);
    eng_set(F_PART(GP_BASS, PF_ON), 1, vnow);
    eng_set(F_PART(GP_BASS, PF_CHAN), 0, vnow);
    eng_play(1, vnow);
    run_internal(1800);
    eng_set(F_PART(GP_BASS, PF_CHAN), 9, vnow);  /* the bass moves to another instrument mid-bar */
    run_internal(2500);
    eng_play(0, vnow);
    check_no_stuck("channel change");
    CHECK(count(0x90, 0) > 0 && count(0x90, 9) > 0, "bass on both channels (%d, %d)", count(0x90, 0), count(0x90, 9));
    for (i = 0; i < nlog; i++)
        if (log_[i].n == 3 && (log_[i].m[0] & 0xF0) == 0x90 && log_[i].m[2] && (log_[i].m[0] & 15) != 0 && (log_[i].m[0] & 15) != 9)
            CHECK(0, "a note on channel %d", (log_[i].m[0] & 15) + 1);
    /* the Simmons lanes on their own channel */
    fresh();
    eng_set(F_PART(GP_DRUMS, PF_ON), 1, vnow);
    eng_set(F_PART(GP_DRUMS, PF_CHAN), 3, vnow);
    eng_set(F_DSET, DS_SIM, vnow);
    eng_set(F_SIM_CHAN, 10, vnow);
    eng_set(F_STYLE, GS_808, vnow);
    eng_play(1, vnow);
    run_internal(16000);
    eng_play(0, vnow);
    CHECK(count(0x90, 9) > 0 && count(0x90, 3) == 0, "Simmons lanes on channel 10 (%d), none on the drums' (%d)", count(0x90, 9), count(0x90, 3));
}

static void test_arp_seq(void)
{
    int i, on = 0, notes[8], nn = 0;
    fresh();
    eng_set(F_CLOCK, CLK_INT, vnow);
    eng_set(F_KEYS_CHAN, 4, vnow);
    eng_set(F_ARP_ON, 1, vnow);
    eng_set(F_ARP_RATE, RATE_16, vnow);
    eng_arp_press(60);
    eng_arp_press(64);
    eng_arp_press(67);
    run_internal(1000);
    for (i = 0; i < nlog; i++)
        if (log_[i].n == 3 && log_[i].m[0] == 0x94 && log_[i].m[2]) {
            if (nn < 8)
                notes[nn++] = log_[i].m[1];
            on++;
        }
    CHECK(on >= 7 && on <= 9, "arp at 1/16, 120 BPM: %d notes in a second", on);
    CHECK(nn >= 4 && notes[0] == 60 && notes[1] == 64 && notes[2] == 67 && notes[3] == 60, "arp order up: %d %d %d %d", notes[0], notes[1], notes[2], notes[3]);
    eng_arp_release(60);
    eng_arp_release(64);
    eng_arp_release(67);
    {
        int before = count(0x90, 4);
        run_internal(500);
        CHECK(count(0x90, 4) - before <= 1, "nothing arpeggiated after the keys are up (%d more)", count(0x90, 4) - before);
    }
    eng_set(F_ARP_ON, 0, vnow);
    check_no_stuck("arp");
    /* the sequencer: track 1 (4 hits in 16) unmuted, a note every 500 ms */
    fresh();
    eng_set(F_CLOCK, CLK_INT, vnow);
    eng_set(F_TRK(0, TF_MUTE), 0, vnow);
    eng_play(1, vnow);
    run_internal(4000);
    eng_play(0, vnow);
    CHECK(count(0x90, 9) >= 7 && count(0x90, 9) <= 9, "sequencer track 1: %d kicks in two bars", count(0x90, 9));
    check_no_stuck("sequencer");
}

static void test_bass_range(void)
{
    /* No style, bass mode or octave setting may put a bass note outside its device's range, or above G5 on any device. */
    int style, mode, oct, dev, i, top = 0, n = 0;
    static const int DEVS[3] = {DV_GENERIC, DV_MB33, DV_HADES};
    static const int HI[3] = {79, 72, 79};
    for (style = 0; style < GEN_NSTYLES; style++)
        for (mode = 0; mode < NBM; mode++)
            for (oct = -2; oct <= 2; oct += 2)
                for (dev = 0; dev < 3; dev++) {
                    fresh();
                    eng_set(F_CLOCK, CLK_INT, vnow);
                    eng_set(F_STYLE, style, vnow);
                    eng_set(F_SEED, 100 + style * 31 + mode, vnow);
                    eng_set(F_PART(GP_BASS, PF_ON), 1, vnow);
                    eng_set(F_PART(GP_BASS, PF_CHAN), 9, vnow);
                    eng_set(F_PART(GP_BASS, PF_DEV), DEVS[dev], vnow);
                    eng_set(F_PART(GP_BASS, PF_MODE), mode, vnow);
                    eng_set(F_PART(GP_BASS, PF_OCT), oct, vnow);
                    eng_play(1, vnow);
                    run_internal(6000);
                    eng_play(0, vnow);
                    for (i = 0; i < nlog; i++)
                        if (log_[i].n == 3 && log_[i].m[0] == 0x99 && log_[i].m[2]) {
                            n++;
                            if (log_[i].m[1] > top)
                                top = log_[i].m[1];
                            if (log_[i].m[1] > HI[dev]) {
                                CHECK(0, "bass note %d (style %d, mode %d, octave %d, device %d)", log_[i].m[1], style,
                                      mode, oct, DEVS[dev]);
                                return;
                            }
                        }
                }
    printf("bass range: %d notes, the highest %d\n", n, top);
}
static int count_note(int ch, int note)
{
    int i, c = 0;
    for (i = 0; i < nlog; i++)
        c += log_[i].n == 3 && log_[i].m[0] == (0x90 | ch) && log_[i].m[1] == note && log_[i].m[2];
    return c;
}
/* a little over four bars at 120 BPM of the drums (channel 4) and the bass (channel 1), fills off */
static void run_band(int style, int seed, int id, int v, int id2, int v2)
{
    fresh();
    eng_set(F_CLOCK, CLK_INT, vnow);
    eng_set(F_STYLE, style, vnow);
    eng_set(F_SEED, seed, vnow);
    eng_set(F_FILL_EVERY, 1, vnow);
    eng_set(F_PART(GP_DRUMS, PF_ON), 1, vnow);
    eng_set(F_PART(GP_DRUMS, PF_CHAN), 3, vnow);
    eng_set(F_PART(GP_BASS, PF_ON), 1, vnow);
    eng_set(F_PART(GP_BASS, PF_CHAN), 0, vnow);
    if (id >= 0)
        eng_set(id, v, vnow);
    if (id2 >= 0)
        eng_set(id2, v2, vnow);
    eng_play(1, vnow);
    run_internal(8100);
    eng_play(0, vnow);
}
static void test_new_controls(void)
{
    /* The settings added at the end keep the old ones' numbers, so settings saved by the first app still load. */
    int k, hats, a, b, m;
    CHECK(F_TRK0 == 101 && F_X0 == 121 && GS_ACIDDREAM == 23, "saved setting and style numbers moved");
    /* Hat density: quarters, eighths, sixteenths of closed hat (note 43), whatever the style has */
    for (k = 1; k <= 3; k++) {
        int want = 4 << (k - 1);
        run_band(GS_PFUNK, 5, F_HAT_DENS, k, -1, 0);
        hats = count_note(3, 43);
        CHECK(hats >= want * 4 && hats <= want * 4 + want / 2 + 1, "hat density %d: %d hats in four bars", k, hats);
        run_band(GS_NEP_MIN, 5, F_HAT_DENS, k, -1, 0);       /* (a style with no hat lane) */
        hats = count_note(3, 43);
        CHECK(hats >= want * 4 && hats <= want * 4 + want / 2 + 1, "hat density %d, no hat lane: %d hats", k, hats);
        run_band(GS_BOOMBAP, 5, F_HAT_DENS, k, -1, 0);       /* (Cut-A-Dor's drums) */
        hats = count_note(3, 43);
        CHECK(hats >= want * 4 && hats <= want * 4 + want / 2 + 1, "hat density %d, Boom Bap: %d hats", k, hats);
    }
    /* Bass and kick: locked, the bass plays where the kick does and nowhere else */
    run_band(GS_PFUNK, 9, F_COUPLE, 3, -1, 0);
    a = count_note(3, 36);
    b = count(0x90, 0);
    CHECK(a > 6 && a == b, "locked: %d kicks, %d bass notes", a, b);
    run_band(GS_PFUNK, 9, F_COUPLE, 2, -1, 0);
    CHECK(count(0x90, 0) >= a, "following: at least a bass note a kick (%d, %d kicks)", count(0x90, 0), a);
    run_band(GS_NEP_MIN, 9, -1, 0, -1, 0);                   /* (this style locks by itself) */
    CHECK(count_note(3, 36) == count(0x90, 0), "Neptunes Minimal: %d kicks, %d bass notes", count_note(3, 36), count(0x90, 0));
    /* Flam: more snare notes; Ratchets: more hats and snares */
    run_band(GS_PFUNK, 9, -1, 0, -1, 0);
    a = count_note(3, 37);
    b = count_note(3, 43);
    run_band(GS_PFUNK, 9, F_FLAM, 127, -1, 0);
    CHECK(count_note(3, 37) >= a + 6, "flam: %d snare notes, %d without", count_note(3, 37), a);
    check_no_stuck("flam");
    run_band(GS_PFUNK, 9, F_RATCHET, 127, -1, 0);
    CHECK(count_note(3, 43) > b + 8 && count_note(3, 37) > a, "ratchets: hats %d (%d), snares %d (%d)", count_note(3, 43), b,
          count_note(3, 37), a);
    check_no_stuck("ratchets");
    /* Hat swing off: no closed hat is late any more (the hat lane has no lateness in this style) */
    run_band(GS_PFUNK, 9, F_SWING, 20, F_HAT_STRAIGHT, 100);
    CHECK(count_note(3, 43) == b, "straight hats: %d hats, %d before", count_note(3, 43), b);
    /* Cut-A-Dor's six: a kick on every bar's one, a backbeat, nothing left on, and the same twice */
    for (m = 0; m < CAD_NSTYLES; m++)
        for (k = 0; k < 6; k++) {
            run_band(GS_DILLA, 40 + k * 17, F_PART(GP_DRUMS, PF_MODE), DM_CAD0 + m, F_MUTATE, k);
            a = count_note(3, 36);
            b = count_note(3, 37);
            hats = nlog;
            CHECK(a >= 5 && b >= (m == CAD_HALFTIME ? 4 : 8), "%s, seed %d: %d kicks, %d snares", CAD_NAME[m], 40 + k * 17, a, b);
            check_no_stuck(CAD_NAME[m]);
            check_order(CAD_NAME[m]);
            run_band(GS_DILLA, 40 + k * 17, F_PART(GP_DRUMS, PF_MODE), DM_CAD0 + m, F_MUTATE, k);
            CHECK(nlog == hats && count_note(3, 36) == a, "%s is not the same twice", CAD_NAME[m]);
        }
    /* Fills every bar, and the chords' voicings, leave nothing on */
    for (k = 2; k <= 6; k++) {
        run_band(GS_NERD, 3, F_FILL_EVERY, k, F_SPACE, 80);
        check_no_stuck("fill every");
    }
    for (k = 0; k <= 3; k++)
        for (m = 0; m < GEN_NSTYLES; m++) {
            run_band(m, 3, F_VOICE, k, F_PART(GP_CHORD, PF_ON), 1);
            check_no_stuck("voicing");
        }
    for (m = 0; m < GEN_NSTYLES; m++) {                      /* every style has a family and a tempo range */
        int lo, hi;
        gen_style_bpm(m, &lo, &hi);
        CHECK(gen_style_cat(m) < GEN_NCATS && lo >= 40 && hi > lo && hi <= 240, "%s: family %d, %d to %d BPM", gen_style_name(m),
              gen_style_cat(m), lo, hi);
    }
    printf("new controls: hat density, coupling, flam, ratchets, Cut-A-Dor modes, fills, voicings ok\n");
}

/* 0.2.1: the notes that used to stay on (review findings 1 to 3) */
static void quiet(int64_t ms)                    /* the engine's thread running, no clock coming in */
{
    int64_t end = vnow + ms * MS;
    while (vnow < end) {
        eng_pump(vnow);
        vnow += 4 * MS;
    }
}
static void test_stuck_notes(void)
{
    /* 1. following the MPC, the clock stops with no Stop (cable out): every note ends within about 300 ms */
    fresh();
    eng_set(F_CLOCK, CLK_EXT, vnow);
    setup_rig();
    eng_midi_clock(0xFA, vnow);
    run_external(3000, 100, 0);
    quiet(400);
    check_no_stuck("followed clock gone, no Stop");
    {
        int before = nlog;
        quiet(1000);
        CHECK(nlog == before, "nothing more once the notes are ended (%d messages)", nlog - before);
        run_external(2000, 100, 0);              /* the clock comes back: the band goes on */
        CHECK(count(0x90, -1) > 0 && nlog > before, "the band plays on when the clock comes back");
        eng_midi_clock(0xFC, vnow);
        check_no_stuck("clock back, then Stop");
    }
    /* 2. the clock mode changed while playing: the transport stops and nothing is left on */
    fresh();
    eng_set(F_CLOCK, CLK_INT_OUT, vnow);
    setup_rig();
    eng_play(1, vnow);
    run_internal(2500);
    eng_set(F_CLOCK, CLK_EXT, vnow);
    quiet(500);
    check_no_stuck("Send clock -> Follow MPC while playing");
    CHECK(count_rt(0xFC) == 1, "a Stop goes out when leaving Send clock (%d)", count_rt(0xFC));
    {
        int32_t st[8];
        eng_status(st, vnow);
        CHECK(st[0] == 0, "the transport is stopped after the clock mode changes");
    }
    fresh();
    eng_set(F_CLOCK, CLK_INT, vnow);
    setup_rig();
    eng_play(1, vnow);
    run_internal(2500);
    eng_set(F_CLOCK, CLK_EXT, vnow);
    check_no_stuck("Internal -> Follow MPC while playing");
    /* 3. following, the MPC's Stop ends the arp's note at once, with no clock after it */
    fresh();
    eng_set(F_CLOCK, CLK_EXT, vnow);
    eng_set(F_KEYS_CHAN, 4, vnow);
    eng_set(F_ARP_ON, 1, vnow);
    eng_set(F_ARP_RATE, RATE_4, vnow);           /* quarter notes, held half a beat */
    eng_arp_press(60);
    eng_arp_press(64);
    eng_midi_clock(0xFA, vnow);
    run_external(1100, 120, 0);                  /* the last quarter has just started: its note is on */
    {
        int i, on = 0;
        for (i = 0; i < nlog; i++)
            if (log_[i].n == 3 && log_[i].m[0] == 0x94)
                on += log_[i].m[2] ? 1 : 0;
        CHECK(on >= 3, "arp notes while following (%d)", on);
    }
    eng_midi_clock(0xFC, vnow);
    check_no_stuck("arp, the MPC's Stop");
}

/* Round 1 (0.2.2): review findings 7, 8, 10, 13, 14 */
static void test_review_fixes(void)
{
    int i, j, k, bad, n, d0, d1;
    int64_t tick, t0;
    /* 7: following, two ticks in one USB packet (the same time) keep the tick length; app at 60, MPC at 180 */
    fresh();
    eng_set(F_TEMPO, 60, vnow);
    eng_set(F_CLOCK, CLK_EXT, vnow);
    tick = 2500000000LL / 180;
    for (i = 0; i < 48; i++) {
        vnow += tick;
        eng_pump(vnow);
        eng_midi_clock(0xF8, vnow);
        if (i % 8 == 7)                          /* now and then a tick at once after the last */
            eng_midi_clock(0xF8, vnow);
        if (i > 8 && i % 8 == 7)
            CHECK(E.ext_per > tick * 8 / 10 && E.ext_per < tick * 12 / 10, "finding 7: tick length %lld ns after two ticks at once (MPC %lld)",
                  (long long)E.ext_per, (long long)tick);
    }
    /* 8: following, everything a tick makes (its own notes too) is stamped the fixed delay or more after it arrived */
    fresh();
    eng_set(F_CLOCK, CLK_EXT, vnow);
    eng_set(F_PART(GP_DRUMS, PF_ON), 1, vnow);
    eng_set(F_PART(GP_BASS, PF_ON), 1, vnow);
    eng_midi_clock(0xFA, vnow);
    nlog = 0;
    run_external(4000, 100, 300000);
    for (i = 0, bad = 0, n = 0; i < nlog; i++)
        if (log_[i].n == 3 && ((log_[i].m[0] & 0xF0) == 0x90 || (log_[i].m[0] & 0xF0) == 0x80)) {
            n++;
            bad += log_[i].t < log_[i].at + EXT_OUT_DELAY_NS;
        }
    CHECK(n > 20 && bad == 0, "finding 8: %d of %d notes stamped less than the fixed delay after their tick", bad, n);
    /* 10: after a stall of 250 ms the clock's ticks and the generators' subticks are still in step */
    fresh();
    eng_set(F_CLOCK, CLK_INT_OUT, vnow);
    eng_set(F_PART(GP_DRUMS, PF_ON), 1, vnow);
    eng_play(1, vnow);
    run_internal(1500);
    d0 = (int)((E.sub_n - (uint32_t)g.pos) & 3u);
    vnow += 250 * MS;                            /* the thread held up */
    run_internal(1500);
    d1 = (int)((E.sub_n - (uint32_t)g.pos) & 3u);
    CHECK(d0 == d1, "finding 10: after a stall the clock's tick phase moved from %d to %d against the generators", d0, d1);
    eng_play(0, vnow);
    check_no_stuck("stall");
    /* 13: Cut-A-Dor, Space 70 and hats in 16ths: no closed hat in the half bars Space clears (bars 5-8 of 8) */
    for (k = 0; k < CAD_NSTYLES; k++) {
        fresh();
        eng_set(F_CLOCK, CLK_INT, vnow);
        eng_set(F_PART(GP_DRUMS, PF_ON), 1, vnow);
        eng_set(F_PART(GP_DRUMS, PF_CHAN), 3, vnow);
        eng_set(F_PART(GP_DRUMS, PF_MODE), DM_CAD0 + k, vnow);
        eng_set(F_FILL_EVERY, 1, vnow);
        eng_set(F_SPACE, 70, vnow);
        eng_set(F_HAT_DENS, 3, vnow);
        eng_play(1, vnow);
        run_internal(16100);
        eng_play(0, vnow);
        t0 = -1;
        for (i = 0; i < nlog && t0 < 0; i++)
            if (log_[i].n == 3 && log_[i].m[0] == 0x93 && log_[i].m[1] == 36 && log_[i].m[2])
                t0 = log_[i].t;              /* the first kick: bar 1, beat 1 */
        for (i = 0, bad = 0; i < nlog; i++)
            if (log_[i].n == 3 && log_[i].m[0] == 0x93 && log_[i].m[1] == 43 && log_[i].m[2] && t0 >= 0) {
                int64_t sx = (log_[i].t - t0) * 2 / (125 * MS);   /* in 32nds at 120 BPM */
                int bar = (int)(sx / 32), half = (int)(sx % 32);
                bad += bar % 8 >= 4 && half >= 17 && half <= 29;   /* (31: the next bar's first hat, a little early) */
            }
        CHECK(t0 >= 0 && bad == 0, "finding 13: %s, %d closed hats in the gaps Space leaves", CAD_NAME[k], bad);
    }
    /* 14: bass locked to the kick in Grids and Turing: every bass note has a kick with it */
    for (k = 0; k < 2; k++)
        for (j = 0; j < 4; j++) {
            run_band(j * 7, 11 + j, F_PART(GP_DRUMS, PF_MODE), k ? DM_TURING : DM_GRIDS, F_COUPLE, 3);
            for (i = 0, bad = 0, n = 0; i < nlog; i++)
                if (log_[i].n == 3 && log_[i].m[0] == 0x90 && log_[i].m[2]) {
                    int q, hit = 0;
                    n++;
                    for (q = 0; q < nlog && !hit; q++)
                        hit = log_[q].n == 3 && log_[q].m[0] == 0x93 && log_[q].m[1] == 36 && log_[q].m[2] &&
                              llabs(log_[q].t - log_[i].t) < 40 * MS;
                    bad += !hit;
                }
            CHECK(n > 0 && bad == 0, "finding 14: %s, %s: %d of %d bass notes have no kick under them", k ? "Turing" : "Grids",
                  gen_style_name(j * 7), bad, n);
        }
    printf("review fixes 7, 8, 10, 13, 14: ok\n");
}

static void test_panic(void)
{
    fresh();
    eng_panic(vnow);
    /* (0.3.2: no Reset All Controllers on the Tanzbär's channel, where the drums are by default) */
    CHECK(count(0xB0, -1) == 47 && count(0xE0, -1) == 16, "panic: CC 123, 120 on 16 channels, 121 on 15, bend on 16 (%d, %d)",
          count(0xB0, -1), count(0xE0, -1));
}

/* 0.3.0: Chopped and screwed's last bar replays its first half; the 42 styles' metadata is complete */
static void test_styles_030(void)
{
    int i, a = 0, b = 0, bad = 0;
    int64_t t0, bar;
    static int64_t ta[64], tb[64];
    static uint8_t na[64], nb[64];
    for (i = 0; i < GEN_NSTYLES; i++) {
        int lo, hi;
        gen_style_bpm(i, &lo, &hi);
        CHECK(lo >= 50 && hi > lo && gen_style_ref(i)[0] && gen_style_name(i)[0], "style %d metadata incomplete", i);
    }
    CHECK(!gen_style_tanz(GS_WUTANG) && gen_style_tanz(GS_MEMPHIS) && !gen_style_tanz(GS_BIGBEAT),
          "Tanzbar flags");
    fresh();
    eng_set(F_TEMPO, 120, vnow);
    eng_set(F_STYLE, GS_CHOPPED, vnow);
    eng_set(F_PHRASE, 0, vnow);                  /* a one-bar phrase: every bar is its last */
    eng_set(F_FEEL, 0, vnow);
    eng_set(F_CLOCK, CLK_INT_OUT, vnow);
    eng_set(F_PART(GP_DRUMS, PF_ON), 1, vnow);
    eng_play(1, vnow);
    run_internal(6000);
    eng_play(0, vnow);
    bar = 2000 * MS;                             /* 4/4 at 120 */
    for (i = 0, t0 = -1; i < nlog; i++)
        if (log_[i].n == 3 && (log_[i].m[0] & 0xF0) == 0x90 && log_[i].m[2]) {
            if (t0 < 0)
                t0 = log_[i].t + bar;            /* the second bar on */
            if (log_[i].t < t0 - 5 * MS)         /* (hits on the beat may be pushed a little early) */
                continue;
            if (log_[i].t < t0 + bar / 2 - 5 * MS && a < 64)
                ta[a] = log_[i].t - t0, na[a++] = log_[i].m[1];
            else if (log_[i].t < t0 + bar - 5 * MS && b < 64)
                tb[b] = log_[i].t - t0 - bar / 2, nb[b++] = log_[i].m[1];
        }
    if (getenv("CHOPDBG")) for (i = 0; i < 64 && (i < a || i < b); i++)
        printf("%2d  %3d %8lld   %3d %8lld\n", i, i < a ? na[i] : 0, i < a ? (long long)ta[i] / 1000 : 0, i < b ? nb[i] : 0, i < b ? (long long)tb[i] / 1000 : 0);
    for (i = 0; i < a && i < b; i++)
        bad += na[i] != nb[i] || ta[i] - tb[i] > MS || tb[i] - ta[i] > MS;
    CHECK(a > 2 && a == b && !bad, "chop: first half %d notes, second %d, %d differ", a, b, bad);
}

/* 0.3.1: Song Position Pointer and Continue when following, the output delay trim, the fuller panic */
static int64_t spp_tick;
static void spp_ticks(int n)
{
    while (n-- > 0) {
        vnow += spp_tick;
        eng_pump(vnow);
        eng_midi_clock(0xF8, vnow);
    }
}
static int spp_take(int64_t mark, uint8_t *nv, int64_t *tt, int cap)   /* drum note-ons after mark */
{
    int i, k = 0;
    for (i = 0; i < nlog && k < cap; i++)
        if (log_[i].n == 3 && (log_[i].m[0] & 0xF0) == 0x90 && log_[i].m[2] && log_[i].at > mark) {
            nv[k * 2] = log_[i].m[1];
            nv[k * 2 + 1] = log_[i].m[2];
            tt[k++] = log_[i].t - mark;
        }
    return k;
}
static void spp_setup(void)
{
    fresh();
    eng_set(F_TEMPO, 100, vnow);
    eng_set(F_CLOCK, CLK_EXT, vnow);
    eng_set(F_PART(GP_DRUMS, PF_ON), 1, vnow);
    eng_set(F_PART(GP_BASS, PF_ON), 1, vnow);
}
static void test_qol_031(void)
{
    static const int POS[] = {0, 5, 16, 37, 70, 131};
    static uint8_t ra[512], rb[512];
    static int64_t ta[256], tb[256];
    int i, j, na, nb, bad, n;
    int64_t mark;
    spp_tick = 2500000000LL / 100;
    for (j = 0; j < (int)(sizeof POS / sizeof POS[0]); j++) {
        int p = POS[j];
        spp_setup();                             /* the reference: played from the top to the position */
        eng_midi_clock(0xFA, vnow);
        spp_ticks(p * 6);
        mark = vnow;
        spp_ticks(96);
        na = spp_take(mark, ra, ta, 256);
        spp_setup();                             /* Start, a while, Stop, SPP, Continue */
        eng_midi_clock(0xFA, vnow);
        spp_ticks(40);
        eng_midi_clock(0xFC, vnow);
        check_no_stuck("spp stop");
        eng_midi_spp(p, vnow);
        eng_midi_clock(0xFB, vnow);
        CHECK(gen_bar() == (int)((unsigned)p / 16u % (unsigned)gen_phrase_bars()) && gen_step() == p % 16,
              "spp %d: at bar %d step %d", p, gen_bar(), gen_step());
        mark = vnow;
        spp_ticks(96);
        nb = spp_take(mark, rb, tb, 256);
        for (i = 0, bad = 0; i < na && i < nb; i++)
            bad += ra[i * 2] != rb[i * 2] || ra[i * 2 + 1] != rb[i * 2 + 1] || ta[i] != tb[i];
        CHECK(na > 4 && na == nb && !bad, "spp %d: from the top %d notes, after Continue %d, %d differ", p, na, nb, bad);
        eng_midi_clock(0xFC, vnow);
        check_no_stuck("spp end");
    }
    /* Continue with no SPP goes on where Stop left it */
    spp_setup();
    eng_midi_clock(0xFA, vnow);
    spp_ticks(6 * 20);
    eng_midi_clock(0xFC, vnow);
    eng_midi_clock(0xFB, vnow);
    CHECK(gen_bar() == 1 % gen_phrase_bars() && gen_step() == 4, "continue: at bar %d step %d", gen_bar(), gen_step());
    eng_midi_clock(0xFC, vnow);
    /* Start after an SPP starts from the top */
    eng_midi_spp(70, vnow);
    eng_midi_clock(0xFA, vnow);
    CHECK(gen_bar() == 0 && gen_step() == 0, "start after spp: at bar %d step %d", gen_bar(), gen_step());
    eng_midi_clock(0xFC, vnow);
    /* the output delay trim moves everything a tick makes */
    for (j = 0; j < 2; j++) {
        int d = j ? 17 : 0;
        spp_setup();
        eng_set(F_OUT_DELAY, d, vnow);
        eng_midi_clock(0xFA, vnow);
        nlog = 0;
        spp_ticks(48);
        for (i = 0, bad = 0, n = 0; i < nlog; i++)
            if (log_[i].n == 3 && (log_[i].m[0] & 0xF0) == 0x90) {
                n++;
                bad += log_[i].t < log_[i].at + d * MS;
            }
        CHECK(n > 4 && !bad, "delay %d ms: %d of %d notes early", d, bad, n);
        eng_midi_clock(0xFC, vnow);
    }
    CHECK(eng_get(F_OUT_DELAY) == 17, "delay setting kept");
    eng_set(F_OUT_DELAY, 99, vnow);
    CHECK(eng_get(F_OUT_DELAY) == 40, "delay clamped to 40 ms");
    /* panic: CC 123, 120, 121 and bend centred on every channel */
    nlog = 0;
    eng_panic(vnow);
    for (i = 0, n = 0; i < nlog; i++)
        n += log_[i].n == 3 && (((log_[i].m[0] & 0xF0) == 0xB0 && (log_[i].m[1] == 123 || log_[i].m[1] == 120 || log_[i].m[1] == 121)) ||
                                ((log_[i].m[0] & 0xF0) == 0xE0 && log_[i].m[1] == 0 && log_[i].m[2] == 64));
    CHECK(n == 63, "panic: %d of 63 reset messages (no CC 121 to the Tanzbar)", n);
    printf("0.3.1: song position (6 positions), continue, start, output delay, panic ok\n");
}

/* 0.3.2: the melodic cowbell: a tune CC 30-100 ms before every cowbell hit, from the table, nothing when off */
static void test_bell_032(void)
{
    static const uint8_t TAB[12] = {42, 49, 57, 67, 81, 255, 4, 9, 15, 21, 28, 35};   /* the Tanzbär's, from C */
    int i, j, k, n, bad, ccs, on;
    for (on = 0; on < 2; on++) {
        fresh();
        eng_set(F_TEMPO, 120, vnow);
        eng_set(F_STYLE, GS_MEMPHIS, vnow);
        eng_set(F_CLOCK, CLK_INT_OUT, vnow);
        eng_set(F_PART(GP_DRUMS, PF_ON), 1, vnow);
        eng_bell_table(84, TAB);
        eng_set(F_BELL, on, vnow);
        eng_play(1, vnow);
        run_internal(8000);
        eng_play(0, vnow);
        for (i = 0, n = 0, bad = 0, ccs = 0; i < nlog; i++) {
            if (log_[i].n == 3 && (log_[i].m[0] & 0xF0) == 0xB0 && log_[i].m[1] == 84) {
                ccs++;
                for (k = 0; k < 12 && TAB[k] != log_[i].m[2]; k++)
                    ;
                bad += k == 12 || TAB[k] > 127;
            }
            if (on && log_[i].n == 3 && (log_[i].m[0] & 0xF0) == 0x90 && log_[i].m[2] && log_[i].m[1] == 45 &&
                log_[i].t > log_[0].t + 2000 * MS) {
                int64_t best = -1;
                for (j = 0; j < nlog; j++)
                    if (log_[j].n == 3 && (log_[j].m[0] & 0xF0) == 0xB0 && log_[j].m[1] == 84 && log_[j].t <= log_[i].t &&
                        (log_[j].m[0] & 15) == (log_[i].m[0] & 15) && log_[j].t > best)
                        best = log_[j].t;
                n++;
                bad += best < 0 || log_[i].t - best < 30 * MS || log_[i].t - best > 100 * MS;
            }
        }
        if (on)
            CHECK(n > 8 && ccs >= n && !bad, "bell: %d cowbell hits, %d tune CCs, %d wrong", n, ccs, bad);
        else
            CHECK(ccs == 0, "bell off: %d tune CCs", ccs);
    }
    printf("0.3.2: melodic cowbell ok\n");
}

/* 0.3.3: batch [D]'s written bell lines */
static void test_bell_lines_033(void)
{
    static const uint8_t TAB[12] = {42, 49, 57, 67, 81, 255, 4, 9, 15, 21, 28, 35};
    static const int ST[4] = {GS_BELLFUNK, GS_MIAMIBELL, GS_DETBELL, GS_AFROBELL};
    static const int FUNK[2][6] = {{1, 1, 3, 1, 4, 3}, {1, 1, 3, 5, 4, 7}};
    static const int FSTEP[6] = {0, 3, 6, 8, 11, 13};
    int i, j, k, r, bad, n, li;
    fresh();
    eng_set(F_STYLE, GS_BELLFUNK, vnow);
    li = bell_line_of(GS_BELLFUNK);
    for (bad = 0, i = 0; i < 2; i++)                     /* as written */
        for (j = 0; j < 6; j++)
            bad += bell_deg(li, (uint32_t)i, FSTEP[j]) != FUNK[i][j];
    CHECK(li >= 0 && !bad, "bell funk: %d degrees not as written", bad);
    eng_set(F_STYLE, GS_AFROBELL, vnow);
    li = bell_line_of(GS_AFROBELL);
    CHECK(bell_deg(li, 2, 14) == 5 && bell_deg(li, 3, 14) == 7 && bell_deg(li, 3, 12) == 1 && bell_deg(li, 3, 0) == 1,
          "afro-funk: fifth, b7 every 4th bar, root on strong hits");
    for (k = 0; k < 4; k++) {                            /* Mutate: at most one note a bar, never the first */
        int moved = 0, firsts = 0, many = 0;
        eng_set(F_STYLE, ST[k], vnow);
        li = bell_line_of(ST[k]);
        for (r = 0; r < 96; r++) {
            int diff = 0, base[16];
            eng_set(F_MUTATE, 0, vnow);
            for (j = 0; j < 16; j++)
                base[j] = bell_deg(li, (uint32_t)r, j);
            eng_set(F_MUTATE, 8, vnow);
            for (j = 0; j < 16; j++)
                if (BELL_LINES[li].bar[r & 1][j] != '.' && bell_deg(li, (uint32_t)r, j) != base[j]) {
                    diff++;
                    firsts += j == 0;
                }
            moved += diff > 0;
            many += diff > 1;
        }
        CHECK(moved > 10 && !firsts && !many, "style %d mutate: %d bars moved, %d firsts, %d bars with more", ST[k], moved,
              firsts, many);
    }
    eng_set(F_MUTATE, 0, vnow);
    for (k = 0; k < 4; k++)                              /* in the app: C and F minor, CCs from the line, in time */
        for (r = 0; r < 2; r++) {
            int64_t t0 = -1, bar = 2000 * MS, best;
            fresh();
            eng_set(F_TEMPO, 120, vnow);
            eng_set(F_STYLE, ST[k], vnow);
            eng_set(F_ROOT, r ? 5 : 0, vnow);
            eng_set(F_MINOR, 1, vnow);
            eng_set(F_CLOCK, CLK_INT_OUT, vnow);
            eng_set(F_PART(GP_DRUMS, PF_ON), 1, vnow);
            eng_bell_table(84, TAB);
            eng_set(F_BELL, 1, vnow);
            eng_play(1, vnow);
            run_internal(8000);
            eng_play(0, vnow);
            li = bell_line_of(ST[k]);
            for (i = 0, n = 0, bad = 0; i < nlog; i++) {
                int64_t rel;
                int b, step, want;
                if (!(log_[i].n == 3 && (log_[i].m[0] & 0xF0) == 0x90 && log_[i].m[2] && log_[i].m[1] == 45))
                    continue;
                if (t0 < 0)
                    t0 = log_[i].t;
                rel = log_[i].t - t0 + 30 * MS;
                b = (int)(rel / bar);
                step = (int)(rel % bar / (bar / 16));
                if (b < 1)
                    continue;
                for (j = 0, best = -1; j < nlog; j++)
                    if (log_[j].n == 3 && (log_[j].m[0] & 0xF0) == 0xB0 && log_[j].m[1] == 84 && log_[j].t <= log_[i].t &&
                        log_[j].t > (best < 0 ? -1 : log_[best].t))
                        best = j;
                want = bell_val((g.c->root + scale_semis(bell_deg(li, (uint32_t)b, step))) % 12);
                n++;
                bad += best < 0 || log_[i].t - log_[best].t < 30 * MS || log_[i].t - log_[best].t > 100 * MS ||
                       log_[best].m[2] != want || want == 255;
            }
            CHECK(n > 12 && !bad, "style %d key %s: %d bell hits, %d wrong", ST[k], r ? "F" : "C", n, bad);
        }
    printf("0.3.3: bell lines ok\n");
}

/* 0.3.4: the pocket option: off leaves the drums as they were; on moves them by the pocket's offsets */
static void test_pocket_034(void)
{
    static uint8_t na[512], nb[512];
    static int64_t ta[512], tb[512];
    int on, i, n[2] = {0, 0}, moved = 0, same = 1, late = 0;
    for (on = 0; on < 2; on++) {
        fresh();
        eng_set(F_TEMPO, 120, vnow);
        eng_set(F_STYLE, GS_DILLA, vnow);
        eng_set(F_CLOCK, CLK_INT_OUT, vnow);
        eng_set(F_PART(GP_DRUMS, PF_ON), 1, vnow);
        eng_set(F_POCKET, on, vnow);
        eng_play(1, vnow);
        run_internal(4000);
        eng_play(0, vnow);
        for (i = 0; i < nlog && n[on] < 512; i++)
            if (log_[i].n == 3 && (log_[i].m[0] & 0xF0) == 0x90 && log_[i].m[2]) {
                (on ? nb : na)[n[on]] = log_[i].m[1];
                (on ? tb : ta)[n[on]++] = log_[i].t;
            }
    }
    {                                                    /* (moved hits may swap places: compare note by note) */
        int64_t sa[128] = {0}, sb[128] = {0};
        int ca[128] = {0}, cb[128] = {0};
        for (i = 0; i < n[0]; i++)
            ca[na[i]]++, sa[na[i]] += ta[i];
        for (i = 0; i < n[1]; i++)
            cb[nb[i]]++, sb[nb[i]] += tb[i];
        for (i = 0; i < 128; i++) {
            same &= ca[i] == cb[i];
            moved += sa[i] != sb[i];
        }
        late = sb[37] > sa[37];                          /* the backbeat snares sit later */
    }
    CHECK(n[0] == n[1] && same && moved >= 3 && late, "pocket: %d / %d notes, same notes %d, %d lanes moved, snares late %d",
          n[0], n[1], same, moved, late);
    eng_set(F_POCKET, 0, vnow);
    printf("0.3.4: pocket ok\n");
}

/* 0.3.5: the Simmons lanes' bar rules (with the Simmons in the drum set) */
static void zap_steps(int style, int lane_note_v, uint16_t out[8])
{
    int i, b, st;
    int64_t t0 = -1, bar = 2000 * MS;
    fresh();
    eng_set(F_TEMPO, 120, vnow);
    eng_set(F_STYLE, style, vnow);
    eng_set(F_DSET, DS_ALL, vnow);
    eng_set(F_FILL_EVERY, 1, vnow);                       /* (no fills: just the grids) */
    eng_set(F_CLOCK, CLK_INT_OUT, vnow);
    eng_set(F_PART(GP_DRUMS, PF_ON), 1, vnow);
    eng_play(1, vnow);
    run_internal(17000);
    eng_play(0, vnow);
    for (i = 0; i < 8; i++)
        out[i] = 0;
    for (i = 0; i < nlog; i++)
        if (log_[i].n == 3 && (log_[i].m[0] & 0xF0) == 0x90 && log_[i].m[2]) {
            if (t0 < 0)
                t0 = log_[i].t;
            if (log_[i].m[1] != lane_note_v)
                continue;
            b = (int)((log_[i].t - t0 + 30 * MS) / bar);
            st = (int)((log_[i].t - t0 + 30 * MS) % bar / (bar / 16));
            if (b < 8)
                out[b] |= (uint16_t)(1u << st);
        }
}
static void test_zaps_035(void)
{
    uint16_t z[8];
    int b, bad;
    zap_steps(GS_ELECTRO, E.g.lane_note[L_SIM1], z);
    for (b = 0, bad = 0; b < 8; b++)
        bad += z[b] != (b % 4 == 3 ? (1u << 2 | 1u << 10 | 1u << 12 | 1u << 14 | 1u << 15) : 1u << 14);
    CHECK(!bad, "electro zaps: %04x %04x %04x %04x", z[0], z[1], z[2], z[3]);
    zap_steps(GS_ITALO, E.g.lane_note[L_SIM1], z);
    for (b = 0, bad = 0; b < 8; b++)
        bad += (z[b] != 0) != (b % 2 == 1);
    CHECK(!bad, "italo zaps in bars 2 and 4 only: %04x %04x %04x %04x", z[0], z[1], z[2], z[3]);
    zap_steps(GS_GATED, E.g.lane_note[L_SIM2], z);
    for (b = 0, bad = 0; b < 8; b++)
        bad += (z[b] != 0) != (b % 4 == 3);
    CHECK(!bad, "gated noise in every 4th bar only: %04x %04x %04x %04x", z[0], z[1], z[2], z[3]);
    printf("0.3.5: zap rules ok\n");
}

/* 0.3.6: LA beats' pump (CC 11 on the chord channel dips on each kick, back to 127 by the next 8th) and harp run
 * (the arp's eight 32nds up two octaves on the phrase's first beat only); neither in another style */
static void brk_run(int style)
{
    fresh();
    eng_set(F_TEMPO, 120, vnow);
    eng_set(F_STYLE, style, vnow);
    eng_set(F_FILL_EVERY, 1, vnow);                       /* (no fills) */
    eng_set(F_CLOCK, CLK_INT_OUT, vnow);
    eng_set(F_PART(GP_DRUMS, PF_ON), 1, vnow);
    eng_set(F_PART(GP_CHORD, PF_ON), 1, vnow);
    eng_set(F_PART(GP_ARP, PF_ON), 1, vnow);
    eng_set(F_PART(GP_DRUMS, PF_CHAN), 9, vnow);         /* (each on its own channel) */
    eng_set(F_PART(GP_CHORD, PF_CHAN), 1, vnow);
    eng_set(F_PART(GP_ARP, PF_CHAN), 2, vnow);
    eng_play(1, vnow);
    run_internal(17000);
    eng_play(0, vnow);
}
static void test_brk_036(void)
{
    int i, j, b, kicks = 0, lows = 0, near = 0, back = 0, cc11 = 0, bad = 0, run[8], last[8];
    int cch, ach, kn = E.g.lane_note[L_KICK], kch;
    int64_t t0 = -1, bar = 2000 * MS, t32 = 2000 * MS / 32;
    brk_run(GS_LABEATS);
    cch = E.g.part[GP_CHORD].chan, ach = E.g.part[GP_ARP].chan, kch = E.g.part[GP_DRUMS].chan;
    for (i = 0; i < nlog; i++)
        if (log_[i].n == 3 && (log_[i].m[0] & 0xF0) == 0x90 && log_[i].m[2] && t0 < 0)
            t0 = log_[i].t;
    for (i = 0; i < nlog; i++) {
        const msg_t *m = &log_[i];
        if (m->n != 3)
            continue;
        if ((m->m[0] & 0xF0) == 0x90 && m->m[2] && m->m[1] == kn && (m->m[0] & 15) == kch && m->t - t0 < 8 * bar)
            kicks++;
        if ((m->m[0] & 0xF0) == 0xB0 && m->m[1] == 11) {
            CHECK((m->m[0] & 15) == cch, "pump CC 11 on channel %d, not the chord part's %d", m->m[0] & 15, cch);
            if (m->m[2] != 40 || m->t - t0 >= 8 * bar)
                continue;
            lows++;
            for (j = 0; j < nlog; j++)                   /* a kick at the same time */
                if (log_[j].n == 3 && (log_[j].m[0] & 0xF0) == 0x90 && log_[j].m[2] && log_[j].m[1] == kn &&
                    (log_[j].m[0] & 15) == kch && llabs(log_[j].t - m->t) <= 2 * MS) {
                    near++;
                    break;
                }
            for (j = i + 1; j < nlog; j++)               /* and back up by the next 8th */
                if (log_[j].n == 3 && log_[j].m[0] == m->m[0] && log_[j].m[1] == 11) {
                    if (log_[j].m[2] == 127 && log_[j].t - m->t <= 2000 * MS / 8 + 2 * MS)
                        back++;
                    if (log_[j].m[2] == 127 || log_[j].m[2] == 40)
                        break;
                }
        }
    }
    CHECK(kicks > 16 && lows == kicks && near == lows && back == lows,
          "pump: %d kicks, %d dips, %d with a kick, %d back to 127 in time", kicks, lows, near, back);
    for (b = 0; b < 8; b++)
        run[b] = 0, last[b] = -1;
    for (i = 0; i < nlog; i++) {                         /* the arp's notes in beat 1 of each bar, 32nds apart, rising */
        const msg_t *m = &log_[i];
        int64_t at;
        if (m->n != 3 || (m->m[0] & 0xF0) != 0x90 || !m->m[2] || (m->m[0] & 15) != ach)
            continue;
        at = m->t - t0 + 5 * MS;
        b = (int)(at / bar);
        if (b >= 8 || at % bar >= bar / 4)
            continue;
        if (b % 4 == 0 && ((last[b] >= 0 && m->m[1] <= last[b]) || llabs(at % bar - 5 * MS - run[b] * t32) > 3 * MS))
            bad++;
        last[b] = m->m[1];
        run[b]++;
    }
    CHECK(run[0] == 8 && run[4] == 8 && run[1] <= 4 && run[2] <= 4 && run[3] <= 4 && run[5] <= 4,
          "harp runs: %d %d %d %d %d %d notes in beat 1", run[0], run[1], run[2], run[3], run[4], run[5]);
    CHECK(!bad, "harp run: %d notes not 32nds apart or not rising", bad);
    nlog = 0;                                            /* leaving the pump: CC 11 back to 127 at once */
    eng_set(F_STYLE, GS_DILLA, vnow);
    CHECK(nlog == 1 && log_[0].m[0] == (0xB0 | cch) && log_[0].m[1] == 11 && log_[0].m[2] == 127,
          "leaving LA beats: %d messages", nlog);
    brk_run(GS_DILLA);
    for (i = 0; i < 8; i++)
        run[i] = 0;
    t0 = -1;
    for (i = 0; i < nlog; i++) {
        const msg_t *m = &log_[i];
        if (m->n != 3)
            continue;
        if ((m->m[0] & 0xF0) == 0x90 && m->m[2] && t0 < 0)
            t0 = m->t;
        cc11 += (m->m[0] & 0xF0) == 0xB0 && m->m[1] == 11;
        if ((m->m[0] & 0xF0) == 0x90 && m->m[2] && (m->m[0] & 15) == ach && t0 >= 0 && m->t - t0 + 5 * MS < 8 * bar &&
            (m->t - t0 + 5 * MS) % bar < bar / 4)
            run[(m->t - t0 + 5 * MS) / bar]++;
    }
    for (b = 0, bad = 0; b < 8; b++)
        bad += run[b] > 4;                               /* (an arp step and its ratchet: no run) */
    CHECK(!cc11 && !bad, "Dilla: %d CC 11, %d bars with a run", cc11, bad);
    printf("0.3.6: pump and harp run ok\n");
}

/* 0.3.7: the HSE filter sweep: the chord part's cutoff over 16 bars falls to its low at the phrase boundary and
 * climbs to the middle; none in another style; restored when the style is left */
static int hse_cc[20];
static int hse_pocket;
static void hse_run(int style, int cc, int ch)
{
    int i, b;
    int64_t t0 = -1, bar = 2000 * MS;
    fresh();
    eng_set(F_TEMPO, 120, vnow);
    eng_set(F_STYLE, style, vnow);
    eng_set(F_CLOCK, CLK_INT_OUT, vnow);
    eng_set(F_PART(GP_DRUMS, PF_ON), 1, vnow);
    eng_set(F_PART(GP_CHORD, PF_ON), 1, vnow);
    eng_set(F_PART(GP_CHORD, PF_CHAN), ch, vnow);
    eng_set(F_POCKET, hse_pocket, vnow);
    eng_play(1, vnow);
    run_internal(33000);
    eng_play(0, vnow);
    for (i = 0; i < 20; i++)
        hse_cc[i] = -1;
    for (i = 0; i < nlog; i++)
        if (log_[i].n == 3 && (log_[i].m[0] & 0xF0) == 0x90 && log_[i].m[2] && t0 < 0)
            t0 = log_[i].t;
    for (i = 0; i < nlog; i++) {                         /* the first CC of each bar (on its downbeat) */
        const msg_t *m = &log_[i];
        if (m->n != 3 || m->m[0] != (0xB0 | ch) || m->m[1] != cc || t0 < 0)
            continue;
        b = (int)((m->t - t0 + 20 * MS) / bar);
        if (b < 20 && hse_cc[b] < 0 && (m->t - t0 + 20 * MS) % bar < 40 * MS)
            hse_cc[b] = m->m[2];
    }
}
static void test_hse_037(void)
{
    int rest, cc = gen_sweep_cc(&rest), b, ok = 1, any = 0;
    hse_run(GS_FRENCH, cc, 1);
    for (b = 0; b < 16; b++)
        ok &= hse_cc[b] >= 0;
    CHECK(ok, "sweep: a CC %d on every downbeat (bar 0 %d, 8 %d, 15 %d)", cc, hse_cc[0], hse_cc[8], hse_cc[15]);
    for (b = 1; b <= 8; b++)
        ok &= hse_cc[b] > hse_cc[b - 1];
    for (b = 9; b < 16; b++)
        ok &= hse_cc[b] < hse_cc[b - 1];
    CHECK(ok && hse_cc[0] < 40 && hse_cc[8] > 100 && hse_cc[16] == hse_cc[0],
          "sweep shape: %d %d %d %d %d", hse_cc[0], hse_cc[4], hse_cc[8], hse_cc[12], hse_cc[16]);
    nlog = 0;
    eng_set(F_STYLE, GS_DILLA, vnow);
    CHECK(nlog == 1 && log_[0].m[0] == 0xB1 && log_[0].m[1] == cc && log_[0].m[2] == rest,
          "leaving French touch: %d messages", nlog);
    hse_run(GS_DUBTECH, cc, 1);
    CHECK(hse_cc[0] >= 0 && hse_cc[8] > hse_cc[0] && hse_cc[8] < 100, "Dub techno: %d %d", hse_cc[0], hse_cc[8]);
    for (hse_pocket = 1, b = 0; b < 2; b++) {           /* with the pocket on too: notes and the sweep */
        int notes = 0, i;
        hse_run(b ? GS_DUBTECH : GS_FRENCH, cc, 1);
        for (i = 0; i < nlog; i++)
            notes += log_[i].n == 3 && (log_[i].m[0] & 0xF0) == 0x90 && log_[i].m[2];
        CHECK(notes > 200 && hse_cc[0] >= 0 && hse_cc[8] > hse_cc[0], "pocket on, style %d: %d notes", b, notes);
    }
    hse_pocket = 0;
    hse_run(GS_DILLA, cc, 1);
    for (b = 0; b < 16; b++)
        any += hse_cc[b] >= 0 && b > 0 && hse_cc[b] != hse_cc[b - 1];
    CHECK(any < 4, "Dilla: %d bars with a moving cutoff on the downbeat", any);
    printf("0.3.7: filter sweep ok\n");
}

/* 0.3.8: Jungle's break edits (bars after the phrase's first differ from the grid's bar, the first is left as it
 * is) and Dubstep's wobble (CC on the bass channel only in Dubstep; restored when the style is left) */
static void bas_run(int style)
{
    fresh();
    eng_set(F_TEMPO, 120, vnow);
    eng_set(F_STYLE, style, vnow);
    eng_set(F_FILL_EVERY, 1, vnow);                       /* (no fills) */
    eng_set(F_CLOCK, CLK_INT_OUT, vnow);
    eng_set(F_PART(GP_DRUMS, PF_ON), 1, vnow);
    eng_set(F_PART(GP_DRUMS, PF_CHAN), 9, vnow);
    eng_set(F_PART(GP_BASS, PF_ON), 1, vnow);
    eng_set(F_PART(GP_BASS, PF_CHAN), 3, vnow);
    eng_play(1, vnow);
    run_internal(17000);
    eng_play(0, vnow);
}
static uint32_t bas_bar_sig(int b, int64_t t0, int ch)  /* the bar's drum notes: which, where (16ths), as a hash */
{
    uint32_t h = 2166136261u;
    int i;
    for (i = 0; i < nlog; i++) {
        const msg_t *m = &log_[i];
        int64_t at = m->t - t0 + 5 * MS;
        if (m->n == 3 && m->m[0] == (0x90 | ch) && m->m[2] && at >= b * 2000 * MS && at < (b + 1) * 2000 * MS)
            h = (h ^ (uint32_t)(m->m[1] * 64 + (int)((at % (2000 * MS)) / (125 * MS)))) * 16777619u;
    }
    return h;
}
static void test_bas_038(void)
{
    int i, b, edited = 0, wob = 0, other = 0, rest, cc = gen_wobble_cc(&rest);
    int64_t t0 = -1;
    uint32_t first;
    bas_run(GS_JUNGLE);
    for (i = 0; i < nlog; i++)
        if (log_[i].n == 3 && (log_[i].m[0] & 0xF0) == 0x90 && log_[i].m[2] && t0 < 0)
            t0 = log_[i].t;
    first = bas_bar_sig(0, t0, 9);
    CHECK(first == bas_bar_sig(4, t0, 9), "Jungle: the phrase's first bars (1 and 5) differ: edited");
    for (b = 1; b < 8; b++)
        if (b != 4)
            edited += bas_bar_sig(b, t0, 9) != first;
    CHECK(edited == 6, "Jungle: %d of the 6 other bars edited", edited);
    for (i = 0; i < nlog; i++)
        other += log_[i].n == 3 && (log_[i].m[0] & 0xF0) == 0xB0 && log_[i].m[1] == cc && (log_[i].m[0] & 15) == 3;
    CHECK(!other, "Jungle: %d wobble CCs", other);
    bas_run(GS_DUBSTEP);
    for (i = 0; i < nlog; i++)
        wob += log_[i].n == 3 && log_[i].m[0] == (0xB0 | 3) && log_[i].m[1] == cc;
    CHECK(wob >= 8 * 40, "Dubstep: %d wobble CCs on the bass channel in 8 bars", wob);
    nlog = 0;
    eng_set(F_STYLE, GS_DILLA, vnow);
    CHECK(nlog == 1 && log_[0].m[0] == (0xB0 | 3) && log_[0].m[1] == cc && log_[0].m[2] == rest,
          "leaving Dubstep: %d messages", nlog);
    bas_run(GS_UKG);
    for (i = 0, other = 0; i < nlog; i++)
        other += log_[i].n == 3 && log_[i].m[0] == (0xB0 | 3) && log_[i].m[1] == cc;
    CHECK(other < 40, "UK garage: %d CCs on the bass's cutoff", other);
    printf("0.3.8: break edits and wobble ok\n");
}

/* 0.3.9: Braindance's polymeter: the closed hat, tom and claves each repeat at a length of their own (hat 10-14,
 * tom 9-11, claves 5-7) inside the phrase, and the second phrase plays the first again (all lanes line up) */
static int idm_period(const uint8_t *hit, int lo, int hi)
{
    int L, i, ok;
    for (L = lo; L <= hi; L++) {
        for (i = 0, ok = 1; i + L < 64; i++)
            ok &= hit[i] == hit[i + L];
        if (ok)
            return L;
    }
    return 0;
}
static void test_idm_039(void)
{
    static const int NOTE[3] = {43, 40, 46}, LO[3] = {10, 9, 5}, HI[3] = {14, 11, 7};
    uint8_t hit[3][128];
    int i, k, per[3], same = 1, cnt[3] = {0, 0, 0};
    int64_t t0 = -1;
    fresh();
    eng_set(F_TEMPO, 120, vnow);
    eng_set(F_STYLE, GS_REPHLEX, vnow);
    eng_set(F_FILL_EVERY, 1, vnow);                       /* (no fills) */
    eng_set(F_CLOCK, CLK_INT_OUT, vnow);
    eng_set(F_PART(GP_DRUMS, PF_ON), 1, vnow);
    eng_set(F_PART(GP_DRUMS, PF_CHAN), 9, vnow);
    eng_play(1, vnow);
    run_internal(17000);
    eng_play(0, vnow);
    memset(hit, 0, sizeof hit);
    for (i = 0; i < nlog; i++)
        if (log_[i].n == 3 && log_[i].m[0] == 0x99 && log_[i].m[2] && t0 < 0)
            t0 = log_[i].t;
    for (i = 0; i < nlog; i++) {
        const msg_t *m = &log_[i];
        int st;
        if (m->n != 3 || m->m[0] != 0x99 || !m->m[2])
            continue;
        st = (int)((m->t - t0 + 62 * MS) / (125 * MS));
        for (k = 0; k < 3; k++)
            if (m->m[1] == NOTE[k] && st < 128)
                hit[k][st] = 1, cnt[k]++;
    }
    for (k = 0; k < 3; k++) {
        per[k] = idm_period(hit[k], LO[k], HI[k]);
        for (i = 0; i < 64; i++)
            same &= hit[k][i] == hit[k][i + 64];
    }
    CHECK(per[0] && per[1] && per[2], "polymeter: lengths hat %d, tom %d, claves %d (hits %d %d %d)", per[0], per[1],
          per[2], cnt[0], cnt[1], cnt[2]);
    CHECK(same, "polymeter: the second phrase differs from the first");
    CHECK(cnt[0] > 32 && cnt[1] > 16 && cnt[2] > 32, "polymeter: hits %d %d %d", cnt[0], cnt[1], cnt[2]);
    CHECK(per[0] != 16 && per[1] != 16, "polymeter: lanes loop at 16");
    printf("0.3.9: polymeter ok (hat %d, tom %d, claves %d)\n", per[0], per[1], per[2]);
}

/* 0.4.0: the WLD batch plays (drums, chords and bass in every new style, the tempo range and family set) and
 * carries none of the bar effects (no CC 11, no cutoff moves beyond the parts' own) */
static void test_wld_040(void)
{
    static const int ST[5] = {GS_DANCEHALL, GS_REGGAETON, GS_REGGAETON2, GS_AFROBEAT, GS_BAILE};
    int k, i;
    for (k = 0; k < 5; k++) {
        int dr = 0, ch = 0, bs = 0, cc11 = 0, lo, hi;
        gen_style_bpm(ST[k], &lo, &hi);
        CHECK(gen_style_cat(ST[k]) == CAT_WLD && lo >= 85 && hi <= 135 && lo < hi && gen_style_tanz(ST[k]),
              "WLD style %d: family %d, %d-%d", ST[k], gen_style_cat(ST[k]), lo, hi);
        CHECK(!gen_style_pump(ST[k]) && !gen_style_sweep(ST[k]) && !gen_style_wobble(ST[k]), "WLD style %d: effects", ST[k]);
        fresh();
        eng_set(F_TEMPO, 100, vnow);
        eng_set(F_STYLE, ST[k], vnow);
        eng_set(F_CLOCK, CLK_INT_OUT, vnow);
        eng_set(F_PART(GP_DRUMS, PF_ON), 1, vnow);
        eng_set(F_PART(GP_DRUMS, PF_CHAN), 9, vnow);
        eng_set(F_PART(GP_CHORD, PF_ON), 1, vnow);
        eng_set(F_PART(GP_CHORD, PF_CHAN), 1, vnow);
        eng_set(F_PART(GP_BASS, PF_ON), 1, vnow);
        eng_set(F_PART(GP_BASS, PF_CHAN), 3, vnow);
        eng_play(1, vnow);
        run_internal(10000);
        eng_play(0, vnow);
        for (i = 0; i < nlog; i++) {
            const msg_t *m = &log_[i];
            if (m->n != 3)
                continue;
            dr += m->m[0] == 0x99 && m->m[2];
            ch += m->m[0] == 0x91 && m->m[2];
            bs += m->m[0] == 0x93 && m->m[2];
            cc11 += (m->m[0] & 0xF0) == 0xB0 && m->m[1] == 11;
        }
        CHECK(dr > 40 && ch > 4 && bs > 4 && !cc11, "WLD style %d: drums %d, chords %d, bass %d, CC 11 %d", ST[k], dr, ch,
              bs, cc11);
    }
    printf("0.4.0: WLD styles ok\n");
}

int main(void)
{
    test_ranges();
    test_internal();
    test_clockless();
    test_external();
    test_parts_fit();
    test_channels_follow();
    test_arp_seq();
    test_bass_range();
    test_new_controls();
    test_stuck_notes();
    test_review_fixes();
    test_styles_030();
    test_qol_031();
    test_bell_032();
    test_bell_lines_033();
    test_pocket_034();
    test_zaps_035();
    test_brk_036();
    test_hse_037();
    test_bas_038();
    test_idm_039();
    test_wld_040();
    test_panic();
    printf(fails ? "FAILED (%d)\n" : "ok\n", fails);
    free(log_);
    return fails != 0;
}
