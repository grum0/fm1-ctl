package com.grum0.gen1

import android.media.midi.MidiDevice

/** The native engine (cpp/gen1_jni.c): the firmware's generators, the clock and the MIDI output. */
object Native {
    init {
        System.loadLibrary("gen1")
    }

    external fun nInit()
    external fun nShutdown()
    external fun nOpenOut(device: MidiDevice, port: Int): Int
    external fun nCloseOut()
    external fun nMidiIn(data: ByteArray, off: Int, count: Int, ts: Long)
    external fun nGet(id: Int): Int
    external fun nSet(id: Int, v: Int)
    external fun nRange(id: Int, out: IntArray)
    external fun nPlay(on: Int)
    external fun nPanic()
    external fun nDefaults()
    external fun nSend(st: Int, d1: Int, d2: Int)
    external fun nArp(note: Int, down: Int)
    external fun nChordKey(pc: Int)
    external fun nPartOff(p: Int)
    external fun nStatus(out: IntArray)
    external fun nChordText(): String
    external fun nLaneSteps(lane: Int): Int
    external fun nName(kind: Int, idx: Int): String
    external fun nCount(kind: Int): Int
    external fun nModeCount(part: Int): Int
    external fun nStyleInfo(style: Int, out: IntArray)
}
