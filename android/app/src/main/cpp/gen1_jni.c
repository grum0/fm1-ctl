/* SPDX-License-Identifier: GPL-3.0-or-later */
/* GEN-1 for Android: the glue between Kotlin (Native.kt), the engine (gen1_engine.c) and the phone's MIDI service
 * (the NDK's AMidi). One thread makes the internal clock and the generators' bars; one mutex serialises it with the
 * screen and the MIDI input. Messages go out stamped with the time they should sound (CLOCK_MONOTONIC, the same
 * clock as System.nanoTime), so a late thread does not make late notes. */
#include <jni.h>
#include <amidi/AMidi.h>
#include <android/log.h>
#include <pthread.h>
#include <sys/resource.h>
#include <time.h>
#include <string.h>
#include <stdint.h>
#include "gen1_engine.h"

#define TAG "GEN-1"
#define PERIOD_NS 4000000L               /* the thread wakes every 4 ms; the engine looks 30 ms ahead */

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static AMidiDevice *out_dev;
static AMidiInputPort *out_port;         /* (AMidi's "input port" is the device's input: where we send) */
static pthread_t thr;
static volatile int thr_run, inited;

static int64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

/* What has been sent to the instruments, and when it should sound: so that a cable that drops out (taking with it the
 * note-offs made meanwhile, and those still queued in Android's MIDI service) cannot leave a note hanging. When the
 * port is back, every note that may still be sounding is ended and a pitch bend left off-centre is centred. */
static int64_t on_t[16][128];            /* the time of each note's last note-on that went out, or 0 */
static int64_t offs_t[16][128];          /* ... and of its last note-off */
static uint16_t bend_moved;              /* channels with the pitch bend off centre */
static int64_t close_t;                  /* when the port was last lost or closed */
#define DETECT_NS 200000000LL            /* a dropout is noticed late: look back this far */

/* A note-off is sent again 60 ms and 250 ms after it sounded, unless that note has been struck since: a USB packet
 * lost on a poor cable would otherwise leave the note held (a mono synth falls back to it when the next ends). */
#define NREP 64
static struct { int64_t due; uint8_t ch, note, stage; } rep[NREP];
static int rep_head;

static void rep_add(int64_t when, int ch, int note, int stage, int64_t gap)
{
    int i = rep_head++ & (NREP - 1);
    rep[i].due = when + gap;
    rep[i].ch = (uint8_t)ch;
    rep[i].note = (uint8_t)note;
    rep[i].stage = (uint8_t)stage;
}

static void rep_run(int64_t now)         /* (mu held) */
{
    int i;
    if (!out_port)
        return;
    for (i = 0; i < NREP; i++) {
        if (!rep[i].due || rep[i].due > now)
            continue;
        /* only once the last note-off has sounded: if the note was struck again and its own note-off is still to
         * come, a resend now would cut it short (that note-off has its own entry) */
        if (on_t[rep[i].ch][rep[i].note] <= offs_t[rep[i].ch][rep[i].note] && offs_t[rep[i].ch][rep[i].note] <= now) {
            uint8_t m[3];
            m[0] = (uint8_t)(0x80 | rep[i].ch);
            m[1] = rep[i].note;
            m[2] = 0;
            AMidiInputPort_sendWithTimestamp(out_port, m, 3, 0);
            if (rep[i].stage == 0) {
                rep[i].stage = 1;
                rep[i].due = now + 190000000LL;
                continue;
            }
        }
        rep[i].due = 0;
    }
}

static void track(int64_t when, const uint8_t *m, int len)
{
    int ch, k;
    if (len < 3)
        return;
    ch = m[0] & 15;
    k = m[0] & 0xF0;
    if (k == 0x90 && m[2])
        on_t[ch][m[1] & 127] = when ? when : 1;
    else if (k == 0x80 || k == 0x90) {
        offs_t[ch][m[1] & 127] = when;
        rep_add(when > 0 ? when : now_ns(), ch, m[1] & 127, 0, 60000000LL);
    }
    else if (k == 0xE0) {
        if (m[1] == 0 && m[2] == 64)
            bend_moved &= (uint16_t)~(1u << ch);
        else
            bend_moved |= (uint16_t)(1u << ch);
    }
}

