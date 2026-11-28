package com.grum0.gen1

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

private val COUPLES = listOf("as the style has it", "free", "follows the kick", "locked to the kick")

@Composable
fun PartsScreen(s: Store) {
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 12.dp, vertical = 4.dp)) {
        for (p in 0 until F.NPARTS) PartCard(s, p)
    }
}

@Composable
fun PartCard(s: Store, p: Int) {
    val on = s.vals[F.part(p, F.PF_ON)] == 1
    val inst = s.target[p]
    val modes = s.modeNames[p]
    Section(s.partNames[p]) {
        SwitchRow("Play", on) { s.set(F.part(p, F.PF_ON), if (it) 1 else 0) }
        Pick(
            "Instrument",
            "${INSTRUMENTS[inst].name} (ch ${s.chan[inst]})",
            INSTRUMENTS.mapIndexed { i, ins -> "${ins.name} (ch ${s.chan[i]})" },
        ) { s.setTarget(p, it) }
        Pick("Mode", modes.getOrElse(s.vals[F.part(p, F.PF_MODE)]) { "?" }, modes) { s.set(F.part(p, F.PF_MODE), it) }
        if (p == F.GP_BASS) Pick("With the kick", COUPLES.getOrElse(s.vals[F.COUPLE]) { "?" }, COUPLES) { s.set(F.COUPLE, it) }
        if (p == F.GP_CHORD) Pick("Voicing", VOICINGS.getOrElse(s.vals[F.VOICE]) { "?" }, VOICINGS) { s.set(F.VOICE, it) }
        SliderRow(s, "Density", F.part(p, F.PF_DENS))
        SliderRow(s, if (p == F.GP_RAND) "Chance" else "Flair", F.part(p, F.PF_FLAIR))
        if (p == F.GP_RAND) {
            SliderRow(s, "Spread", F.SPREAD, { "$it scale steps" })
            // 0.5.4: after Mutable Instruments' Marbles
            SliderRow(s, "Bias", F.M_BIAS, { if (it == 64) "middle" else if (it < 64) "low $it" else "high $it" })
            SliderRow(s, "Shape", F.M_SHAPE, { if (it == 64) "even" else if (it < 64) "bell $it" else "edges $it" })
            SliderRow(s, "Steps", F.M_STEPS, { if (it >= 127) "the whole pool" else if (it < 32) "root only" else if (it < 64) "root and fifth" else "with the thirds" })
            SwitchRow("Memory (fresh notes stay in the loop)", s.vals[F.M_MEMORY] == 1) { s.set(F.M_MEMORY, if (it) 1 else 0) }
            SliderRow(s, "Shuffle", F.M_SHUFFLE, { if (it == 0) "off" else "$it" })
            SliderRow(s, "Length", F.M_LEN, { if (it == 16) "one bar" else "$it steps" })
            Pick("Rhythm", MRHYTHM.getOrElse(s.vals[F.M_RHYTHM]) { "?" }, MRHYTHM) { s.set(F.M_RHYTHM, it) }
        }
        if (p == F.GP_CHORD) Pick("Make room for the lead", ROOMS.getOrElse(s.vals[F.ROOM]) { "?" }, ROOMS) { s.set(F.ROOM, it) }
        StepRow(s, "Variation", F.part(p, F.PF_VAR))
        StepRow(s, "Octave", F.part(p, F.PF_OCT), { if (it > 0) "+$it" else "$it" })
    }
}

private val MRHYTHM = listOf("Coin", "Answer the drums", "Markov")
private val ROOMS = listOf("off", "shorten chords", "drop chords")
