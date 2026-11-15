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

static void sink(void *ud, int64_t when, const uint8_t *m, int len)   /* (mu held) */
{
    (void)ud;
    if (!out_port)
        return;
    AMidiInputPort_sendWithTimestamp(out_port, m, (size_t)len, when <= now_ns() ? 0 : when);
}

static void *loop(void *arg)
{
    (void)arg;
    setpriority(PRIO_PROCESS, 0, -19);   /* the audio priority, if the system lets us */
    while (thr_run) {
        struct timespec nap = {0, PERIOD_NS};
        pthread_mutex_lock(&mu);
        eng_pump(now_ns());
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
    if (out_port)
        AMidiInputPort_close(out_port);
    if (out_dev)
        AMidiDevice_release(out_dev);
    out_dev = d;
    out_port = p;
    pthread_mutex_unlock(&mu);
    return 0;
}

JNIEXPORT void JNICALL J(nCloseOut)(JNIEnv *env, jobject self)
{
    (void)env, (void)self;
    pthread_mutex_lock(&mu);
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

/* names: kind 0 style, 1 device, 2 part, 3 lane, 4 drum set, 5 a part's mode (idx = part << 8 | mode) */
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
    default: return 0;
    }
}

JNIEXPORT jint JNICALL J(nModeCount)(JNIEnv *env, jobject self, jint part)
{
    (void)env, (void)self;
    return part >= 0 && part < GEN_NPARTS ? gen_nmodes(part) : 0;
}
