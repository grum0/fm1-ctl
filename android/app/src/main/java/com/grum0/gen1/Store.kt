package com.grum0.gen1

import android.content.Context
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue

/** A piece of hardware on the MIDI splitter: its profile in the generators and the channel it listens on. */
data class Instrument(val key: String, val name: String, val dev: Int, val defChan: Int)

// The channels as Grumo's instruments are set (2026-11-15; the JV-1010's two 2026-11-17).
// New instruments go at the end. Channels and choices are saved by key, so the list can grow.
val INSTRUMENTS = listOf(
    Instrument("tanzbar", "Tanzbär Lite", F.DV_TANZBAR, 4),
    Instrument("mbase", "Jomox MBase 11", F.DV_MBASE, 8),
    Instrument("mb33", "MAM MB-33", F.DV_MB33, 12),
    Instrument("hades", "Dreadbox Hades", F.DV_HADES, 9),
    Instrument("monologue", "Korg Monologue", F.DV_MONOLOGUE, 3),
    Instrument("jv1010", "Roland JV-1010", F.DV_JV1010, 13),
    Instrument("jv1010b", "Roland JV-1010, 2nd part", F.DV_JV1010, 7),   // (it is multitimbral: a second part, a second channel)
    Instrument("volca", "Volca FM", F.DV_VOLCAFM, 2),
    Instrument("other", "Other (generic)", F.DV_GENERIC, 16),
)

/** The first build's placeholder channels: saved channels equal to these were never set by hand, and give way to the real ones. */
val OLD_DEFAULT_CHAN = listOf(4, 8, 1, 2, 3, 6, 7, 5, 16)
const val RIG_VERSION = 3

/** The JV-1010's placeholder channel before 0.2.1: a saved 6 on its first part was never set by hand. */
const val OLD_JV_CHAN = 6

/** The firmware's own pairing: drums Tanzbär, chords JV-1010, arp Volca FM, bass MB-33, lead Monologue, kick MBase, Marble JV-1010 (2nd part). */
val DEFAULT_TARGET = listOf(0, 5, 7, 2, 4, 1, 6)

val LANE_DEFAULT = listOf(36, 37, 38, 41, 43, 39, 40, 40, 44, 44, 45, 46, 47, 0, 1)

/** The Tanzbär Lite's controllers, voice by voice (the FM-1 firmware's banks E to L). */
class TanzVoice(val name: String, val knobs: List<Pair<String, Int>>)

val TANZ = listOf(
    TanzVoice("Bass drum", listOf("Tone" to 2, "Tune" to 3, "Decay" to 64)),
    TanzVoice("Snare", listOf("Tone" to 11, "Noise" to 13, "Decay" to 67)),
    TanzVoice("Claps", listOf("Filter" to 18, "Decay" to 75, "Attack" to 76, "Trigger" to 77)),
    TanzVoice("Toms", listOf("Tune" to 19, "Decay" to 20, "Pan" to 73)),
    TanzVoice("Congas", listOf("Tune" to 79, "Decay" to 78, "Pan" to 82)),
    TanzVoice("Cowbell", listOf("Tune" to 84, "Decay" to 85)),
    TanzVoice("Claves", listOf("Tune" to 86, "Decay" to 87)),
    TanzVoice("Cymbal", listOf("Tune" to 89, "Decay" to 88, "Tone" to 92)),
    TanzVoice("Hi-hats", listOf("Closed decay" to 91, "Open decay" to 90)),
)

/** Everything the screens read and change. The settings live in the native engine; `vals` mirrors them for Compose. */
class Store(ctx: Context) {
    private val app = ctx.applicationContext
    private val prefs = ctx.getSharedPreferences("gen1", Context.MODE_PRIVATE)
    private val main = Handler(Looper.getMainLooper())
    val hub = MidiHub(ctx, prefs)

    val vals = mutableStateListOf<Int>()
    val lo = IntArray(F.COUNT)
    val hi = IntArray(F.COUNT)

    val styleNames: List<String>
    val devNames: List<String>
    val partNames: List<String>
    val laneNames: List<String>
    val dsetNames: List<String>
    val modeNames: List<List<String>>
    val catNames: List<String>                  // the styles' families ("Hip-hop") and their three letters ("HIP")
    val catCodes: List<String>
    val styleCat: List<Int>                     // each style's family, and the tempos it sits at
    val styleLo: List<Int>
    val styleHi: List<Int>
    val version: String = runCatching { ctx.packageManager.getPackageInfo(ctx.packageName, 0).versionName }.getOrNull() ?: "?"
    var keepAwake by mutableStateOf(true)       // the screen stays on while the app is up

