/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The generators alone: every style, every part, checked for stuck notes, ranges and one voice at a time; with
 * "dump" the events as text (bar pos part status d1 d2) for a closer look. */
#define GEN_STANDALONE
#include <stdio.h>
#include <stdlib.h>
#include "../../app/gen.c"

static gen_cfg_t cfg;
static int dump, fails;
static uint32_t now;
static int held[16][128], nheld[16], maxheld[16];
static long notes[16], ccs[16], bends[16];
static void out(uint32_t st, uint32_t d1, uint32_t d2)
{
    int ch = st & 15, k = st & 0xF0;
    if (dump)
        printf("%u %u %02X %u %u\n", now / GEN_BAR, now % GEN_BAR, st, d1, d2);
    if (k == 0x90) {
        if (held[ch][d1]) { printf("FAIL double note-on ch %d note %u\n", ch, d1); fails++; }
        held[ch][d1] = 1; nheld[ch]++; notes[ch]++;
        if (nheld[ch] > maxheld[ch]) maxheld[ch] = nheld[ch];
    } else if (k == 0x80) {
        if (!held[ch][d1]) { printf("FAIL note-off without on ch %d note %u\n", ch, d1); fails++; }
        held[ch][d1] = 0; nheld[ch]--;
    } else if (k == 0xB0) ccs[ch]++;
    else if (k == 0xE0) bends[ch]++;
}
int main(int argc, char **argv)
{
    int s, p, bars = 16, only = argc > 2 ? atoi(argv[2]) : -1, minor, gm = argc > 3, gmminor = argc > 3 ? atoi(argv[3]) : 0;
    dump = argc > 1 && !strcmp(argv[1], "dump");
    for (s = 0; s < GEN_NSTYLES; s++)
        for (minor = 0; minor < 2; minor++) {
            uint32_t t;
            if ((only >= 0 && s != only) || (gm && minor != gmminor)) continue;
            gen_defaults(&cfg);
            if (!gen_valid(&cfg)) { printf("FAIL defaults invalid\n"); fails++; }
            cfg.style = s; cfg.minor = minor; cfg.seed = 7 + s; cfg.mutate = minor ? 3 : 0;
            for (p = 0; p < GEN_NPARTS; p++) { cfg.part[p].on = 1; cfg.part[p].chan = p; cfg.part[p].flair = 90; }
            cfg.sim_chan = 8; cfg.dset = DS_ALL; cfg.swing = minor ? 12 : 0;
            if (gm) {                    /* a General MIDI layout, to listen to: bass 1, chords 2, arp 3, lead 4, drums 10 */
                static const uint8_t CH[GEN_NPARTS] = {9, 1, 2, 0, 3, 0, 4};
                for (p = 0; p < GEN_NPARTS; p++) { cfg.part[p].chan = CH[p]; cfg.part[p].dev = DV_GENERIC; cfg.part[p].flair = 0; }
                cfg.part[GP_BASS].dev = DV_MB33; cfg.part[GP_BASS].flair = 60; cfg.part[GP_LEAD].flair = 50; cfg.part[GP_DRUMS].flair = 60;
                cfg.part[GP_KICK].on = 0; cfg.part[GP_RAND].on = 0; cfg.sim_chan = 0; cfg.mutate = 2; cfg.dset = DS_ALL;
                cfg.part[GP_ARP].on = s == GS_DETROIT || s == GS_ELECTRO || s == GS_BOOGIE || s == GS_ACIDDREAM;
                cfg.part[GP_ARP].dens = 127;
                cfg.lane_note[L_KICK] = 36; cfg.lane_note[L_SNARE] = 38; cfg.lane_note[L_RIM] = 37; cfg.lane_note[L_CLAP] = 39;
                cfg.lane_note[L_CHH] = 42; cfg.lane_note[L_OHH] = 46; cfg.lane_note[L_TOML] = 45; cfg.lane_note[L_TOMH] = 50;
                cfg.lane_note[L_CONGL] = 64; cfg.lane_note[L_CONGH] = 63; cfg.lane_note[L_COWB] = 56; cfg.lane_note[L_CLAVE] = 75;
                cfg.lane_note[L_CYM] = 49; cfg.lane_note[L_SIM1] = 50; cfg.lane_note[L_SIM2] = 40;
                cfg.seed = argc > 4 ? atoi(argv[4]) : 11 + s;
            }
            memset(held, 0, sizeof held); memset(nheld, 0, sizeof nheld); memset(maxheld, 0, sizeof maxheld);
            memset(notes, 0, sizeof notes); memset(ccs, 0, sizeof ccs); memset(bends, 0, sizeof bends);
            gen_init(&cfg, out);
            gen_service();
            gen_start();
            for (t = 0; t < bars * GEN_BAR; t++) { now = t; gen_service(); gen_subtick(); }
            gen_stop();
            for (p = 0; p < 16; p++)
                if (nheld[p]) { printf("FAIL %s: %d notes left on ch %d\n", gen_style_name(s), nheld[p], p); fails++; }
            if (maxheld[GP_BASS] > 2 || maxheld[GP_LEAD] > 2 || maxheld[GP_KICK] > 1) {
                printf("FAIL %s: mono parts overlap (bass %d lead %d kick %d)\n", gen_style_name(s), maxheld[GP_BASS], maxheld[GP_LEAD], maxheld[GP_KICK]); fails++; }
            if (!dump)
                printf("%-12s %s  drums %4ld  chords %4ld (max %d)  arp %4ld  bass %4ld cc %ld bend %ld  lead %4ld cc %ld bend %ld  kick %3ld  sim %ld\n",
                       gen_style_name(s), minor ? "min" : "maj", notes[0], notes[1], maxheld[1], notes[2], notes[3], ccs[3], bends[3],
                       notes[4], ccs[4], bends[4], notes[5], notes[7]);
        }
    if (!dump)
        printf(fails ? "FAILED (%d)\n" : "ok\n", fails);
    return fails != 0;
}