static void settle(void)                 /* (mu held, the port open) */
{
    int ch, n;
    int64_t t0 = close_t - DETECT_NS;
    for (ch = 0; ch < 16; ch++) {
        for (n = 0; n < 128; n++) {
            int64_t on = on_t[ch][n], off = offs_t[ch][n];
            if (on && (on > off || off > t0)) {      /* no off after it, or an off that may not have arrived */
                uint8_t m[3];
                m[0] = (uint8_t)(0x80 | ch);
                m[1] = (uint8_t)n;
                m[2] = 0;
                AMidiInputPort_sendWithTimestamp(out_port, m, 3, 0);
            }
            on_t[ch][n] = 0;
            offs_t[ch][n] = 0;
        }
        if (bend_moved >> ch & 1u) {
            uint8_t m[3];
            m[0] = (uint8_t)(0xE0 | ch);
            m[1] = 0;
            m[2] = 64;
            AMidiInputPort_sendWithTimestamp(out_port, m, 3, 0);
        }
    }
    bend_moved = 0;
    memset(rep, 0, sizeof rep);
}

/* ---------------------------------------------------------------- MIDI monitor --- *
 * The real-time side (the output sink and MIDI in, both under mu, so one writer at a time) writes fixed-size
 * records into a ring while the monitor is open, and nothing else: no allocation, no logging, no lock of its own.
 * The screen reads it without the lock (nMonRead); a record overwritten while being read is dropped. */
#define MON_N 1024u
typedef struct { int64_t t; uint8_t dir, len, b[3]; } mon_t;   /* dir 0 out, 1 in */
static mon_t mon[MON_N];
static uint32_t mon_w;                   /* records written, ever (atomic) */
static int mon_on;                       /* (atomic) */

static void mon_put(int dir, int64_t t, const uint8_t *m, int len)
{
    uint32_t w;
    mon_t *r;
    if (!__atomic_load_n(&mon_on, __ATOMIC_RELAXED))
        return;
    w = __atomic_load_n(&mon_w, __ATOMIC_RELAXED);
    r = &mon[w % MON_N];
    r->t = t;
    r->dir = (uint8_t)dir;
    r->len = (uint8_t)(len > 3 ? 3 : len);
    r->b[0] = m[0];
    r->b[1] = len > 1 ? m[1] : 0;
    r->b[2] = len > 2 ? m[2] : 0;
    __atomic_store_n(&mon_w, w + 1u, __ATOMIC_RELEASE);
}

static void sink(void *ud, int64_t when, const uint8_t *m, int len)   /* (mu held) */
{
    (void)ud;
    if (!out_port)
        return;                          /* (not tracked: it never went out) */
    if (AMidiInputPort_sendWithTimestamp(out_port, m, (size_t)len, when <= now_ns() ? 0 : when) >= 0) {
        track(when, m, len);
        mon_put(0, when, m, len);
    }
}

static void *loop(void *arg)
{
    (void)arg;
    setpriority(PRIO_PROCESS, 0, -19);   /* the audio priority, if the system lets us */
    while (thr_run) {
        struct timespec nap = {0, PERIOD_NS};
        pthread_mutex_lock(&mu);
        eng_pump(now_ns());
        rep_run(now_ns());
        pthread_mutex_unlock(&mu);
        nanosleep(&nap, 0);
    }
    return 0;
}

#define J(name) Java_com_grum0_gen1_Native_##name

JNIEXPORT void JNICALL J(nInit)(JNIEnv *env, jobject self)
{
    (void)env, (void)self;
    pthread_mutex_lock(&mu);
    if (!inited) {
        eng_init(sink, 0);
        inited = 1;
    }
    pthread_mutex_unlock(&mu);
    if (!thr_run) {
        thr_run = 1;
        if (pthread_create(&thr, 0, loop, 0)) {
            thr_run = 0;
            __android_log_print(ANDROID_LOG_ERROR, TAG, "no thread");
        }
    }
}

JNIEXPORT void JNICALL J(nShutdown)(JNIEnv *env, jobject self)
{
    (void)env, (void)self;
    if (thr_run) {
        thr_run = 0;
        pthread_join(thr, 0);
    }
}

