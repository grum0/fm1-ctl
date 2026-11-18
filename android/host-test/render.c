/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Renders the generators through the Android engine on a computer, for two jobs:
 *
 *   golden     every style under every drum mode, all seven parts on, four bars at 120 BPM from a fixed seed; one
 *              fingerprint (FNV-1a over every message and its time) per pair, compared with golden.txt. A change
 *              to a style nobody meant to touch shows up here. After a change made on purpose:
 *              `render golden --update` rewrites golden.txt (say so in the commit).
 *   preview    one standard MIDI file per style (eight bars at the middle of the style's tempo range, drum mode
 *              Style, one track per part on Grumo's channels) into a directory, to audition on the MPC.
 *
 *   cc -DGEN_STANDALONE ... render.c && ./render golden [--update] | ./render preview DIR      (run.sh, CI) */
#define GEN_STANDALONE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../app/gen.c"
#include "../app/src/main/cpp/gen1_engine.c"

#define SEED 1234
/* the app's default instrument for each part (Store.kt DEFAULT_TARGET) and its channel and device */
static const struct { uint8_t ch, dev; const char *who; } PART_INST[GEN_NPARTS] = {
    [GP_DRUMS] = {4, DV_TANZBAR, "Tanzbar Lite"}, [GP_CHORD] = {13, DV_JV1010, "JV-1010"},
    [GP_ARP] = {2, DV_VOLCAFM, "Volca FM"},        [GP_BASS] = {12, DV_MB33, "MB-33"},
    [GP_LEAD] = {3, DV_MONOLOGUE, "Monologue"},    [GP_KICK] = {8, DV_MBASE, "MBase 11"},
    [GP_RAND] = {7, DV_JV1010, "JV-1010 part 2"},
};

typedef struct { int64_t t; uint8_t m[3]; uint8_t n; } ev_t;
static ev_t *evs;
static int nev, capev;
static int64_t vnow;

static void sink(void *ud, int64_t when, const uint8_t *m, int n)
{
    (void)ud;
    if (nev == capev) {
        capev = capev ? capev * 2 : 8192;
        evs = realloc(evs, (size_t)capev * sizeof *evs);
        if (!evs)
            exit(2);
    }
    evs[nev].t = when;
    evs[nev].n = (uint8_t)n;
    memset(evs[nev].m, 0, 3);
    memcpy(evs[nev].m, m, (size_t)n);
    nev++;
}

/* one run: the style, the drum mode, these parts on (bits), at bpm, for bars; the messages are in evs */
static int64_t run(int style, int dmode, unsigned parts, int bpm, int bars)
{
    int p, k = 0;
    int64_t t0, end;
    nev = 0;
    vnow = 1000 * MS;
    eng_init(sink, 0);
    eng_set(F_CLOCK, CLK_INT, vnow);
    eng_set(F_TEMPO, bpm, vnow);
    eng_set(F_STYLE, style, vnow);
    eng_set(F_SEED, SEED, vnow);
    eng_set(F_PART(GP_DRUMS, PF_MODE), dmode, vnow);
    for (p = 0; p < GEN_NPARTS; p++) {
        eng_set(F_PART(p, PF_CHAN), PART_INST[p].ch - 1, vnow);
        eng_set(F_PART(p, PF_DEV), PART_INST[p].dev, vnow);
        eng_set(F_PART(p, PF_ON), (int)((parts >> p) & 1u), vnow);
    }
    nev = 0;
    t0 = vnow;
    eng_play(1, vnow);
    end = vnow + (int64_t)bars * 4 * 60000 * MS / bpm;
    while (vnow < end) {                       /* the internal clock's thread, as in the engine test */
        eng_pump(vnow);
        vnow += (++k % 50 == 0 ? 17 : 4) * MS;
    }
    eng_play(0, vnow);
    return t0;
}

static uint64_t fnv(uint64_t h, const void *p, size_t n)
{
    const uint8_t *b = p;
    while (n--)
        h = (h ^ *b++) * 1099511628211ull;
    return h;
}

static int golden(int update)
{
    const char *path = "golden.txt";
    int s, m, nm = gen_nmodes(GP_DRUMS), bad = 0, lines = 0;
    FILE *f;
    char want[256];
    char (*got)[96] = calloc((size_t)(GEN_NSTYLES * nm), sizeof *got);
    if (!got)
        return 2;
    for (s = 0; s < GEN_NSTYLES; s++)
        for (m = 0; m < nm; m++) {
            int i;
            uint64_t h = 14695981039346656037ull;
            int64_t t0 = run(s, m, (1u << GEN_NPARTS) - 1u, 120, 4);
            for (i = 0; i < nev; i++) {
                int64_t dt = evs[i].t - t0;
                h = fnv(h, &dt, sizeof dt);
                h = fnv(h, evs[i].m, evs[i].n);
            }
            snprintf(got[s * nm + m], sizeof got[0], "%d %d %016llx %d", s, m, (unsigned long long)h, nev);
        }
    if (update) {
        f = fopen(path, "w");
        if (!f)
            return 2;
        fprintf(f, "# GEN-1 golden output: style, drum mode, fingerprint, messages (render.c; seed %d, 4 bars at 120)\n", SEED);
        for (s = 0; s < GEN_NSTYLES * nm; s++)
            fprintf(f, "%s\n", got[s]);
        fclose(f);
        printf("golden: wrote %d fingerprints to %s\n", GEN_NSTYLES * nm, path);
        free(got);
        return 0;
    }
    f = fopen(path, "r");
    if (!f) {
        printf("golden: %s missing (run with --update)\n", path);
        free(got);
        return 1;
    }
    while (fgets(want, sizeof want, f)) {
        int ws, wm;
        if (want[0] == '#')
            continue;
        want[strcspn(want, "\n")] = 0;
        if (sscanf(want, "%d %d", &ws, &wm) != 2 || ws < 0 || ws >= GEN_NSTYLES || wm < 0 || wm >= nm) {
            printf("golden: a line for a style or mode that no longer exists: %s\n", want);
            bad++;
            continue;
        }
        lines++;
        if (strcmp(want, got[ws * nm + wm])) {
            printf("golden: %s, drum mode %s changed (was %s, now %s)\n", gen_style_name(ws), gen_mode_name(GP_DRUMS, wm), want,
                   got[ws * nm + wm]);
            bad++;
        }
    }
    fclose(f);
    if (lines < GEN_NSTYLES * nm)
        printf("golden: %d style/mode pairs have no fingerprint yet (new styles: run with --update)\n", GEN_NSTYLES * nm - lines);
    free(got);
    if (bad) {
        printf("golden: %d differences. If the change is meant, run `render golden --update` and say so in the commit.\n", bad);
        return 1;
    }
    printf("golden: %d style/mode pairs unchanged\n", lines);
    return 0;
}

/* --------------------------------------------------------- standard MIDI files */
typedef struct { uint8_t *b; size_t n, cap; } buf_t;
static void bput(buf_t *b, const void *p, size_t n)
{
    if (b->n + n > b->cap) {
        b->cap = (b->n + n) * 2 + 256;
        b->b = realloc(b->b, b->cap);
        if (!b->b)
            exit(2);
    }
    memcpy(b->b + b->n, p, n);
    b->n += n;
}
static void put1(buf_t *b, unsigned v) { uint8_t c = (uint8_t)v; bput(b, &c, 1); }
static void putvar(buf_t *b, uint32_t v)
{
    uint8_t t[5];
    int n = 0;
    t[n++] = v & 0x7F;
    while (v >>= 7)
        t[n++] = (uint8_t)(0x80 | (v & 0x7F));
    while (n--)
        put1(b, t[n]);
}
static void put_be(buf_t *b, uint32_t v, int n)
{
    while (n--)
        put1(b, (v >> (8 * n)) & 0xFF);
}
static void meta_text(buf_t *b, int type, const char *s)
{
    putvar(b, 0);
    put1(b, 0xFF);
    put1(b, (unsigned)type);
    putvar(b, (uint32_t)strlen(s));
    bput(b, s, strlen(s));
}
static void track(FILE *f, buf_t *t)
{
    putvar(t, 0);
    bput(t, "\xFF\x2F\x00", 3);
    fwrite("MTrk", 1, 4, f);
    {
        buf_t h = {0};
        put_be(&h, (uint32_t)t->n, 4);
        fwrite(h.b, 1, h.n, f);
        free(h.b);
    }
    fwrite(t->b, 1, t->n, f);
    t->n = 0;
}

#define PPQ 96
static int preview(const char *dir)
{
    int s, p, nfiles = 0;
    buf_t t = {0};
    for (s = 0; s < GEN_NSTYLES; s++) {
        char path[512], name[96], *q;
        int lo, hi, bpm, i;
        FILE *f;
        gen_style_bpm(s, &lo, &hi);
        bpm = (lo + hi) / 2;
        snprintf(name, sizeof name, "%s %s", gen_cat_code(gen_style_cat(s)), gen_style_name(s));
        for (q = name; *q; q++)
            if (!((*q >= 'a' && *q <= 'z') || (*q >= 'A' && *q <= 'Z') || (*q >= '0' && *q <= '9')))
                *q = '_';
        snprintf(path, sizeof path, "%s/%02d_%s_%dbpm.mid", dir, s, name, bpm);
        f = fopen(path, "wb");
        if (!f) {
            printf("preview: cannot write %s\n", path);
            return 1;
        }
        fwrite("MThd\0\0\0\6\0\1", 1, 10, f);
        {
            uint8_t h[4] = {0, (uint8_t)(GEN_NPARTS + 1), 0, PPQ};
            fwrite(h, 1, 4, f);
        }
        /* the tempo track */
        meta_text(&t, 0x03, gen_style_name(s));
        putvar(&t, 0);
        bput(&t, "\xFF\x51\x03", 3);
        put_be(&t, (uint32_t)(60000000 / bpm), 3);
        putvar(&t, 0);
        bput(&t, "\xFF\x58\x04\x04\x02\x18\x08", 7);
        track(f, &t);
        /* a track for each part, rendered alone (the engine test checks that a part alone plays as in the band) */
        for (p = 0; p < GEN_NPARTS; p++) {
            char tn[96];
            uint32_t last = 0;
            int64_t t0 = run(s, 0, 1u << p, bpm, 8);
            snprintf(tn, sizeof tn, "%s (%s, ch %d)", gen_part_name(p), PART_INST[p].who, PART_INST[p].ch);
            meta_text(&t, 0x03, tn);
            for (i = 0; i < nev; i++) {
                int64_t dt = evs[i].t - t0;
                uint32_t tick;
                if (evs[i].n != 3 || (evs[i].m[0] & 0xF0) == 0xF0)
                    continue;
                if (dt < 0)
                    dt = 0;
                tick = (uint32_t)((dt * bpm * PPQ + 30000000000LL) / 60000000000LL);
                if (tick < last)
                    tick = last;
                putvar(&t, tick - last);
                bput(&t, evs[i].m, 3);
                last = tick;
            }
            track(f, &t);
        }
        fclose(f);
        nfiles++;
    }
    free(t.b);
    printf("preview: %d files in %s\n", nfiles, dir);
    return 0;
}

int main(int argc, char **argv)
{
    int r = 1;
    if (argc >= 2 && !strcmp(argv[1], "golden"))
        r = golden(argc >= 3 && !strcmp(argv[2], "--update"));
    else if (argc >= 4 && !strcmp(argv[1], "dump")) {   /* every message of one style/mode pair, for a diff */
        int i;
        int64_t t0 = run(atoi(argv[2]), atoi(argv[3]), (1u << GEN_NPARTS) - 1u, 120, 4);
        for (i = 0; i < nev; i++)
            printf("%lld %02x %02x %02x\n", (long long)(evs[i].t - t0), evs[i].m[0], evs[i].m[1], evs[i].m[2]);
        r = 0;
    } else if (argc >= 3 && !strcmp(argv[1], "preview"))
        r = preview(argv[2]);
    else
        printf("usage: render golden [--update] | render preview DIR | render dump STYLE MODE\n");
    free(evs);
    return r;
}
