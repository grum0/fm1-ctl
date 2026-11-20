package com.grum0.gen1

import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ColorFilter
import androidx.compose.ui.res.painterResource
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

@Composable
fun RigScreen(s: Store) {
    val hub = s.hub
    var sure by remember { mutableStateOf(false) }
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 12.dp, vertical = 4.dp)) {

        Section("MIDI interface") {
            val sendDevs = hub.devices.filter { it.inputPortCount > 0 }
            val sendInfo = sendDevs.firstOrNull { hub.nameOf(it) == hub.sendName }
            Text("Notes and clock go out through", style = MaterialTheme.typography.bodySmall)
            Pick("", hub.sendName ?: "None", listOf("None") + sendDevs.map { hub.nameOf(it) }) { i ->
                hub.chooseSend(if (i == 0) null else hub.nameOf(sendDevs[i - 1]), 0)
            }
            if (sendInfo != null && sendInfo.inputPortCount > 1) {
                Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                    Text("Output", Modifier.weight(1f), style = MaterialTheme.typography.bodyMedium)
                    Stepper(
                        "${hub.sendPort + 1}",
                        { hub.chooseSend(hub.sendName, (hub.sendPort - 1).coerceAtLeast(0)) },
                        { hub.chooseSend(hub.sendName, (hub.sendPort + 1).coerceAtMost(sendInfo.inputPortCount - 1)) },
                    )
                }
            }
            Text(hub.sendStatus, style = MaterialTheme.typography.bodySmall, color = Mint)

            val recvDevs = hub.devices.filter { it.outputPortCount > 0 }
            val recvInfo = recvDevs.firstOrNull { hub.nameOf(it) == hub.recvName }
            Text("The MPC's clock comes in through", Modifier.padding(top = 10.dp), style = MaterialTheme.typography.bodySmall)
            Pick("", hub.recvName ?: "None", listOf("None") + recvDevs.map { hub.nameOf(it) }) { i ->
                hub.chooseRecv(if (i == 0) null else hub.nameOf(recvDevs[i - 1]), 0)
            }
            if (recvInfo != null && recvInfo.outputPortCount > 1) {
                Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                    Text("Input", Modifier.weight(1f), style = MaterialTheme.typography.bodyMedium)
                    Stepper(
                        "${hub.recvPort + 1}",
                        { hub.chooseRecv(hub.recvName, (hub.recvPort - 1).coerceAtLeast(0)) },
                        { hub.chooseRecv(hub.recvName, (hub.recvPort + 1).coerceAtMost(recvInfo.outputPortCount - 1)) },
                    )
                }
            }
            Text(hub.recvStatus, style = MaterialTheme.typography.bodySmall, color = Mint)
            SmallButton("Scan again") {
                hub.refresh()
                hub.autoConnect()
            }
        }

        Section("Instruments: the channel each one listens on") {
            Text(
                "Everything goes down one cable, so give each instrument its own channel here and on the instrument. " +
                    "Test plays a short note to check.",
                style = MaterialTheme.typography.bodySmall,
            )
            INSTRUMENTS.forEachIndexed { i, ins ->
                Row(Modifier.fillMaxWidth().padding(top = 4.dp), verticalAlignment = Alignment.CenterVertically) {
                    Text(ins.name, Modifier.weight(1f), style = MaterialTheme.typography.bodyMedium, maxLines = 2)
                    Stepper("ch ${s.chan[i]}", { s.setChan(i, s.chan[i] - 1) }, { s.setChan(i, s.chan[i] + 1) })
                    SmallButton("Test") { s.blip(i) }
                }
            }
            Row(Modifier.fillMaxWidth().padding(top = 4.dp), verticalAlignment = Alignment.CenterVertically) {
                Text("Simmons SDS-200 (through the MPC's pads)", Modifier.weight(1f), style = MaterialTheme.typography.bodyMedium, maxLines = 2)
                Stepper(
                    if (s.vals[F.SIM_CHAN] == 0) "drums" else "ch ${s.vals[F.SIM_CHAN]}",
                    { s.set(F.SIM_CHAN, s.vals[F.SIM_CHAN] - 1) },
                    { s.set(F.SIM_CHAN, s.vals[F.SIM_CHAN] + 1) },
                )
            }
        }

        Section("Following the MPC, and the MIDI monitor") {
            StepRow(s, "Output delay after each MPC clock tick (ms)", F.OUT_DELAY)
            Text(
                "Following the MPC, everything a clock tick plays goes out this long after the tick arrives (5 ms " +
                    "as before). Raise it if the app's parts sound early against the MPC's own tracks, lower it if " +
                    "they drag. The MPC's Song Position and Continue are followed: Continue plays on from where the " +
                    "MPC was moved to.",
                style = MaterialTheme.typography.bodySmall,
            )
            MidiMonitor(s)
        }

        Section("Tuning") {
            StepRow(s, "Lead pitch bend range (semitones)", F.BEND)
            StepRow(s, "MBase kick: lowest note", F.MB_LO, { noteName(it) })
            StepRow(s, "MBase kick: transpose", F.MB_XPOSE, { if (it > 0) "+$it" else "$it" })
            StepRow(s, "MBase kick: tune CC 101 at start", F.MB_TUNE, { if (it == 0) "off" else "$it" })
        }

        Section("Settings") {
            SwitchRow("Keep the screen on", s.keepAwake) {
                s.keepAwake = it
                s.touch()
            }
            SwitchRow("Hide styles that don't suit the Tanzbär", s.hideNonTanz) {
                s.hideNonTanz = it
                s.touch()
            }
            Text(
                "Leaves out of the style picker the styles built on sampled breaks or a live kit, which the Tanzbär " +
                    "Lite's kit can't play convincingly. The style playing stays as it is.",
                style = MaterialTheme.typography.bodySmall,
            )
            SwitchRow("Show JV-1010 part suggestions", s.jvHints) {
                s.jvHints = it
                s.touch()
            }
            Text(
                "Performance GEN-1 Keys in USER:01; send jv1010_user01.syx (android/tools/jv1010 in the repository) " +
                    "to load it. Play shows which JV part suits each style's chords, Marble, arp and lead, and the " +
                    "style picker tags each style with its chords part. Pick the part on the MPC track's output " +
                    "channel: the app keeps sending on its own channels.",
                style = MaterialTheme.typography.bodySmall,
            )
            SwitchRow("Tanzbär sound follows the style and key", s.tanzStyle) {
                s.tanzStyle = it
                s.touch()
                s.applyTanzSound()
            }
            Text(
                "Picking a style, changing the key and Start set the Tanzbär's tone and decays for the style and tune " +
                    "the toms, congas and claves where the style asks (and the kick, with Kick tune on Key). The Control page's " +
                    "Tanzbär sliders follow, and stay free to change until the next style or key.",
                style = MaterialTheme.typography.bodySmall,
            )
            Text("Kick tune", style = MaterialTheme.typography.bodyMedium)
            ChipGrid(listOf("Style", "Key"), { (it == 1) == s.kickKey }, 2) {
                s.kickKey = it == 1
                s.touch()
                s.applyTanzSound()
            }
            Text(
                "Style: the style's own kick pitch. Key: the kick tuned to the key's root (its fifth in C, C# and D, " +
                    "which the kick can't reach).",
                style = MaterialTheme.typography.bodySmall,
            )
            Text("Cowbell pitch", style = MaterialTheme.typography.bodyMedium)
            ChipGrid(listOf("Style", "Fixed", "Melodic"), { it == s.bellMode }, 3) {
                s.bellMode = it
                s.touch()
                s.applyTanzSound()
            }
            Text(
                "Fixed: one pitch, in the key. Melodic: the cowbell plays a line on the chords, retuned just before " +
                    "each hit, with a short decay. Style: the bell styles (Electro bell funk, Miami bass bell, Detroit bell techno, " +
                    "Afro-funk bell) play their written bell lines, the rest a fixed pitch.",
                style = MaterialTheme.typography.bodySmall,
            )
            SwitchRow("Picking a style moves the tempo into its range", s.styleTempo) {
                s.styleTempo = it
                s.touch()
            }
            Text(
                "The clock and the parts keep going with the screen off or the app in the background: see the GEN-1 " +
                    "notification. “Stop and quit” there ends it.",
                style = MaterialTheme.typography.bodySmall,
            )
            if (!sure) {
                SmallButton("Reset everything to the defaults…") { sure = true }
            } else {
                Text("This puts every setting, channel and instrument back. Sure?", style = MaterialTheme.typography.bodyMedium)
                Row {
                    SmallButton("Yes, reset") {
                        s.resetAll()
                        sure = false
                    }
                    SmallButton("No") { sure = false }
                }
            }
        }

        Section("About") {
            Image(
                painter = painterResource(R.drawable.ic_logo),
                contentDescription = "Grumo",
                colorFilter = ColorFilter.tint(Color.White),
                modifier = Modifier.padding(vertical = 6.dp).size(width = 118.dp, height = 40.dp),
            )
            Text("GEN-1 for Android", style = MaterialTheme.typography.bodyMedium)
            Text("Build ${s.buildId}", style = MaterialTheme.typography.bodySmall)
            Text(
                "Free software under the GNU General Public License, version 3 only (GPL-3.0-only).",
                Modifier.padding(top = 6.dp),
                style = MaterialTheme.typography.bodySmall,
            )
            Text("Credits", Modifier.padding(top = 8.dp), style = MaterialTheme.typography.bodyMedium)
            Text(
                "• CTL-1 by Charles Vestal (MPL-2.0): the clock, arpeggiator and four-track sequencer.\n" +
                    "• Cut-A-Dor by Kid Bunka (GPL-3.0-only): the drum pattern generator behind the Boom Bap, Lo-Fi, " +
                    "Funk Break, Half Time, Hip-Hop and Fast Break modes.\n" +
                    "• Mutable Instruments, Grids by Emilie Gillet (GPL-3.0-or-later): the Grids drum maps, by way of " +
                    "Hortator's C port; Marbles was the inspiration for the Marble part.",
                style = MaterialTheme.typography.bodySmall,
            )
        }
    }
}