JNIEXPORT jint JNICALL J(nOpenOut)(JNIEnv *env, jobject self, jobject midi_device, jint port)
{
    AMidiDevice *d = 0;
    AMidiInputPort *p = 0;
    media_status_t st;
    (void)self;
    st = AMidiDevice_fromJava(env, midi_device, &d);
    if (st != AMEDIA_OK)
        return (jint)st;
    st = AMidiInputPort_open(d, port, &p);
    if (st != AMEDIA_OK) {
        AMidiDevice_release(d);
        return (jint)st;
    }
    pthread_mutex_lock(&mu);
    if (out_port) {
        close_t = now_ns();
        AMidiInputPort_close(out_port);
    }
    if (out_dev)
        AMidiDevice_release(out_dev);
    out_dev = d;
    out_port = p;
    settle();                            /* whatever the last connection left sounding, off */
    pthread_mutex_unlock(&mu);
    return 0;
}

JNIEXPORT void JNICALL J(nCloseOut)(JNIEnv *env, jobject self)
{
    (void)env, (void)self;
    pthread_mutex_lock(&mu);
    close_t = now_ns();
    if (out_port)
        AMidiInputPort_close(out_port);
    if (out_dev)
        AMidiDevice_release(out_dev);
    out_port = 0;
    out_dev = 0;
    pthread_mutex_unlock(&mu);
}

/* MIDI in: the realtime bytes that matter (clock, start, continue, stop), stamped as the USB read them */
/* MIDI in, a byte at a time (a message may be split over two packets): the clock bytes and Song Position Pointer
 * go to the engine, every whole message to the monitor. Running status is kept; real-time bytes may come between
 * a message's bytes; system exclusive is skipped. (mu held) */
static uint8_t in_st, in_need, in_have, in_msg[3], in_sysex;
static void in_byte(uint8_t b, int64_t t)
{
    if (b >= 0xF8) {                             /* real time: on its own, anywhere */
        if (b == 0xF8 || b == 0xFA || b == 0xFB || b == 0xFC)
            eng_midi_clock(b, t);
        mon_put(1, t, &b, 1);
        return;
    }
    if (b & 0x80u) {
        in_sysex = b == 0xF0;
        in_have = 0;
        in_st = b;
        in_msg[0] = b;
        if (b >= 0xF0) {
            in_need = b == 0xF2 ? 2 : (b == 0xF1 || b == 0xF3) ? 1 : 0;
            if (!in_need) {
                if (!in_sysex && b != 0xF7)
                    mon_put(1, t, &b, 1);
                in_st = 0;                       /* (system messages cancel running status) */
            }
        } else {
            in_need = (b & 0xF0u) == 0xC0u || (b & 0xF0u) == 0xD0u ? 1 : 2;
        }
        return;
    }
    if (in_sysex || !in_st)
        return;
    if (in_have == 0)
        in_msg[0] = in_st;
    in_msg[1 + in_have++] = b;
    if (in_have < in_need)
        return;
    if (in_st == 0xF2)
        eng_midi_spp(in_msg[1] | in_msg[2] << 7, t);
    mon_put(1, t, in_msg, 1 + in_need);
    in_have = 0;
    if (in_st >= 0xF0)
        in_st = 0;
}

JNIEXPORT void JNICALL J(nMidiIn)(JNIEnv *env, jobject self, jbyteArray data, jint off, jint count, jlong ts)
{
    jbyte buf[256];
    int i, n = count > 256 ? 256 : count;
    int64_t now = now_ns(), t = (int64_t)ts;
    (void)self;
    if (n <= 0)
        return;
    (*env)->GetByteArrayRegion(env, data, off, n, buf);
    if (t <= 0 || t > now || t < now - 1000000000LL)
        t = now;
    pthread_mutex_lock(&mu);
    for (i = 0; i < n; i++)
        in_byte((uint8_t)buf[i], t);
    pthread_mutex_unlock(&mu);
}


