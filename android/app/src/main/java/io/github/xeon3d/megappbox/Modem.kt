package io.github.xeon3d.megappbox

import android.app.Activity
import android.app.AlertDialog
import android.text.InputType
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.Switch
import android.widget.TextView
import android.widget.Toast

/**
 * The modem on COM2 (the ActionTec 56K of the TournaMAXX / MegaNET kits), as the desktop's
 * Tools > Modem on COM2 and Modem settings.  There is no telephone network: a dial connects
 * to a TCP host instead - a TournaMAXX-Revival server, say - and with no host the line is
 * dead.  Fitted per image (from Diamond on it starts fitted); the host is the same for
 * every image.  It all takes effect at once, as on the desktop: the modem plugs in or out
 * with the cabinet running, and a release that looked for it at boot notices at its next
 * one - so a restart is offered.
 */
object Modem {
    fun show(a: Activity, onApplied: () -> Unit) {
        val m = Native.nativeModemGet()
        val dp = a.resources.displayMetrics.density
        fun px(v: Int) = (v * dp).toInt()

        val fitted = Switch(a).apply {
            text = "Modem on COM2: ActionTec 56K"
            isChecked = m[0] == "1"
        }
        val host = EditText(a).apply {
            hint = "Host a dial connects to (empty: no line)"
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_URI
            setText(m[1])
            isSingleLine = true
        }
        val port = EditText(a).apply {
            hint = "Port"
            inputType = InputType.TYPE_CLASS_NUMBER
            setText(m[2])
            isSingleLine = true
        }
        val sounds = Switch(a).apply {
            text = "Modem sounds (dialling, the handshake)"
            isChecked = m[3] == "1"
            setPadding(0, px(8), 0, 0)
        }
        val note = TextView(a).apply {
            text = "Whatever number the cabinet dials, the call goes to this host: a " +
                "TournaMAXX-Revival server (its modem port, 2323 by default) on this network " +
                "or the Internet. Changes take effect at once."
            setPadding(0, px(12), 0, 0)
        }

        fun refresh() {
            host.isEnabled = fitted.isChecked
            port.isEnabled = fitted.isChecked
        }
        fitted.setOnCheckedChangeListener { _, _ -> refresh() }
        refresh()

        val body = LinearLayout(a).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(px(24), px(8), px(24), 0)
            addView(fitted)
            addView(host)
            addView(port)
            addView(sounds)
            addView(note)
        }
        AlertDialog.Builder(a)
            .setTitle("Modem")
            .setView(ScrollView(a).apply { addView(body) })
            .setPositiveButton("Apply") { _, _ ->
                val p = port.text.toString().trim().toIntOrNull()
                if (fitted.isChecked && host.text.isNotBlank() && (p == null || p !in 1..32767)) {
                    Toast.makeText(a, "The port is a number from 1 to 32767.", Toast.LENGTH_LONG).show()
                    return@setPositiveButton
                }
                val plugged = Native.nativeModemSet(fitted.isChecked, host.text.toString().trim(), p ?: 23, sounds.isChecked)
                onApplied()
                if (plugged) offerRestart(a, fitted.isChecked)
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    private fun offerRestart(a: Activity, on: Boolean) {
        AlertDialog.Builder(a)
            .setTitle(if (on) "The modem is connected to COM2." else "The modem is disconnected from COM2.")
            .setMessage("If the cabinet has already finished booting, it may not notice until it restarts. Restart it now?")
            .setPositiveButton("Restart") { _, _ -> Native.nativeHardReset() }
            .setNegativeButton("Not now", null)
            .show()
    }
}
