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
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp

private val SIM2 = listOf("Snare layer", "Crash", "Percussion")

@Composable
fun DrumsScreen(s: Store) {
    val drums = F.GP_DRUMS
    val modes = s.modeNames[drums]
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 12.dp, vertical = 4.dp)) {
        Section("Drums") {
            SwitchRow("Play", s.vals[F.part(drums, F.PF_ON)] == 1) { s.set(F.part(drums, F.PF_ON), if (it) 1 else 0) }
            Pick("Mode", modes.getOrElse(s.vals[F.part(drums, F.PF_MODE)]) { "?" }, modes) { s.set(F.part(drums, F.PF_MODE), it) }
            Pick("Lanes sent", s.dsetNames.getOrElse(s.vals[F.DSET]) { "?" }, s.dsetNames) { s.set(F.DSET, it) }
            SliderRow(s, "Density", F.part(drums, F.PF_DENS))
            SliderRow(s, "Flair", F.part(drums, F.PF_FLAIR))
            Pick("Simmons lane 2", SIM2.getOrElse(s.vals[F.SIM2]) { "?" }, SIM2) { s.set(F.SIM2, it) }
            Pick(
                "Simmons channel",
                if (s.vals[F.SIM_CHAN] == 0) "same as the drums" else "ch ${s.vals[F.SIM_CHAN]}",
                listOf("same as the drums") + List(16) { "ch ${it + 1}" },
            ) { s.set(F.SIM_CHAN, it) }
        }

        Section("Grids (mode Grids)") {
            SliderRow(s, "Map X", F.GX)
            SliderRow(s, "Map Y", F.GY)
            SliderRow(s, "Chaos", F.CHAOS)
        }

        Section("This bar, lane by lane") {
            for (l in 0 until F.NLANES) LaneRow(s, l)
            SmallButton("Reset lane notes to the Tanzbär's") { s.resetLaneNotes() }
        }
    }
}

@Composable
private fun LaneRow(s: Store, lane: Int) {
    val bits = s.lanes[lane]
    val id = F.LANE0 + lane
    Column(Modifier.fillMaxWidth().padding(vertical = 3.dp)) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Text(s.laneNames[lane], Modifier.weight(1f), style = MaterialTheme.typography.bodyMedium)
            Stepper("${s.vals[id]}", { s.set(id, s.vals[id] - 1) }, { s.set(id, s.vals[id] + 1) })
        }
        Row(Modifier.fillMaxWidth().padding(top = 2.dp), horizontalArrangement = Arrangement.spacedBy(2.dp)) {
            for (i in 0 until 16) {
                val hit = bits shr i and 1 == 1
                val now = s.running && s.step == i
                val c = if (hit && now) Color.White else if (hit) Accent else if (i % 4 == 0) Color(0xFF34343C) else Color(0xFF25252C)
                Box(Modifier.weight(1f).height(10.dp).background(c, RoundedCornerShape(2.dp)))
            }
        }
    }
}