/* the monitor: on while the screen shows it (records are only made then) */
JNIEXPORT void JNICALL J(nMonOpen)(JNIEnv *env, jobject self, jint on)
{
    (void)env, (void)self;
    __atomic_store_n(&mon_on, on ? 1 : 0, __ATOMIC_RELAXED);
}

/* The records from `since` (a count of records, as returned before; 0 at first) to now, at most what the arrays
 * hold: t[] their times (ns, the same clock as the engine's: out the time they sound, in the time they arrived),
 * b[] five bytes each (direction 0 out / 1 in, length, the message). res[0] the count to pass next time, res[1]
 * how many were filled in, res[2] how many were lost (overwritten before they were read). Takes no lock. */
JNIEXPORT void JNICALL J(nMonRead)(JNIEnv *env, jobject self, jint since, jlongArray t, jbyteArray b, jintArray res)
{
    static jlong ts[256];
    static jbyte bs[256 * 5];
    jint r[3];
    uint32_t w, from = (uint32_t)since, i, n, cap, lost = 0;
    (void)self;
    cap = (uint32_t)(*env)->GetArrayLength(env, t);
    if (cap > 256)
        cap = 256;
    w = __atomic_load_n(&mon_w, __ATOMIC_ACQUIRE);
    if (w - from > MON_N) {                      /* (the ring went round since the last read) */
        lost = w - from - MON_N;
        from = w - MON_N;
    }
    n = w - from < cap ? w - from : cap;
    for (i = 0; i < n; i++) {
        const mon_t *m = &mon[(from + i) % MON_N];
        ts[i] = m->t;
        bs[i * 5] = (jbyte)m->dir;
        bs[i * 5 + 1] = (jbyte)m->len;
        bs[i * 5 + 2] = (jbyte)m->b[0];
        bs[i * 5 + 3] = (jbyte)m->b[1];
        bs[i * 5 + 4] = (jbyte)m->b[2];
    }
    w = __atomic_load_n(&mon_w, __ATOMIC_ACQUIRE);
    if (w - from > MON_N) {                      /* some were overwritten while being copied: drop them */
        uint32_t bad = w - from - MON_N;
        if (bad > n)
            bad = n;
        memmove(ts, ts + bad, (n - bad) * sizeof ts[0]);
        memmove(bs, bs + bad * 5, (n - bad) * 5);
        n -= bad;
        lost += bad;
        from += bad;
    }
    (*env)->SetLongArrayRegion(env, t, 0, (jsize)n, ts);
    (*env)->SetByteArrayRegion(env, b, 0, (jsize)(n * 5), bs);
    r[0] = (jint)(from + n);
    r[1] = (jint)n;
    r[2] = (jint)lost;
    (*env)->SetIntArrayRegion(env, res, 0, 3, r);
}

JNIEXPORT jint JNICALL J(nGet)(JNIEnv *env, jobject self, jint id)
{
    int v;
    (void)env, (void)self;
    pthread_mutex_lock(&mu);
    v = eng_get(id);
    pthread_mutex_unlock(&mu);
    return v;
}

JNIEXPORT void JNICALL J(nSet)(JNIEnv *env, jobject self, jint id, jint v)
{
    (void)env, (void)self;
    pthread_mutex_lock(&mu);
    eng_set(id, v, now_ns());
    pthread_mutex_unlock(&mu);
}

JNIEXPORT void JNICALL J(nRange)(JNIEnv *env, jobject self, jint id, jintArray out)
{
    jint r[2];
    int lo, hi;
    (void)self;
    eng_range(id, &lo, &hi);
    r[0] = lo;
    r[1] = hi;
    (*env)->SetIntArrayRegion(env, out, 0, 2, r);
}

JNIEXPORT void JNICALL J(nPlay)(JNIEnv *env, jobject self, jint on)
{
    (void)env, (void)self;
    pthread_mutex_lock(&mu);
    eng_play(on, now_ns());
    pthread_mutex_unlock(&mu);
}

JNIEXPORT void JNICALL J(nPanic)(JNIEnv *env, jobject self)
{
    (void)env, (void)self;
    pthread_mutex_lock(&mu);
    eng_panic(now_ns());
    pthread_mutex_unlock(&mu);
}