    val chan = mutableStateListOf<Int>()        // each instrument's MIDI channel, 1..16
    val target = mutableStateListOf<Int>()      // each part's instrument
    var keysTarget by mutableIntStateOf(0)      // the keyboard and the arpeggiator play this instrument
    var seqTarget by mutableIntStateOf(0)       // and the four-track sequencer this one
    var keyOct by mutableIntStateOf(0)
    var ccTarget by mutableIntStateOf(0)        // the controller sliders on the Control page
    val ccVals = mutableStateMapOf<String, Int>()  // and where they were left: "instrument key:cc" -> value
    val tanz = mutableStateMapOf<Int, Int>()    // the Tanzbär's controllers: number -> value

    // what the engine is doing, polled
    var running by mutableStateOf(false)
    var bar by mutableIntStateOf(0)
    var step by mutableIntStateOf(0)
    var phraseBars by mutableIntStateOf(1)
    var beat by mutableStateOf(false)
    var heard by mutableIntStateOf(0)
    var bpm10 by mutableIntStateOf(1200)
    var clockSeen by mutableStateOf(true)
    var chordText by mutableStateOf("")
    val lanes = mutableStateListOf<Int>()
    private val st = IntArray(8)

    private var dirty = false
    private var dirtyAt = 0L

    init {
        Native.nInit()
        val r = IntArray(2)
        for (id in 0 until F.COUNT) {
            Native.nRange(id, r)
            lo[id] = r[0]
            hi[id] = r[1]
            vals.add(Native.nGet(id))
        }
        styleNames = List(Native.nCount(0)) { Native.nName(0, it) }
        devNames = List(Native.nCount(1)) { Native.nName(1, it) }
        partNames = List(Native.nCount(2)) { Native.nName(2, it) }
        laneNames = List(Native.nCount(3)) { Native.nName(3, it) }
        dsetNames = List(Native.nCount(4)) { Native.nName(4, it) }
        modeNames = List(F.NPARTS) { p -> List(Native.nModeCount(p)) { m -> Native.nName(5, (p shl 8) or m) } }
        catNames = List(F.NCATS) { Native.nName(6, it) }
        catCodes = List(F.NCATS) { Native.nName(7, it) }
        val info = IntArray(3)
        val cats = ArrayList<Int>()
        val los = ArrayList<Int>()
        val his = ArrayList<Int>()
        for (i in styleNames.indices) {
            Native.nStyleInfo(i, info)
            cats.add(info[0].coerceIn(0, F.NCATS - 1))
            los.add(info[1])
            his.add(info[2])
        }
        styleCat = cats
        styleLo = los
        styleHi = his
        INSTRUMENTS.forEach { chan.add(it.defChan) }
        DEFAULT_TARGET.forEach { target.add(it) }
        repeat(F.NLANES) { lanes.add(0) }
        hub.onOutOpen = { sendTanzAll() }
    }

    // ------------------------------------------------------------------ settings --
    fun refreshAll() {
        for (id in 0 until F.COUNT) {
            val v = Native.nGet(id)
            if (vals[id] != v) vals[id] = v
        }
    }

    fun set(id: Int, v: Int) {
        Native.nSet(id, v)
        refreshAll()
        touch()
    }

    fun touch() {
        dirty = true
        dirtyAt = SystemClock.elapsedRealtime()
    }

    /** Push each part's instrument (its channel and profile), and the keyboard's and the sequencer's channels, to the engine. */
    fun applyRig() {
        for (p in 0 until F.NPARTS) {
            val inst = INSTRUMENTS[target[p]]
            Native.nSet(F.part(p, F.PF_CHAN), chan[target[p]] - 1)
            Native.nSet(F.part(p, F.PF_DEV), inst.dev)
        }
        Native.nSet(F.KEYS_CHAN, chan[keysTarget] - 1)
        Native.nSet(F.SEQ_CH, chan[seqTarget] - 1)
        refreshAll()
        touch()
    }

    fun setChan(inst: Int, c: Int) {
        chan[inst] = c.coerceIn(1, 16)
        applyRig()
    }

