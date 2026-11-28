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
private val HATS = listOf("With the kit", "1/4", "1/8", "1/16")
private val GROUPS = listOf("Kick density", "Snare, clap, rim density", "Percussion and hats density")
private val VOICES = listOf("BD", "SD", "HH")
private val GRES = listOf("half speed", "normal", "double speed")
private val FILLS = listOf("at the phrase's end", "never", "every bar", "every 2 bars", "every 4 bars", "every 8 bars", "every 16 bars")

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
            for (g in 0 until 3)                         // 0.5.3: on top of Density, in every mode (Grids: BD / SD / HH fill)
                SliderRow(s, GROUPS[g], F.DENS_GRP0 + g, { if (it == 0) "as Density" else if (it > 0) "+$it" else "$it" })
            SliderRow(s, "Ghosts (soft snares in the gaps)", F.GHOSTS, { if (it == 0) "off" else "$it" })
            SliderRow(s, "Flair", F.part(drums, F.PF_FLAIR))
            Pick("Simmons lane 2", SIM2.getOrElse(s.vals[F.SIM2]) { "?" }, SIM2) { s.set(F.SIM2, it) }
            Pick(
                "Simmons channel",
                if (s.vals[F.SIM_CHAN] == 0) "same as the drums" else "ch ${s.vals[F.SIM_CHAN]}",
                listOf("same as the drums") + List(16) { "ch ${it + 1}" },
            ) { s.set(F.SIM_CHAN, it) }
        }

        Section("Hats, flams, fills") {
            Text("Closed hat", style = MaterialTheme.typography.bodyMedium)
            ChipGrid(HATS, { it == s.vals[F.HAT_DENS] }, 4) { s.set(F.HAT_DENS, it) }
            SliderRow(s, "Flam (grace notes before the snare)", F.FLAM, { if (it == 0) "off" else "$it" })
            Pick("Fill", FILLS.getOrElse(s.vals[F.FILL_EVERY]) { "?" }, FILLS) { s.set(F.FILL_EVERY, it) }
            SliderRow(s, "Ratchets (32nd hats, snare rushes)", F.RATCHET, { if (it == 0) "the style's" else "$it" })
            SliderRow(s, "Space (hats and percussion leave the phrase's end)", F.SPACE, {
                if (it < 35) "off" else if (it < 61) "the last beat" else if (it < 75) "the last beat, thinner" else "the last two beats"
            })
        }

        Section("Grids (mode Grids)") {
            SwitchRow("Euclidean mode (the fills set each voice's hits)", s.vals[F.G_EUCLID] == 1) {
                s.set(F.G_EUCLID, if (it) 1 else 0)
            }
            if (s.vals[F.G_EUCLID] == 1) {
                for (v in 0 until 3) {
                    SliderRow(s, "${VOICES[v]} length", F.G_LEN0 + v)
                    SliderRow(s, "${VOICES[v]} rotation", F.G_ROT0 + v)
                }
            } else {
                SliderRow(s, "Map X", F.GX)
                SliderRow(s, "Map Y", F.GY)
                SliderRow(s, "Chaos", F.CHAOS)
            }
            Text("Fill per voice: the kick, snare and percussion density above.", style = MaterialTheme.typography.bodySmall)
            for (v in 0 until 3) SliderRow(s, "${VOICES[v]} accent", F.G_ACC0 + v, { "+$it" })
            Pick("Clock", GRES.getOrElse(s.vals[F.G_RES]) { "?" }, GRES) { s.set(F.G_RES, it) }
            SwitchRow("Loudest hats to the open hat", s.vals[F.G_OHAT] == 1) { s.set(F.G_OHAT, if (it) 1 else 0) }
            SmallButton("Reset: the pattern from step 1 at the next bar") { Native.nGridsReset() }
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
