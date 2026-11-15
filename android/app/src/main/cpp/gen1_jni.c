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
        if (on_t[rep[i].ch][rep[i].note] <= offs_t[rep[i].ch][rep[i].note]) {
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

static void sink(void *ud, int64_t when, const uint8_t *m, int len)   /* (mu held) */
{
    (void)ud;
    if (!out_port)
        return;                          /* (not tracked: it never went out) */
    if (AMidiInputPort_sendWithTimestamp(out_port, m, (size_t)len, when <= now_ns() ? 0 : when) >= 0)
        track(when, m, len);
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
    for (i = 0; i < n; i++) {
        uint8_t b = (uint8_t)buf[i];
        if (b == 0xF8 || b == 0xFA || b == 0xFB || b == 0xFC)
            eng_midi_clock(b, t);
    }
    pthread_mutex_unlock(&mu);
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
 * family, 7 its three letters */
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

/* a style's family and the tempos it sits at: out[0] CAT_*, [1] lowest BPM, [2] highest */
JNIEXPORT void JNICALL J(nStyleInfo)(JNIEnv *env, jobject self, jint style, jintArray out)
{
    jint r[3] = {0, 0, 0};
    int lo = 0, hi = 0;
    (void)self;
    if (style >= 0 && style < GEN_NSTYLES) {
        gen_style_bpm(style, &lo, &hi);
        r[0] = gen_style_cat(style);
        r[1] = lo;
        r[2] = hi;
    }
    (*env)->SetIntArrayRegion(env, out, 0, 3, r);
}

JNIEXPORT jint JNICALL J(nModeCount)(JNIEnv *env, jobject self, jint part)
{
    (void)env, (void)self;
    return part >= 0 && part < GEN_NPARTS ? gen_nmodes(part) : 0;
}