JNIEXPORT void JNICALL J(nDefaults)(JNIEnv *env, jobject self)
{
    (void)env, (void)self;
    pthread_mutex_lock(&mu);
    eng_defaults(now_ns());
    pthread_mutex_unlock(&mu);
}

JNIEXPORT void JNICALL J(nSend)(JNIEnv *env, jobject self, jint st, jint d1, jint d2)
{
    (void)env, (void)self;
    pthread_mutex_lock(&mu);
    eng_send_now((uint8_t)st, (uint8_t)d1, (uint8_t)d2, now_ns());
    pthread_mutex_unlock(&mu);
}

JNIEXPORT void JNICALL J(nArp)(JNIEnv *env, jobject self, jint note, jint down)
{
    (void)env, (void)self;
    pthread_mutex_lock(&mu);
    if (down)
        eng_arp_press(note);
    else
        eng_arp_release(note);
    pthread_mutex_unlock(&mu);
}

JNIEXPORT void JNICALL J(nChordKey)(JNIEnv *env, jobject self, jint pc)
{
    (void)env, (void)self;
    pthread_mutex_lock(&mu);
    eng_chord_key(pc, now_ns());
    pthread_mutex_unlock(&mu);
}

JNIEXPORT void JNICALL J(nPartOff)(JNIEnv *env, jobject self, jint p)
{
    (void)env, (void)self;
    pthread_mutex_lock(&mu);
    eng_part_off(p, now_ns());
    pthread_mutex_unlock(&mu);
}

JNIEXPORT void JNICALL J(nStatus)(JNIEnv *env, jobject self, jintArray out)
{
    int32_t s[8];
    (void)self;
    pthread_mutex_lock(&mu);
    eng_status(s, now_ns());
    pthread_mutex_unlock(&mu);
    (*env)->SetIntArrayRegion(env, out, 0, 8, (const jint *)s);
}

JNIEXPORT jstring JNICALL J(nChordText)(JNIEnv *env, jobject self)
{
    char b[32];
    (void)self;
    memset(b, 0, sizeof b);
    pthread_mutex_lock(&mu);
    gen_chord_text(b);
    pthread_mutex_unlock(&mu);
    b[sizeof b - 1] = 0;
    return (*env)->NewStringUTF(env, b);
}

JNIEXPORT jint JNICALL J(nLaneSteps)(JNIEnv *env, jobject self, jint lane)
{
    int v;
    (void)env, (void)self;
    pthread_mutex_lock(&mu);
    v = lane >= 0 && lane < GEN_NLANES ? gen_lane_steps(lane) : 0;
    pthread_mutex_unlock(&mu);
    return v;
}

/* names: kind 0 style, 1 device, 2 part, 3 lane, 4 drum set, 5 a part's mode (idx = part << 8 | mode), 6 a style
 * family, 7 its three letters, 8 a style's artists or scene */
JNIEXPORT jstring JNICALL J(nName)(JNIEnv *env, jobject self, jint kind, jint idx)
{
    const char *s = "";
    (void)self;
    switch (kind) {
    case 0: s = idx >= 0 && idx < GEN_NSTYLES ? gen_style_name(idx) : ""; break;
    case 1: s = idx >= 0 && idx < GEN_NDEV ? gen_dev_name(idx) : ""; break;
    case 2: s = idx >= 0 && idx < GEN_NPARTS ? gen_part_name(idx) : ""; break;
    case 3: s = idx >= 0 && idx < GEN_NLANES ? gen_lane_name(idx) : ""; break;
    case 4: s = idx >= 0 && idx < GEN_NDSET ? gen_dset_name(idx) : ""; break;
    case 5:
        if ((idx >> 8) >= 0 && (idx >> 8) < GEN_NPARTS && (idx & 255) < gen_nmodes(idx >> 8))
            s = gen_mode_name(idx >> 8, idx & 255);
        break;
    case 6: s = idx >= 0 && idx < GEN_NCATS ? gen_cat_name(idx) : ""; break;
    case 7: s = idx >= 0 && idx < GEN_NCATS ? gen_cat_code(idx) : ""; break;
    case 8: s = idx >= 0 && idx < GEN_NSTYLES ? gen_style_ref(idx) : ""; break;
    default: break;
    }
    return (*env)->NewStringUTF(env, s);
}

