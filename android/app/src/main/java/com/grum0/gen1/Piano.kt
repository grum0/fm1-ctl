package com.grum0.gen1

import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.unit.dp

private val WHITE_SEMIS = intArrayOf(0, 2, 4, 5, 7, 9, 11)
private val BLACK_AFTER = intArrayOf(1, 3, -1, 6, 8, 10, -1)   // the black key above each white one, or none

/** A touch keyboard of `octaves` octaves from startNote (a C), and one key more. Every finger is its own key. */
@Composable
fun Piano(startNote: Int, octaves: Int, onDown: (Int) -> Unit, onUp: (Int) -> Unit) {
    val total = 7 * octaves + 1
    BoxWithConstraints(Modifier.fillMaxWidth().height(140.dp)) {
        val whiteW = maxWidth / total
        val blackW = whiteW * 0.6f
        Row(Modifier.fillMaxSize()) {
            for (i in 0 until total) {
                val note = startNote + (i / 7) * 12 + WHITE_SEMIS[i % 7]
                PianoKey(note, false, Modifier.weight(1f).fillMaxHeight().padding(horizontal = 0.5.dp), onDown, onUp)
            }
        }
        for (i in 0 until total - 1) {
            val b = BLACK_AFTER[i % 7]
            if (b < 0) continue
            val note = startNote + (i / 7) * 12 + b
            PianoKey(note, true, Modifier.offset(x = whiteW * (i + 1) - blackW / 2).width(blackW).height(88.dp), onDown, onUp)
        }
    }
}

@Composable
private fun PianoKey(note: Int, black: Boolean, modifier: Modifier, onDown: (Int) -> Unit, onUp: (Int) -> Unit) {
    var down by remember { mutableStateOf(false) }
    val bg = if (down) Accent else if (black) Color(0xFF1A1A1F) else Color(0xFFE9E9EC)
    Box(
        modifier
            .background(bg, RoundedCornerShape(bottomStart = 5.dp, bottomEnd = 5.dp))
            .pointerInput(note) {
                detectTapGestures(onPress = {
                    down = true
                    onDown(note)
                    try {
                        tryAwaitRelease()
                    } finally {
                        down = false
                        onUp(note)
                    }
                })
            },
    )
}

private val NAMES = listOf("C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B")
fun noteName(n: Int) = NAMES[n % 12] + (n / 12 - 1)
