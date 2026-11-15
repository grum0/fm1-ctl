package com.grum0.gen1

import android.Manifest
import android.app.Activity
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.NavigationRail
import androidx.compose.material3.NavigationRailItem
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ColorFilter
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.delay

class MainActivity : ComponentActivity() {
    private val askNotifications = registerForActivityResult(ActivityResultContracts.RequestPermission()) { }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val store = (application as Gen1App).store
        // (Android 13 on: without this the service still runs, but its notification, with "Stop and quit", is hidden)
        if (Build.VERSION.SDK_INT >= 33 &&
            checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED
        ) {
            askNotifications.launch(Manifest.permission.POST_NOTIFICATIONS)
        }
        setContent { Gen1Theme { Gen1Root(store) } }
    }

    override fun onStart() {
        super.onStart()
        ClockService.start(this)             // the clock goes on with the screen off or the app behind another
    }

    override fun onStop() {
        super.onStop()
        (application as Gen1App).store.save()
    }
}

private val TABS = listOf("Play" to "▶", "Parts" to "♪", "Drums" to "▦", "Rig" to "⚙", "Control" to "◉")

@Composable
fun Gen1Root(s: Store) {
    var tab by remember { mutableIntStateOf(0) }
    var splash by remember { mutableStateOf(true) }
    val activity = LocalContext.current as? Activity
    LaunchedEffect(Unit) {
        while (true) {
            s.poll()
            delay(50)
        }
    }
    LaunchedEffect(Unit) {
        delay(1500)
        splash = false
    }
    LaunchedEffect(s.keepAwake) {
        val w = activity?.window
        if (s.keepAwake) w?.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        else w?.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
    }
    if (splash) {
        Splash { splash = false }
        return
    }
    BoxWithConstraints(Modifier.fillMaxSize()) {
        val wide = maxWidth >= 720.dp        // a tablet on its side: Play stays up on the left, the other pages beside it
        val side = if (tab == 0) 1 else tab
        Scaffold(
            containerColor = MaterialTheme.colorScheme.background,
            bottomBar = {
                if (!wide) {
                    NavigationBar {
                        TABS.forEachIndexed { i, t ->
                            NavigationBarItem(
                                selected = tab == i,
                                onClick = { tab = i },
                                icon = { Text(t.second) },
                                label = { Text(t.first, maxLines = 1) },
                            )
                        }
                    }
                }
            },
        ) { inner ->
            Row(Modifier.padding(inner).fillMaxSize()) {
                if (wide) {
                    NavigationRail {
                        TABS.forEachIndexed { i, t ->
                            if (i > 0) {
                                NavigationRailItem(
                                    selected = side == i,
                                    onClick = { tab = i },
                                    icon = { Text(t.second) },
                                    label = { Text(t.first, maxLines = 1) },
                                )
                            }
                        }
                    }
                }
                Column(Modifier.weight(1f).fillMaxHeight()) {
                    Header(s)
                    TransportBar(s)
                    UsbWarning(s)
                    if (wide) {
                        Row(Modifier.weight(1f)) {
                            Box(Modifier.weight(1f).fillMaxHeight()) { PlayScreen(s) }
                            Box(Modifier.weight(1f).fillMaxHeight()) { Page(s, side) }
                        }
                    } else {
                        Box(Modifier.weight(1f)) { Page(s, tab) }
                    }
                }
            }
        }
    }
}

@Composable
private fun Page(s: Store, tab: Int) {
    when (tab) {
        0 -> PlayScreen(s)
        1 -> PartsScreen(s)
        2 -> DrumsScreen(s)
        3 -> RigScreen(s)
        else -> ControlScreen(s)
    }
}

