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
        if (p == F.GP_RAND) SliderRow(s, "Spread", F.SPREAD, { "$it scale steps" })
        StepRow(s, "Variation", F.part(p, F.PF_VAR))
        StepRow(s, "Octave", F.part(p, F.PF_OCT), { if (it > 0) "+$it" else "$it" })
    }
}
