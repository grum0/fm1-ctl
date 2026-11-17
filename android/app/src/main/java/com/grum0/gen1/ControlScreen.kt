package com.grum0.gen1

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

private val ARP_MODES = listOf("Up", "Down", "Up-down", "Random", "As played")
private val ARP_RATES = listOf("1/4", "1/8", "1/8 triplet", "1/16", "1/16 triplet", "1/32")

/** The controllers any synth takes, as the FM-1 firmware's banks A and B had them. */
private val COMMON_CC = listOf(
    "Cutoff" to 74, "Resonance" to 71, "Mod wheel" to 1, "Volume" to 7,
    "Attack" to 73, "Release" to 72,
)

@Composable
fun ControlScreen(s: Store) {
    val instNames = INSTRUMENTS.mapIndexed { i, ins -> "${ins.name} (ch ${s.chan[i]})" }
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 12.dp, vertical = 4.dp)) {

        Section("Keys") {
            Pick("Play", instNames[s.keysTarget], instNames) { s.chooseKeysTarget(it) }
            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                Text("Octave", Modifier.weight(1f), style = MaterialTheme.typography.bodyMedium)
                Stepper(
                    if (s.keyOct > 0) "+${s.keyOct}" else "${s.keyOct}",
                    { s.keyOct = (s.keyOct - 1).coerceAtLeast(-2); s.touch() },
                    { s.keyOct = (s.keyOct + 1).coerceAtMost(2); s.touch() },
                )
            }
            SliderRow(s, "Velocity", F.VEL)
            Piano(48 + 12 * s.keyOct, 2, { s.keyDown(it) }, { s.keyUp(it) })
        }

        Section("Arpeggiator (on the keys above)") {
            SwitchRow("Arpeggiator", s.vals[F.ARP_ON] == 1) { s.set(F.ARP_ON, if (it) 1 else 0) }
            Pick("Order", ARP_MODES.getOrElse(s.vals[F.ARP_MODE]) { "?" }, ARP_MODES) { s.set(F.ARP_MODE, it) }
            Pick("Rate", ARP_RATES.getOrElse(s.vals[F.ARP_RATE]) { "?" }, ARP_RATES) { s.set(F.ARP_RATE, it) }
            StepRow(s, "Octaves", F.ARP_OCT)
            SwitchRow("Latch", s.vals[F.ARP_LATCH] == 1) { s.set(F.ARP_LATCH, if (it) 1 else 0) }
            SliderRow(s, "Humanize (velocity spread)", F.HUMAN)
        }

        Section("Tanzbär Lite") {
            Text(
                "Sent on the Tanzbär's channel (${s.chan[0]}). The knobs stay where you leave them.",
                style = MaterialTheme.typography.bodySmall,
            )
            for (voice in TANZ) {
                Text(voice.name, Modifier.padding(top = 8.dp), style = MaterialTheme.typography.titleSmall, color = Mint)
                for ((label, cc) in voice.knobs) {
                    ValueSlider("$label  (CC $cc)", s.tanzValue(cc), { s.setTanz(cc, it) })
                }
            }
        }

        Section("Any instrument") {
            var bend by remember { mutableFloatStateOf(8192f) }
            Pick("Send to", instNames[s.ccTarget], instNames) { s.chooseCcTarget(it) }
            for ((label, cc) in COMMON_CC) {
                ValueSlider("$label  (CC $cc)", s.ccValue(s.ccTarget, cc), { s.setCc(s.ccTarget, cc, it) })
            }
            Text("Pitch bend", Modifier.padding(top = 6.dp), style = MaterialTheme.typography.bodyMedium)
            Slider(
                value = bend,
                onValueChange = {
                    bend = it
                    s.sendBend(s.ccTarget, it.toInt())
                },
                onValueChangeFinished = {
                    bend = 8192f
                    s.sendBend(s.ccTarget, 8192)
                },
                valueRange = 0f..16383f,
            )
        }

        Section("Four-track sequencer") {
            Text(
                "Euclidean tracks, for when you want a loop beside the generators. They start muted.",
                style = MaterialTheme.typography.bodySmall,
            )
            Pick("Play", instNames[s.seqTarget], instNames) { s.chooseSeqTarget(it) }
            for (t in 0 until F.NTRACKS) {
                Text("Track ${t + 1}", Modifier.padding(top = 8.dp), style = MaterialTheme.typography.titleSmall, color = Mint)
                SwitchRow("Playing", s.vals[F.trk(t, F.TF_MUTE)] == 0) { s.set(F.trk(t, F.TF_MUTE), if (it) 0 else 1) }
                StepRow(s, "Steps", F.trk(t, F.TF_STEPS))
                StepRow(s, "Hits", F.trk(t, F.TF_HITS))
                StepRow(s, "Rotate", F.trk(t, F.TF_ROT))
                StepRow(s, "Note", F.trk(t, F.TF_NOTE), { "$it ${noteName(it)}" })
            }
        }
    }
}
