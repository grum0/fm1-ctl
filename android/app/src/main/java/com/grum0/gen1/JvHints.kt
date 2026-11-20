// SPDX-License-Identifier: GPL-3.0-only
package com.grum0.gen1

/** The line under the style name on Play: which part of the JV-1010's "GEN-1 Keys" performance suits this style's
 *  chords, Marble, arp and lead (JvData, from android/tools/jv1010), for the parts that are on and have one. The
 *  chords and Marble play the JV anyway; the arp and lead are the Volca FM's and Monologue's, hence "try". Only a
 *  suggestion: nothing is sent. */
fun jvHintLine(s: Store): String {
    val st = s.vals[F.STYLE]
    fun on(p: Int) = s.vals[F.part(p, F.PF_ON)] == 1
    fun name(n: Int) = "$n ${JvData.PART.getOrElse(n) { "" }}".trim()
    val out = mutableListOf<String>()
    val chords = JvData.CHORDS.getOrElse(st) { 0 }
    val marble = JvData.MARBLE.getOrElse(st) { 0 }
    val arp = JvData.ARP.getOrElse(st) { 0 }
    val lead = JvData.LEAD.getOrElse(st) { 0 }
    if (chords > 0 && on(F.GP_CHORD)) out += "chords ${name(chords)}"
    if (marble > 0 && on(F.GP_RAND)) out += "Marble ${name(marble)}"
    if (arp > 0 && on(F.GP_ARP)) out += "arp: try JV ${name(arp)}"
    if (lead > 0 && on(F.GP_LEAD)) out += "lead: try JV ${name(lead)}"
    return if (out.isEmpty()) "" else "JV  " + out.joinToString(" · ")
}
