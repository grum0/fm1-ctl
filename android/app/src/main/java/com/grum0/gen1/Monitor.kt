package com.grum0.gen1

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.delay
import kotlin.math.abs
import kotlin.math.sqrt

private const val MON_LINES = 200       // lines kept on screen
private const val MON_TICKS = 96        // clock ticks the tempo and jitter are measured over (four beats)

/** One MIDI message in words: "ch 4  note on C2 100". */
private fun describe(st: Int, d1: Int, d2: Int): String {
    val ch = (st and 15) + 1
    return when (st and 0xF0) {
        0x80 -> "ch $ch  note off ${noteName(d1)}"
        0x90 -> if (d2 == 0) "ch $ch  note off ${noteName(d1)}" else "ch $ch  note on ${noteName(d1)} $d2"
        0xA0 -> "ch $ch  poly pressure ${noteName(d1)} $d2"
        0xB0 -> "ch $ch  CC $d1 = $d2"
        0xC0 -> "ch $ch  program $d1"
        0xD0 -> "ch $ch  pressure $d1"
        0xE0 -> "ch $ch  pitch bend ${(d1 or (d2 shl 7)) - 8192}"
        else -> when (st) {
            0xF8 -> "clock"
            0xFA -> "start"
            0xFB -> "continue"
            0xFC -> "stop"
            0xF2 -> "song position ${d1 or (d2 shl 7)} (16ths)"
            0xFE -> "active sensing"
            else -> "system " + Integer.toHexString(st).uppercase()
        }
    }
}

/**
 * The MIDI monitor (Rig): every message out and in, with its time. The engine only records while this is on the
 * screen (Native.nMonOpen); this reads its ring ten times a second. Following the MPC it also shows the tempo
 * measured from the MPC's clock and how much the ticks wander (jitter).
 */
@Composable
fun MidiMonitor(s: Store) {
    var open by remember { mutableStateOf(false) }
    SwitchRow("Show the MIDI monitor", open) { open = it }
    if (!open) return
    var paused by remember { mutableStateOf(false) }
    var hideClock by remember { mutableStateOf(true) }
    var lost by remember { mutableIntStateOf(0) }
    var clockText by remember { mutableStateOf("") }
    val lines = remember { mutableStateListOf<String>() }

    DisposableEffect(Unit) {
        Native.nMonOpen(1)
        onDispose { Native.nMonOpen(0) }
    }
    LaunchedEffect(Unit) {
        val t = LongArray(256)
        val b = ByteArray(256 * 5)
        val res = IntArray(3)
        val ticks = LongArray(MON_TICKS + 1)
        var nTicks = 0
        val opened = System.nanoTime()
        var t0 = 0L
        var since = 0
        while (true) {
            do {
                Native.nMonRead(since, t, b, res)
                since = res[0]
                lost += res[2]
                for (i in 0 until res[1]) {
                    if (t[i] < opened - 1_000_000_000L) continue    // (left from an earlier time the monitor was open)
                    val dir = b[i * 5].toInt()
                    val st = b[i * 5 + 2].toInt() and 0xFF
                    val d1 = b[i * 5 + 3].toInt() and 0xFF
                    val d2 = b[i * 5 + 4].toInt() and 0xFF
                    if (dir == 1 && st == 0xF8) {
                        ticks[nTicks % ticks.size] = t[i]
                        nTicks++
                    }
                    if (paused || (hideClock && (st == 0xF8 || st == 0xFE))) continue
                    if (t0 == 0L) t0 = t[i]
                    val secs = (t[i] - t0) / 1_000_000L / 1000.0
                    lines.add("%9.3f  %s  %s".format(secs, if (dir == 1) "IN " else "OUT", describe(st, d1, d2)))
                    while (lines.size > MON_LINES) lines.removeAt(0)
                }
            } while (res[1] == t.size)
            // the followed clock: the tempo over the last four beats, and how far single ticks stray from the mean
            if (s.vals[F.CLOCK] == F.CLK_EXT && nTicks > MON_TICKS) {
                val n = MON_TICKS
                val last = nTicks - 1
                val iv = LongArray(n) { k ->
                    ticks[(last - k) % ticks.size] - ticks[(last - k - 1 + ticks.size) % ticks.size]
                }
                val mean = iv.average()
                val dev = iv.maxOf { abs(it - mean) }
                val sd = sqrt(iv.sumOf { (it - mean) * (it - mean) } / n)
                clockText = if (mean > 0) {
                    "MPC clock: %.2f BPM · tick jitter %.2f ms typical, %.2f ms worst".format(
                        60e9 / (mean * 24), sd / 1e6, dev / 1e6,
                    )
                } else ""
            } else if (s.vals[F.CLOCK] == F.CLK_EXT) {
                clockText = "MPC clock: waiting for four beats of clock"
            } else {
                clockText = ""
            }
            delay(100)
        }
    }

    Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
        SmallButton(if (paused) "Go on" else "Pause") { paused = !paused }
        SmallButton("Clear", Modifier.padding(start = 6.dp)) { lines.clear() }
    }
    SwitchRow("Hide clock ticks", hideClock) { hideClock = it }
    if (clockText.isNotEmpty()) Text(clockText, style = MaterialTheme.typography.bodySmall, color = Mint)
    if (lost > 0) Text("$lost messages came too fast to show", style = MaterialTheme.typography.bodySmall)
    Column(Modifier.fillMaxWidth().padding(top = 4.dp)) {
        // newest first; times in seconds from the first message shown (out: when it sounds, in: when it arrived)
        for (i in lines.indices.reversed()) {
            Text(lines[i], fontFamily = FontFamily.Monospace, fontSize = 11.sp, maxLines = 1)
        }
    }
}
