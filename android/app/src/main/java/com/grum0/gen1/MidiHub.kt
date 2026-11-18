package com.grum0.gen1

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import androidx.core.content.ContextCompat
import android.content.SharedPreferences
import android.media.midi.MidiDevice
import android.media.midi.MidiDeviceInfo
import android.media.midi.MidiManager
import android.media.midi.MidiOutputPort
import android.media.midi.MidiReceiver
import android.os.Handler
import android.os.Looper
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue

/**
 * The phone's MIDI interface. Messages go out through one device's input port (the native side holds it, so it can
 * stamp each message with its time); the clock from the MPC comes in through another (or the same) device's output port.
 */
class MidiHub(private val ctx: Context, private val prefs: SharedPreferences) {
    private val mgr = ctx.getSystemService(Context.MIDI_SERVICE) as MidiManager
    private val handler = Handler(Looper.getMainLooper())

    val devices = mutableStateListOf<MidiDeviceInfo>()
    var sendName by mutableStateOf<String?>(prefs.getString("send_name", null))
    var sendPort by mutableIntStateOf(prefs.getInt("send_port", 0))
    var recvName by mutableStateOf<String?>(prefs.getString("recv_name", null))
    var recvPort by mutableIntStateOf(prefs.getInt("recv_port", 0))
    var sendStatus by mutableStateOf("Not connected")
    var recvStatus by mutableStateOf("Not connected")
    var sendOk by mutableStateOf(false)           // the output port is open
    /** The phone is plugged into a USB host (the MPC) as a device, and its USB is not set to MIDI (so: charging only). */
    var usbNotMidi by mutableStateOf(false)

    private var outDev: MidiDevice? = null
    private var inDev: MidiDevice? = null
    private var inPort: MidiOutputPort? = null
    private var openingOut = false
    private var openingIn = false
    private var started = false
    var onOutOpen: (() -> Unit)? = null           // called on the main thread when the output port has just opened

    private val receiver = object : MidiReceiver() {
        override fun onSend(msg: ByteArray, offset: Int, count: Int, timestamp: Long) {
            Native.nMidiIn(msg, offset, count, timestamp)
        }
    }

    fun nameOf(info: MidiDeviceInfo): String =
        info.properties.getString(MidiDeviceInfo.PROPERTY_NAME) ?: "MIDI device ${info.id}"

    @Suppress("DEPRECATION")
    fun start() {
        if (started) return
        started = true
        mgr.registerDeviceCallback(object : MidiManager.DeviceCallback() {
            override fun onDeviceAdded(device: MidiDeviceInfo) {
                refresh()
                autoConnect()
            }

            override fun onDeviceRemoved(device: MidiDeviceInfo) {
                if (outDev?.info?.id == device.id) closeOut()
                if (inDev?.info?.id == device.id) closeIn()
                refresh()
                autoConnect()
            }
        }, handler)
        // Android tells every app what its USB port is doing as a device: plugged into a host or not, and as what.
        // (Plugged into the MPC, the phone falls back to charging only after a replug or a lock: then no MIDI passes.)
        runCatching {
            val sticky = ContextCompat.registerReceiver(ctx, object : BroadcastReceiver() {
                override fun onReceive(c: Context, i: Intent) = usbState(i)
            }, IntentFilter("android.hardware.usb.action.USB_STATE"), ContextCompat.RECEIVER_EXPORTED)
            if (sticky != null) usbState(sticky)
        }
        refresh()
        autoConnect()
    }

    private fun usbState(i: Intent) {
        val connected = i.getBooleanExtra("connected", false)
        val midi = i.getBooleanExtra("midi", false)
        usbNotMidi = connected && !midi
        if (connected && midi) handler.postDelayed({
            refresh()
            autoConnect()
        }, 700)
    }

    @Suppress("DEPRECATION")
    fun refresh() {
        devices.clear()
        devices.addAll(mgr.devices.toList())
    }

