package com.grum0.gen1

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import kotlin.math.roundToInt

val Accent = Color(0xFFFF6B3D)
val Mint = Color(0xFF7FD6C2)
val Panel = Color(0xFF17171B)

@Composable
fun Gen1Theme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = darkColorScheme(
            primary = Accent,
            secondary = Mint,
            background = Color(0xFF0B0B0D),
            surface = Color(0xFF111114),
            surfaceVariant = Panel,
            onBackground = Color(0xFFEDEDED),
            onSurface = Color(0xFFEDEDED),
        ),
        content = content,
    )
}

@Composable
fun Section(title: String, content: @Composable ColumnScope.() -> Unit) {
    Column(
        Modifier
            .fillMaxWidth()
            .padding(vertical = 6.dp)
            .background(Panel, RoundedCornerShape(10.dp))
            .padding(12.dp),
    ) {
        Text(title, style = MaterialTheme.typography.titleSmall, color = Accent)
        Spacer(Modifier.size(4.dp))
        content()
    }
}

/** A setting from the engine as a slider with its value on the right. */
@Composable
fun SliderRow(
    s: Store,
    label: String,
    id: Int,
    fmt: (Int) -> String = { it.toString() },
    lo: Int = s.lo[id],
    hi: Int = s.hi[id],
) {
    if (hi <= lo) return
    val v = s.vals[id]
    Column(Modifier.fillMaxWidth()) {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
            Text(label, style = MaterialTheme.typography.bodyMedium)
            Text(fmt(v), style = MaterialTheme.typography.bodyMedium, color = Mint)
        }
        Slider(
            value = v.toFloat().coerceIn(lo.toFloat(), hi.toFloat()),
            onValueChange = { s.set(id, it.roundToInt()) },
            valueRange = lo.toFloat()..hi.toFloat(),
        )
    }
}

/** A controller slider: not a setting, a number sent straight out. */
@Composable
fun ValueSlider(label: String, value: Int, onChange: (Int) -> Unit, max: Int = 127, note: String = "") {
    Column(Modifier.fillMaxWidth()) {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
            Text(label, style = MaterialTheme.typography.bodyMedium)
            Text(
                if (note.isEmpty()) value.toString() else "$value  $note",
                style = MaterialTheme.typography.bodyMedium,
                color = Mint,
            )
        }
        Slider(
            value = value.toFloat(),
            onValueChange = { onChange(it.roundToInt().coerceIn(0, max)) },
            valueRange = 0f..max.toFloat(),
        )
    }
}

/** A setting with minus and plus. */
@Composable
fun StepRow(
    s: Store,
    label: String,
    id: Int,
    fmt: (Int) -> String = { it.toString() },
) {
    Row(
        Modifier
            .fillMaxWidth()
            .padding(vertical = 2.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(label, Modifier.weight(1f), style = MaterialTheme.typography.bodyMedium)
        Stepper(fmt(s.vals[id]), { s.set(id, s.vals[id] - 1) }, { s.set(id, s.vals[id] + 1) })
    }
}

@Composable
fun Stepper(text: String, onMinus: () -> Unit, onPlus: () -> Unit) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        OutlinedButton(onClick = onMinus, contentPadding = androidx.compose.foundation.layout.PaddingValues(0.dp), modifier = Modifier.size(40.dp)) {
            Text("−")
        }
        Text(text, Modifier.width(60.dp), color = Mint, style = MaterialTheme.typography.bodyMedium, textAlign = androidx.compose.ui.text.style.TextAlign.Center)
        OutlinedButton(onClick = onPlus, contentPadding = androidx.compose.foundation.layout.PaddingValues(0.dp), modifier = Modifier.size(40.dp)) {
            Text("+")
        }
    }
}

@Composable
fun SwitchRow(label: String, on: Boolean, onChange: (Boolean) -> Unit) {
    Row(
        Modifier
            .fillMaxWidth()
            .padding(vertical = 2.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(label, Modifier.weight(1f), style = MaterialTheme.typography.bodyMedium)
        Switch(checked = on, onCheckedChange = onChange)
    }
}

/** A button that opens a list. */
@Composable
fun Pick(label: String, current: String, options: List<String>, modifier: Modifier = Modifier, onPick: (Int) -> Unit) {
    var open by remember { mutableStateOf(false) }
    Box(modifier) {
        OutlinedButton(onClick = { open = true }, modifier = Modifier.fillMaxWidth()) {
            Text(if (label.isEmpty()) current else "$label: $current", maxLines = 1)
        }
        DropdownMenu(expanded = open, onDismissRequest = { open = false }) {
            options.forEachIndexed { i, name ->
                DropdownMenuItem(text = { Text(name) }, onClick = {
                    open = false
                    onPick(i)
                })
            }
        }
    }
}

/** A row of a setting's choices as chips, in lines of perRow. */
@Composable
fun ChipGrid(items: List<String>, selected: (Int) -> Boolean, perRow: Int, onClick: (Int) -> Unit) {
    items.indices.chunked(perRow).forEach { row ->
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            row.forEach { i ->
                FilterChip(
                    selected = selected(i),
                    onClick = { onClick(i) },
                    label = { Text(items[i], maxLines = 1) },
                    modifier = Modifier.weight(1f),
                )
            }
            repeat(perRow - row.size) { Spacer(Modifier.weight(1f)) }
        }
    }
}

@Composable
fun BigButton(text: String, modifier: Modifier = Modifier, onClick: () -> Unit) {
    Button(onClick = onClick, modifier = modifier) { Text(text) }
}

@Composable
fun SmallButton(text: String, modifier: Modifier = Modifier, onClick: () -> Unit) {
    TextButton(onClick = onClick, modifier = modifier) { Text(text) }
}

fun chanText(c: Int) = "ch $c"
