package com.grum0.gen1

import android.os.Bundle
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.delay

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        // the clock lives in this app's thread: keep the screen (and so the app) awake while it is up
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        val store = (application as Gen1App).store
        setContent { Gen1Theme { Gen1Root(store) } }
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
    LaunchedEffect(Unit) {
        while (true) {
            s.poll()
            delay(50)
        }
    }
    Scaffold(
        containerColor = MaterialTheme.colorScheme.background,
        bottomBar = {
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
        },
    ) { inner ->
        Column(Modifier.padding(inner).fillMaxSize()) {
            TransportBar(s)
            Box(Modifier.weight(1f)) {
                when (tab) {
                    0 -> PlayScreen(s)
                    1 -> PartsScreen(s)
                    2 -> DrumsScreen(s)
                    3 -> RigScreen(s)
                    else -> ControlScreen(s)
                }
            }
        }
    }
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
                .padding(end = 0.dp)
                .background(if (s.beat && s.running) Mint else Color(0xFF33333B), CircleShape),
        )
        OutlinedButton(onClick = { s.panic() }, modifier = Modifier.padding(start = 10.dp)) { Text("Panic") }
    }
}