    private fun find(name: String?, forSending: Boolean): MidiDeviceInfo? {
        if (name == null) return null
        return devices.firstOrNull {
            nameOf(it) == name && (if (forSending) it.inputPortCount > 0 else it.outputPortCount > 0)
        }
    }

    fun autoConnect() {
        if (sendName == null && recvName == null) {          // first run: one USB interface is the obvious choice
            val usb = devices.filter { it.type == MidiDeviceInfo.TYPE_USB }
            if (usb.size == 1) {
                val n = nameOf(usb[0])
                if (usb[0].inputPortCount > 0) sendName = n
                if (usb[0].outputPortCount > 0) recvName = n
                savePrefs()
            }
        }
        val s = find(sendName, true)
        if (outDev == null) {
            if (s != null) openOut(s, sendPort)
            else sendStatus = if (sendName == null) "No interface chosen" else "${sendName} is not plugged in"
        }
        val r = find(recvName, false)
        if (inPort == null) {
            if (r != null) openIn(r, recvPort)
            else recvStatus = if (recvName == null) "No interface chosen" else "${recvName} is not plugged in"
        }
    }

    private fun openOut(info: MidiDeviceInfo, port: Int) {
        if (openingOut) return
        openingOut = true
        sendStatus = "Connecting to ${nameOf(info)}…"
        mgr.openDevice(info, { dev ->
            openingOut = false
            if (dev == null) {
                sendStatus = "Could not open ${nameOf(info)}"
                return@openDevice
            }
            // a late answer for a device no longer chosen, or gone meanwhile: drop it and open what is chosen now
            if (nameOf(info) != sendName || port != sendPort || devices.none { it.id == info.id } || outDev != null) {
                runCatching { dev.close() }
                handler.post { autoConnect() }
                return@openDevice
            }
            val r = Native.nOpenOut(dev, port)
            if (r == 0) {
                outDev = dev
                sendOk = true
                sendStatus = "Sending to ${nameOf(info)}, port ${port + 1}"
                onOutOpen?.invoke()
            } else {
                dev.close()
                sendStatus = "Port ${port + 1} of ${nameOf(info)} is busy or missing (error $r)"
            }
        }, handler)
    }

    private fun openIn(info: MidiDeviceInfo, port: Int) {
        if (openingIn) return
        openingIn = true
        recvStatus = "Connecting to ${nameOf(info)}…"
        mgr.openDevice(info, { dev ->
            openingIn = false
            if (dev == null) {
                recvStatus = "Could not open ${nameOf(info)}"
                return@openDevice
            }
            if (nameOf(info) != recvName || port != recvPort || devices.none { it.id == info.id } || inPort != null) {
                runCatching { dev.close() }
                handler.post { autoConnect() }
                return@openDevice
            }
            val p = dev.openOutputPort(port)
            if (p == null) {
                dev.close()
                recvStatus = "Port ${port + 1} of ${nameOf(info)} is missing"
                return@openDevice
            }
            p.connect(receiver)
            inDev = dev
            inPort = p
            recvStatus = "Listening to ${nameOf(info)}, port ${port + 1}"
        }, handler)
    }

    private fun closeOut() {
        Native.nCloseOut()
        runCatching { outDev?.close() }
        outDev = null
        sendOk = false
        sendStatus = "Not connected"
    }

    private fun closeIn() {
        runCatching { inPort?.disconnect(receiver) }
        runCatching { inPort?.close() }
        runCatching { inDev?.close() }
        inPort = null
        inDev = null
        recvStatus = "Not connected"
    }

    fun chooseSend(name: String?, port: Int) {
        closeOut()
        sendName = name
        sendPort = port
        savePrefs()
        refresh()
        autoConnect()
    }

    fun chooseRecv(name: String?, port: Int) {
        closeIn()
        recvName = name
        recvPort = port
        savePrefs()
        refresh()
        autoConnect()
    }

    private fun savePrefs() {
        prefs.edit()
            .putString("send_name", sendName).putInt("send_port", sendPort)
            .putString("recv_name", recvName).putInt("recv_port", recvPort)
            .apply()
    }
}