    fun setTarget(part: Int, inst: Int) {
        target[part] = inst
        applyRig()
    }

    fun chooseKeysTarget(inst: Int) {
        keysTarget = inst
        applyRig()
    }

    fun chooseSeqTarget(inst: Int) {
        seqTarget = inst
        applyRig()
    }

    fun newSeed() = set(F.SEED, (0..999).random())

    fun resetLaneNotes() {
        for (l in 0 until F.NLANES) Native.nSet(F.LANE0 + l, LANE_DEFAULT[l])
        refreshAll()
        touch()
    }

    fun resetAll() {
        Native.nDefaults()
        INSTRUMENTS.forEachIndexed { i, ins -> chan[i] = ins.defChan }
        DEFAULT_TARGET.forEachIndexed { p, t -> target[p] = t }
        keysTarget = 0
        seqTarget = 0
        ccTarget = 0
        ccVals.clear()
        tanz.clear()
        applyRig()
    }

    // ------------------------------------------------------------------ transport --
    fun togglePlay() {
        // the service keeps the clock going with the screen off; after "Stop and quit" it has to come back with Play
        if (!running) ClockService.start(app)
        Native.nPlay(if (running) 0 else 1)
    }
    fun panic() = Native.nPanic()

    // ------------------------------------------------------------------ playing --
    fun keyDown(note: Int) {
        if (vals[F.ARP_ON] == 1) Native.nArp(note, 1)
        else Native.nSend(0x90 or (chan[keysTarget] - 1), note, vals[F.VEL])
    }

    fun keyUp(note: Int) {
        if (vals[F.ARP_ON] == 1) Native.nArp(note, 0)
        else Native.nSend(0x80 or (chan[keysTarget] - 1), note, 0)
    }

    fun sendCc(inst: Int, cc: Int, v: Int) = Native.nSend(0xB0 or (chan[inst] - 1), cc, v.coerceIn(0, 127))

    fun setTanz(cc: Int, v: Int) {
        tanz[cc] = v
        sendCc(0, cc, v)
        touch()
    }

    fun tanzValue(cc: Int): Int = tanz[cc] ?: 64

    /** The Tanzbär's controllers as last left, sent again when the MIDI output opens, so the unit matches the sliders. */
    fun sendTanzAll() = tanz.forEach { (cc, v) -> sendCc(0, cc, v) }

    fun ccValue(inst: Int, cc: Int): Int = ccVals["${INSTRUMENTS[inst].key}:$cc"] ?: if (cc == 7) 100 else 64

    fun setCc(inst: Int, cc: Int, v: Int) {
        ccVals["${INSTRUMENTS[inst].key}:$cc"] = v
        sendCc(inst, cc, v)
        touch()
    }

    fun chooseCcTarget(inst: Int) {
        ccTarget = inst
        touch()
    }

    fun sendBend(inst: Int, v: Int) {                       // v: 0..16383, 8192 the middle
        val c = v.coerceIn(0, 16383)
        Native.nSend(0xE0 or (chan[inst] - 1), c and 127, c shr 7)
    }

    /** A short note on the instrument's channel, to hear that the wiring and the channel are right. */
    fun blip(inst: Int) {
        val ch = chan[inst] - 1
        val note = if (INSTRUMENTS[inst].dev == F.DV_TANZBAR) vals[F.LANE0 + F.L_KICK] else 48
        Native.nSend(0x90 or ch, note, 100)
        main.postDelayed({ Native.nSend(0x80 or ch, note, 0) }, 250)
    }

    // ------------------------------------------------------------------ status --
    fun poll() {
        Native.nStatus(st)
        running = st[0] == 1
        bar = st[1]
        step = st[2]
        phraseBars = st[3]
        beat = st[4] == 1
        heard = st[5]
        bpm10 = st[6]
        clockSeen = st[7] == 1
        chordText = Native.nChordText()
        for (l in 0 until F.NLANES) {
            val b = Native.nLaneSteps(l)
            if (lanes[l] != b) lanes[l] = b
        }
        if (dirty && SystemClock.elapsedRealtime() - dirtyAt > 1500) save()
    }

