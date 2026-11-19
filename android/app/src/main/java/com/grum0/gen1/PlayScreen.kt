package com.grum0.gen1

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp

private val KEYS = listOf("C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B")
private val CLOCKS = listOf("Send clock", "Internal", "Follow MPC")
private val PHRASES = listOf(1, 2, 4, 8)
val VOICINGS = listOf("Style", "Tight", "Wide", "Octaves")

@Composable
fun StepStrip(s: Store) {
    Row(Modifier.fillMaxWidth().padding(vertical = 6.dp), horizontalArrangement = Arrangement.spacedBy(3.dp)) {
        for (i in 0 until 16) {
            val lit = s.running && s.step == i
            val c = if (lit) Accent else if (i % 4 == 0) Color(0xFF3A3A44) else Color(0xFF25252C)
            Box(Modifier.weight(1f).height(18.dp).background(c, RoundedCornerShape(3.dp)))
        }
    }
}

@Composable
fun PlayScreen(s: Store) {
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 12.dp, vertical = 4.dp)) {
        StepStrip(s)

        Section("Song") {
            StylePicker(s)
            Text("Key", style = MaterialTheme.typography.bodyMedium)
            ChipGrid(KEYS, { it == s.vals[F.ROOT] }, 6) { s.set(F.ROOT, it) }
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                FilterChip(selected = s.vals[F.MINOR] == 1, onClick = { s.set(F.MINOR, 1) }, label = { Text("Minor") }, modifier = Modifier.weight(1f))
                FilterChip(selected = s.vals[F.MINOR] == 0, onClick = { s.set(F.MINOR, 0) }, label = { Text("Major") }, modifier = Modifier.weight(1f))
            }
            Row(Modifier.fillMaxWidth().padding(top = 6.dp), verticalAlignment = androidx.compose.ui.Alignment.CenterVertically) {
                Text("Seed", Modifier.weight(1f), style = MaterialTheme.typography.bodyMedium)
                Stepper("${s.vals[F.SEED]}", { s.set(F.SEED, s.vals[F.SEED] - 1) }, { s.set(F.SEED, s.vals[F.SEED] + 1) })
                SmallButton("New") { s.newSeed() }
            }
        }

        Section("Parts") {
            ChipGrid(
                List(F.NPARTS) { p -> (if (s.heard shr p and 1 == 1) "● " else "") + s.partNames[p] },
                { s.vals[F.part(it, F.PF_ON)] == 1 },
                4,
            ) { p -> s.set(F.part(p, F.PF_ON), 1 - s.vals[F.part(p, F.PF_ON)]) }
        }

        Section("Groove") {
            SliderRow(s, "Swing", F.SWING, { if (it == 0) "Style" else "${49 + it}%" })
            SliderRow(s, "Feel", F.FEEL, { if (it == 8) "8 (as written)" else "$it" })
            SliderRow(s, "Straighten the hats", F.HAT_STRAIGHT, { if (it == 0) "off: they swing with the kit" else if (it == 100) "straight" else "$it%" })
            SwitchRow("Cut-A-Dor's pocket on the styles' drums (by Feel)", s.vals[F.POCKET] == 1) {
                s.set(F.POCKET, if (it) 1 else 0)
            }
            SliderRow(s, "Mutate", F.MUTATE)
            Row(Modifier.fillMaxWidth(), verticalAlignment = androidx.compose.ui.Alignment.CenterVertically) {
                Text("Phrase", Modifier.weight(1f), style = MaterialTheme.typography.bodyMedium)
                Stepper("${PHRASES[s.vals[F.PHRASE]]} bars", { s.set(F.PHRASE, s.vals[F.PHRASE] - 1) }, { s.set(F.PHRASE, s.vals[F.PHRASE] + 1) })
            }
        }

        Section("Chords") {
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                FilterChip(selected = s.vals[F.PROG] == 0, onClick = { s.set(F.PROG, 0) }, label = { Text("Style's progression") }, modifier = Modifier.weight(1f))
                FilterChip(selected = s.vals[F.PROG] == 1, onClick = { s.set(F.PROG, 1) }, label = { Text("From keys") }, modifier = Modifier.weight(1f))
            }
            if (s.vals[F.PROG] == 1) {
                Text("Tap a note to set the chord", style = MaterialTheme.typography.bodySmall)
                ChipGrid(KEYS, { false }, 6) { Native.nChordKey(it) }
            }
            Text("Voicing", Modifier.padding(top = 6.dp), style = MaterialTheme.typography.bodyMedium)
            ChipGrid(VOICINGS, { it == s.vals[F.VOICE] }, 4) { s.set(F.VOICE, it) }
        }

        Section("Clock") {
            ChipGrid(CLOCKS, { it == s.vals[F.CLOCK] }, 3) { s.set(F.CLOCK, it) }
            val following = s.vals[F.CLOCK] == F.CLK_EXT
            Text(
                when {
                    !following -> "The phone is the clock" + if (s.vals[F.CLOCK] == F.CLK_INT_OUT) ", and sends it out" else ", and keeps it to itself"
                    s.clockSeen -> "Following the MPC: ${s.bpm10 / 10}.${s.bpm10 % 10} BPM"
                    else -> "Waiting for the MPC's clock…"
                },
                style = MaterialTheme.typography.bodySmall,
                color = if (following && !s.clockSeen) Accent else Color.Unspecified,
            )
            SliderRow(s, if (following) "Tempo (until the clock comes)" else "Tempo", F.TEMPO, { "$it BPM" })
            val st = s.vals[F.STYLE].coerceIn(0, s.styleNames.size - 1)
            Text("${s.styleNames[st]} sits at ${s.styleLo[st]} to ${s.styleHi[st]} BPM", style = MaterialTheme.typography.bodySmall)
        }
    }
}
