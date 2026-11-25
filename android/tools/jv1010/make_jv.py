#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The Roland JV-1010's part suggestions: perf.tsv (the performance "GEN-1 Keys" in USER:01) and hints.tsv (a JV
part for each style's chords, Marble, arp and lead) -> app/src/main/java/com/grum0/gen1/JvData.kt (the screen's
hints), app/src/main/cpp/jv_data.h (the same table for the portable core, from 0.5.0) and jv1010_user01.syx, so the app's hints and the JV's memory always come from the same table. Run after editing
either; commit the outputs. `--check` exits 1 if either is stale. Styles are matched to app/gen.h's style enum by
name, and a style without a hints.tsv row is refused.

The SysEx is the chat builder's (claude/gen1-jv1010-make_perf.py in the project), byte for byte: 17 Roland DT1
messages, 630 bytes. Address map and parameter layout from the JV-1010 Owner's Manual, MIDI Implementation:
  User Performance USER:01 = 10 00 00 00
  Performance Common       = +00 00 (0x42 bytes)
  Performance Part n       = +(0x10 + n-1) 00 (0x19 bytes)
Roland DT1: F0 41 <dev> 6A 12 aa aa aa aa data.. sum F7, device ID 17 = 0x10.
Load: put the JV in Performance mode and send the file to its MIDI IN (SysEx Librarian, MIDI-OX); recall it with
Bank Select 80/0 and Program Change 1 on the Performance Control Channel."""
import os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..', '..', '..')
KT = os.path.join(HERE, '..', '..', 'app', 'src', 'main', 'java', 'com', 'grum0', 'gen1', 'JvData.kt')
HC = os.path.join(HERE, '..', '..', 'app', 'src', 'main', 'cpp', 'jv_data.h')
SYX = os.path.join(HERE, 'jv1010_user01.syx')

DEV = 0x10
MODEL = 0x6A
BASE = (0x10, 0x00)                                  # USER:01
NAME = "GEN-1 Keys"
SOURCES = {'exp8': (2, 8),                           # SR-JV80-08 in slot XP-B: group type 2, group ID 8 (assumed)
           'prA': (0, 3), 'prB': (0, 4), 'prC': (0, 5), 'prD': (0, 6), 'prE': (0, 7)}
OUTPUTS = {'MIX': 0, 'EFX': 1}
EFX_SOURCE_PART = 6                                  # the performance's EFX from part 6's patch (the B3's rotary)
TEMPO = 120
HINT_COLS = ['CHORDS', 'MARBLE', 'ARP', 'LEAD']


def rows(name):
    for n, line in enumerate(open(os.path.join(HERE, name), encoding='utf-8'), 1):
        line = line.rstrip('\n')
        if line.strip() and not line.startswith('#'):
            yield n, [c for c in line.split('\t') if not c.startswith('#')]


def enum_styles():
    h = open(os.path.join(ROOT, 'app', 'gen.h'), encoding='utf-8').read()
    body = h[h.index('enum { GS_'):]
    body = body[:body.index('GEN_NSTYLES')]
    return re.findall(r'GS_[A-Z0-9_]+', body)


def dt1(addr, data):
    body = list(addr) + list(data)
    assert all(0 <= b <= 0x7F for b in body)
    cs = (128 - sum(body) % 128) % 128
    return bytes([0xF0, 0x41, DEV, MODEL, 0x12] + body + [cs, 0xF7])


def common():
    d = [ord(c) for c in NAME.ljust(12)[:12]]
    efx_src = EFX_SOURCE_PART if EFX_SOURCE_PART <= 9 else EFX_SOURCE_PART - 1
    d += [efx_src, 0]                                # EFX source, EFX type (ignored when the source is a part)
    d += [0, 15, 0, 15, 8, 0, 15, 8, 0, 15, 127, 0]  # flat EQ
    d += [0, 127, 0, 0]                              # output MIX, mix out, chorus send, reverb send
    d += [0, 63, 0, 63]                              # EFX control sources off, depth 0
    d += [80, 8, 40, 0, 0, 0]                        # chorus level, rate, depth, pre-delay, feedback, output MIX
    d += [4, 80, 70, 16, 0]                          # reverb Hall 1, level, time, HF damp 8 kHz, delay feedback
    d += [TEMPO >> 4, TEMPO & 0x0F]
    d += [0]                                         # keyboard range switch off
    d += [0] * 16                                    # voice reserve
    d += [0, 0]                                      # keyboard mode, clock source
    assert len(d) == 0x42, len(d)
    return dt1((BASE[0], BASE[1], 0x00, 0x00), d)


def part(n, src, num, lvl, pan, cho, rev, out):
    gtype, gid = src
    v = num - 1
    d = [1, n - 1, gtype, gid, v >> 4, v & 0x0F, lvl, pan, 48, 50, out,
         127, cho, rev, 1, 1, 1, 0, 127, 3, 1, 1, 0, 0x08, 0x00]
    assert len(d) == 0x19
    return dt1((BASE[0], BASE[1], 0x10 + n - 1, 0x00), d)


def part10_off():                                    # the rhythm part: receive off, channel 10, nothing else
    return dt1((BASE[0], BASE[1], 0x19, 0x00), [0, 9])


def kt_str(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"').replace('$', '\\$') + '"'


def main():
    check = '--check' in sys.argv[1:]
    parts = {}
    for n, r in rows('perf.tsv'):
        if len(r) < 9:
            sys.exit(f'perf.tsv line {n}: 9 columns expected')
        p = int(r[0])
        if not 1 <= p <= 16 or p == 10 or p in parts:
            sys.exit(f'perf.tsv line {n}: part {p} is not 1-16 (10 is the rhythm part) or appears twice')
        if r[1] not in SOURCES or r[8] not in OUTPUTS:
            sys.exit(f'perf.tsv line {n}: unknown source {r[1]} or output {r[8]}')
        parts[p] = (SOURCES[r[1]], int(r[2]), r[3], int(r[4]), int(r[5]), int(r[6]), int(r[7]), OUTPUTS[r[8]])
    styles = enum_styles()
    hints = {}
    for n, r in rows('hints.tsv'):
        if len(r) < 5 or r[0] not in styles or r[0] in hints:
            sys.exit(f'hints.tsv line {n}: a style of app/gen.h (once) and four parts expected')
        v = [0 if x == '-' else int(x) for x in r[1:5]]
        for x in v:
            if x and x not in parts:
                sys.exit(f'hints.tsv line {n}: part {x} is not in perf.tsv')
        hints[r[0]] = v
    missing = [s for s in styles if s not in hints]
    if missing:
        sys.exit('hints.tsv has no row for ' + ', '.join(missing) + ' (every style needs its JV-1010 suggestion)')

    msgs = [common()] + [part(p, s[0], s[1], *s[3:]) for p, s in sorted(parts.items())] + [part10_off()]
    syx = b''.join(msgs)

    names = [''] * 17
    for p, s in parts.items():
        names[p] = s[2]
    out = ['// SPDX-License-Identifier: GPL-3.0-only',
           '// Generated by android/tools/jv1010/make_jv.py from perf.tsv and hints.tsv; do not edit.',
           'package com.grum0.gen1',
           '',
           '/** The JV-1010 performance "GEN-1 Keys" (USER:01) and a part of it for each style\'s chords, Marble, arp and',
           ' *  lead. Only suggestions: the app keeps sending each part on its own channel. */',
           'object JvData {',
           '    /** The performance\'s part names by part number (1-16; 10, the rhythm part, is off). */',
           '    val PART = arrayOf(' + ', '.join(kt_str(x) for x in names) + ')',
           '']
    for i, col in enumerate(HINT_COLS):
        vals = [str(hints[s][i]) for s in styles]
        out.append(f'    /** Per style, in the style enum\'s order: the JV part for the {col.lower()}, 0 = none. */')
        out.append(f'    val {col} = intArrayOf(')
        for k in range(0, len(vals), 20):
            out.append('        ' + ', '.join(vals[k:k + 20]) + ',')
        out.append('    )')
    out.append('}')
    kt = '\n'.join(out) + '\n'

    def c_str(x):
        return '"' + x.replace('\\', '\\\\').replace('"', '\\"') + '"'
    h = ['/* SPDX-License-Identifier: GPL-3.0-only',
         ' * Generated by android/tools/jv1010/make_jv.py from perf.tsv and hints.tsv; do not edit.',
         ' * The JV-1010 performance "GEN-1 Keys" (USER:01) and a part of it for each style\'s chords, Marble, arp and',
         ' * lead: suggestions only. */',
         '#pragma once',
         '#include <stdint.h>',
         '#include "gen.h"',
         '',
         '#define JV_NSTYLES %d' % len(styles),
         '_Static_assert(JV_NSTYLES == GEN_NSTYLES, "jv_data.h: run make_jv.py");',
         '/* the performance\'s part names by part number (1-16; 10, the rhythm part, is off) */',
         'static const char *const JV_PART[17] = {' + ', '.join(c_str(x) for x in names) + '};',
         '/* per style, in the style enum\'s order: the JV part for its ' + ', '.join(c.lower() for c in HINT_COLS)
         + '; 0 = none */',
         'static const uint8_t JV_HINT[JV_NSTYLES][%d] = {' % len(HINT_COLS)]
    for s_ in styles:
        h.append('    {%s}, /* %s */' % (', '.join(str(x) for x in hints[s_]), s_[3:]))
    h += ['};', '']
    hc = '\n'.join(h)

    stale = []
    for path, data, mode in ((KT, kt.encode('utf-8'), 'wb'), (HC, hc.encode('utf-8'), 'wb'), (SYX, syx, 'wb')):
        old = open(path, 'rb').read() if os.path.exists(path) else None
        if old != data:
            if check:
                stale.append(os.path.basename(path))
            else:
                open(path, mode).write(data)
    if check:
        if stale:
            sys.exit(' and '.join(stale) + ' out of date: run tools/jv1010/make_jv.py')
        print('JV-1010: JvData.kt, jv_data.h and jv1010_user01.syx up to date')
    else:
        print(f'JV-1010: {len(styles)} styles, {len(msgs)} SysEx messages, {len(syx)} bytes written')


if __name__ == '__main__':
    main()