    // ------------------------------------------------------------------ saving --
    fun save() {
        dirty = false
        prefs.edit()
            .putString("fields", vals.joinToString(","))
            .putString("chan_by_key", INSTRUMENTS.indices.joinToString(",") { "${INSTRUMENTS[it].key}=${chan[it]}" })
            .putString("target_keys", target.joinToString(",") { INSTRUMENTS[it].key })
            .putString("keys_key", INSTRUMENTS[keysTarget].key)
            .putString("seq_key", INSTRUMENTS[seqTarget].key)
            .putString("cc_key", INSTRUMENTS[ccTarget].key)
            .putString("cc_vals", ccVals.entries.joinToString(",") { "${it.key}=${it.value}" })
            .putInt("keyOct", keyOct)
            .putInt("rig_version", RIG_VERSION)
            .putBoolean("keepAwake", keepAwake)
            .putString("tanz", tanz.entries.joinToString(",") { "${it.key}=${it.value}" })
            .apply()
    }

    private fun ints(s: String): List<Int> = s.split(",").mapNotNull { it.trim().toIntOrNull() }

    private fun pairs(s: String?): List<Pair<String, String>> =
        s?.split(",")?.mapNotNull { e -> e.split("=").takeIf { it.size == 2 }?.let { it[0].trim() to it[1].trim() } } ?: emptyList()

    private fun instIndex(key: String?): Int? = INSTRUMENTS.indexOfFirst { it.key == key }.takeIf { it >= 0 }

    /** An instrument chosen somewhere: saved by key from 0.2.1, as a position in the list before it. */
    private fun loadInst(keyName: String, oldName: String): Int =
        instIndex(prefs.getString(keyName, null))
            ?: prefs.getInt(oldName, 0).coerceIn(0, INSTRUMENTS.size - 1)

    fun load() {
        prefs.getString("fields", null)?.let { s ->
            val a = ints(s)
            // settings added since go at the end (F.HAT_DENS on): a shorter list from an older build is still good
            if (a.size in F.HAT_DENS..F.COUNT) a.forEachIndexed { id, v -> Native.nSet(id, v) }
        }
        val rigV = prefs.getInt("rig_version", 1)
        val byKey = pairs(prefs.getString("chan_by_key", null))
        if (byKey.isNotEmpty()) {
            // by key: an instrument added since keeps its default, one taken away is skipped
            for ((k, v) in byKey) {
                val i = instIndex(k) ?: continue
                v.toIntOrNull()?.let { chan[i] = it.coerceIn(1, 16) }
            }
        } else prefs.getString("chan", null)?.let { s ->
            // before 0.2.1: by position, in the list as it was then (the first instruments are in the same order)
            val a = ints(s)
            if (!(rigV < 2 && a == OLD_DEFAULT_CHAN))
                a.take(INSTRUMENTS.size).forEachIndexed { i, v -> chan[i] = v.coerceIn(1, 16) }
        }
        val jv = instIndex("jv1010")
        if (rigV < 3 && jv != null && chan[jv] == OLD_JV_CHAN) chan[jv] = INSTRUMENTS[jv].defChan
        keepAwake = prefs.getBoolean("keepAwake", true)
        val tk = prefs.getString("target_keys", null)?.split(",")
        if (tk != null && tk.size == F.NPARTS) {
            tk.forEachIndexed { p, k -> instIndex(k.trim())?.let { target[p] = it } }
        } else prefs.getString("target", null)?.let { s ->
            val a = ints(s)
            if (a.size == F.NPARTS) a.forEachIndexed { p, v -> target[p] = v.coerceIn(0, INSTRUMENTS.size - 1) }
        }
        keysTarget = loadInst("keys_key", "keysTarget")
        seqTarget = loadInst("seq_key", "seqTarget")
        ccTarget = instIndex(prefs.getString("cc_key", null)) ?: 0
        for ((k, v) in pairs(prefs.getString("cc_vals", null))) {
            val n = v.toIntOrNull() ?: continue
            if (instIndex(k.substringBefore(":")) != null) ccVals[k] = n.coerceIn(0, 127)
        }
        keyOct = prefs.getInt("keyOct", 0).coerceIn(-2, 2)
        prefs.getString("tanz", null)?.split(",")?.forEach { e ->
            val kv = e.split("=")
            val k = kv.getOrNull(0)?.toIntOrNull()
            val v = kv.getOrNull(1)?.toIntOrNull()
            if (k != null && v != null) tanz[k] = v.coerceIn(0, 127)
        }
        applyRig()
        dirty = false
        if (rigV < RIG_VERSION) save()
    }
}