/** On opening: the mark and the name, for a moment (a tap skips it). */
@Composable
private fun Splash(onDone: () -> Unit) {
    Column(
        Modifier.fillMaxSize().background(Color(0xFF0B0B0D)).clickable(onClick = onDone),
        verticalArrangement = Arrangement.Center,
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Logo(Modifier.size(width = 236.dp, height = 80.dp))
        Spacer(Modifier.height(18.dp))
        Box(Modifier.size(width = 236.dp, height = 3.dp).background(Accent))
        Spacer(Modifier.height(14.dp))
        Text("GEN-1", color = Accent, fontSize = 28.sp, fontWeight = FontWeight.Bold, letterSpacing = 8.sp)
    }
}

@Composable
fun Logo(modifier: Modifier = Modifier, tint: Color = Color.White) {
    Image(
        painter = painterResource(R.drawable.ic_logo),
        contentDescription = "Grumo",
        colorFilter = ColorFilter.tint(tint),
        modifier = modifier,
    )
}

/** The top line of every page: the mark, the name, the style. */
@Composable
private fun Header(s: Store) {
    val st = s.vals[F.STYLE].coerceIn(0, s.styleNames.size - 1)
    Row(
        Modifier.fillMaxWidth().background(Color(0xFF0B0B0D)).padding(horizontal = 12.dp, vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Logo(Modifier.size(width = 59.dp, height = 20.dp))
        Text(
            "GEN-1",
            Modifier.padding(start = 10.dp),
            color = Accent,
            style = MaterialTheme.typography.labelLarge,
            fontWeight = FontWeight.Bold,
            letterSpacing = 3.sp,
        )
        Spacer(Modifier.weight(1f))
        Text(
            "${s.catCodes[s.styleCat[st]]} · ${s.styleNames[st]}",
            style = MaterialTheme.typography.labelMedium,
            color = Mint,
            maxLines = 1,
        )
    }
}

/** A line across every page when no MIDI can go out, with what to do about it. */
@Composable
private fun UsbWarning(s: Store) {
    val hub = s.hub
    val text = when {
        hub.usbNotMidi ->
            "USB is not in MIDI mode, so nothing reaches the MPC. Pull down the notifications, tap the USB one and " +
                "choose MIDI under “Use USB for”."
        hub.sendName != null && !hub.sendOk ->
            "MIDI out is not connected (${hub.sendStatus}). Check the cable; on the MPC's cable, set “Use USB for” to MIDI."
        else -> return
    }
    Text(
        text,
        Modifier.fillMaxWidth().background(Color(0xFFB3261E)).padding(horizontal = 12.dp, vertical = 6.dp),
        color = Color.White,
        style = MaterialTheme.typography.bodySmall,
    )
}

/** On every page: start and stop, the chord and the bar, the beat, and Panic. */
@Composable
fun TransportBar(s: Store) {
    val following = s.vals[F.CLOCK] == F.CLK_EXT
    Row(
        Modifier.fillMaxWidth().background(Panel).padding(horizontal = 10.dp, vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Button(
            onClick = { s.togglePlay() },
            colors = ButtonDefaults.buttonColors(containerColor = if (s.running) Color(0xFFB3261E) else Accent),
        ) { Text(if (s.running) "■ Stop" else "▶ Play") }
        Column(Modifier.weight(1f).padding(horizontal = 12.dp)) {
            Text(
                if (s.chordText.isEmpty()) "—" else s.chordText,
                style = MaterialTheme.typography.headlineSmall,
                fontWeight = FontWeight.Bold,
            )
            Text(
                "bar ${s.bar + 1}/${s.phraseBars}  ·  " +
                    if (following && !s.clockSeen) "no clock" else "${s.bpm10 / 10}.${s.bpm10 % 10} BPM",
                style = MaterialTheme.typography.bodySmall,
            )
        }
        Box(
            Modifier
                .size(12.dp)
                .background(if (s.beat && s.running) Mint else Color(0xFF33333B), CircleShape),
        )
        OutlinedButton(onClick = { s.panic() }, modifier = Modifier.padding(start = 10.dp)) { Text("Panic") }
    }
}