JNIEXPORT jint JNICALL J(nCount)(JNIEnv *env, jobject self, jint kind)
{
    (void)env, (void)self;
    switch (kind) {
    case 0: return GEN_NSTYLES;
    case 1: return GEN_NDEV;
    case 2: return GEN_NPARTS;
    case 3: return GEN_NLANES;
    case 4: return GEN_NDSET;
    case 5: return F_COUNT;
    case 6: return GEN_NCATS;
    default: return 0;
    }
}

/* 0.4.1: the offset that a knob at value means over the style's base */
JNIEXPORT jint JNICALL J(nTanzOffset)(JNIEnv *env, jobject self, jint slot, jint base, jint value)
{
    (void)env, (void)self;
    return eng_tanz_offset(slot, base, value);
}
/* 0.5.0: the Tanzbär's sound, worked out by the engine; the screen sends what these return */
JNIEXPORT jint JNICALL J(nBellMelodic)(JNIEnv *env, jobject self, jint mode)
{
    int v;
    (void)env, (void)self;
    pthread_mutex_lock(&mu);
    v = eng_bell_melodic(mode);
    pthread_mutex_unlock(&mu);
    return v;
}
JNIEXPORT jint JNICALL J(nTanzSlot)(JNIEnv *env, jobject self, jint cc)
{
    (void)env, (void)self;
    return eng_tanz_slot(cc);
}
JNIEXPORT jint JNICALL J(nTanzBase)(JNIEnv *env, jobject self, jint slot, jint melodic)
{
    int v;
    (void)env, (void)self;
    pthread_mutex_lock(&mu);
    v = eng_tanz_base(slot, melodic);
    pthread_mutex_unlock(&mu);
    return v;
}
/* the CCs to send, as (cc, value) pairs into out (2 * ENG_TANZ_MAX ints); returns how many pairs */
JNIEXPORT jint JNICALL J(nTanzSound)(JNIEnv *env, jobject self, jint kick_key, jint melodic, jintArray out)
{
    uint8_t cc[ENG_TANZ_MAX], v[ENG_TANZ_MAX];
    jint r[2 * ENG_TANZ_MAX];
    int i, n;
    (void)self;
    if ((*env)->GetArrayLength(env, out) < 2 * ENG_TANZ_MAX)
        return 0;
    pthread_mutex_lock(&mu);
    n = eng_tanz_sound(kick_key, melodic, cc, v);
    pthread_mutex_unlock(&mu);
    for (i = 0; i < n; i++) {
        r[2 * i] = cc[i];
        r[2 * i + 1] = v[i];
    }
    if (n)
        (*env)->SetIntArrayRegion(env, out, 0, 2 * n, r);
    return n;
}
JNIEXPORT jint JNICALL J(nStyleTempo)(JNIEnv *env, jobject self, jint on)
{
    int v;
    (void)env, (void)self;
    pthread_mutex_lock(&mu);
    v = eng_style_tempo(on);
    pthread_mutex_unlock(&mu);
    return v;
}

/* a style's family and the tempos it sits at: out[0] CAT_*, [1] lowest BPM, [2] highest, [3] 1 if it suits the
 * Tanzbär Lite */
JNIEXPORT void JNICALL J(nStyleInfo)(JNIEnv *env, jobject self, jint style, jintArray out)
{
    jint r[4] = {0, 0, 0, 1};
    int lo = 0, hi = 0;
    (void)self;
    if (style >= 0 && style < GEN_NSTYLES) {
        gen_style_bpm(style, &lo, &hi);
        r[0] = gen_style_cat(style);
        r[1] = lo;
        r[2] = hi;
        r[3] = gen_style_tanz(style);
    }
    (*env)->SetIntArrayRegion(env, out, 0, 4, r);
}

JNIEXPORT jint JNICALL J(nModeCount)(JNIEnv *env, jobject self, jint part)
{
    (void)env, (void)self;
    return part >= 0 && part < GEN_NPARTS ? gen_nmodes(part) : 0;
}
