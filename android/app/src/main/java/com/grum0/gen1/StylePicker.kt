package com.grum0.gen1

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp

/** The style, chosen in two steps: its family (HIP, FNK, ...), then the style, each with the tempos it sits at. */
@Composable
fun StylePicker(s: Store) {
    val cur = s.vals[F.STYLE].coerceIn(0, s.styleNames.size - 1)
    var open by remember { mutableStateOf(false) }
    var cat by remember { mutableIntStateOf(s.styleCat[cur]) }
    OutlinedButton(
        onClick = {
            cat = s.styleCat[cur]
            open = true
        },
        modifier = Modifier.fillMaxWidth(),
    ) {
        Text("${s.catCodes[s.styleCat[cur]]} · ${s.styleNames[cur]} · ${s.styleLo[cur]}–${s.styleHi[cur]} BPM", maxLines = 1)
    }
    if (!open) return
    AlertDialog(
        onDismissRequest = { open = false },
        confirmButton = { TextButton(onClick = { open = false }) { Text("Close") } },
        title = { Text("Style") },
        text = {
            Column(Modifier.fillMaxWidth().verticalScroll(rememberScrollState())) {
                ChipGrid(s.catCodes, { it == cat }, 3) { cat = it }
                Text(
                    s.catNames[cat],
                    Modifier.padding(top = 10.dp, bottom = 4.dp),
                    style = MaterialTheme.typography.titleSmall,
                    color = Accent,
                )
                for (i in s.styleNames.indices) {
                    if (s.styleCat[i] == cat) {
                        val here = i == cur
                        Row(
                            Modifier
                                .fillMaxWidth()
                                .padding(vertical = 2.dp)
                                .background(if (here) Color(0xFF2A2A31) else Color.Transparent, RoundedCornerShape(8.dp))
                                .clickable {
                                    s.set(F.STYLE, i)
                                    open = false
                                }
                                .padding(horizontal = 10.dp, vertical = 12.dp),
                            verticalAlignment = Alignment.CenterVertically,
                        ) {
                            Text(
                                s.styleNames[i],
                                Modifier.weight(1f),
                                style = MaterialTheme.typography.bodyLarge,
                                fontWeight = if (here) FontWeight.Bold else FontWeight.Normal,
                            )
                            Text("${s.styleLo[i]}–${s.styleHi[i]}", style = MaterialTheme.typography.bodyMedium, color = Mint)
                        }
                    }
                }
            }
        },
    )
}
