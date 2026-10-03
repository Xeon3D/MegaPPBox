package io.github.xeon3d.megappbox

import android.app.Activity
import android.app.AlertDialog
import android.content.Context
import android.net.ConnectivityManager
import android.text.InputType
import android.view.View
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.RadioButton
import android.widget.RadioGroup
import android.widget.ScrollView
import android.widget.Switch
import android.widget.TextView
import android.widget.Toast
import java.net.Inet4Address

/**
 * Mega-Link: the cabinet's network card (the desktop's Network dialog, cut down to what
 * Android can do).  The card is on the switch: either the local one - every cabinet on
 * the same network with the same link name, over UDP multicast, as the desktop's Local
 * Switch - or one other cabinet directly, by its address, for networks that drop
 * multicast.  Fitting or removing the card restarts the cabinet.
 */
object MegaLink {
    private val cardNames = mapOf(
        "te16xp" to "TRENDnet TE-16XP (ISA)",
        "rtl8139c+" to "Realtek RTL8139 (PCI)",
    )

    fun show(a: Activity, onApplied: () -> Unit) {
        val n = Native.nativeNetworkGet()
        if (n[0] != "1") {
            Toast.makeText(a, "XL cabinets had no network card: Mega-Link is for MAXX.", Toast.LENGTH_LONG).show()
            return
        }
        val dp = a.resources.displayMetrics.density
        fun px(v: Int) = (v * dp).toInt()

        val fitted = Switch(a).apply {
            text = "Network card: ${cardNames[n[5]] ?: n[5]}"
            isChecked = n[1] == "1"
        }
        val local = RadioButton(a).apply { id = View.generateViewId(); text = "Same Wi-Fi (the local switch)" }
        val direct = RadioButton(a).apply { id = View.generateViewId(); text = "Direct to one cabinet" }
        val how = RadioGroup(a).apply {
            addView(local)
            addView(direct)
            check(if (n[2] == "rswitch") direct.id else local.id)
        }
        val host = EditText(a).apply {
            hint = "The other cabinet's address (e.g. 192.168.1.20)"
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_URI
            setText(n[3])
            isSingleLine = true
        }
        val secret = EditText(a).apply {
            hint = "Link name (the same on every cabinet; may be empty)"
            setText(n[4])
            isSingleLine = true
        }
        val here = wifiAddress(a)
        val note = TextView(a).apply {
            text = (if (here != null) "This phone: $here\n" else "This phone is not on Wi-Fi.\n") +
                "Every cabinet in one Mega-Link needs the same link name. A PC cabinet joins with " +
                "Tools > Network settings: Local Switch, the same link name. The cabinet restarts to fit the card."
            setPadding(0, px(12), 0, 0)
        }

        fun refresh() {
            val on = fitted.isChecked
            local.isEnabled = on
            direct.isEnabled = on
            secret.isEnabled = on
            host.isEnabled = on
            host.visibility = if (how.checkedRadioButtonId == direct.id) View.VISIBLE else View.GONE
        }
        fitted.setOnCheckedChangeListener { _, _ -> refresh() }
        how.setOnCheckedChangeListener { _, _ -> refresh() }
        refresh()

        val body = LinearLayout(a).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(px(24), px(8), px(24), 0)
            addView(fitted)
            addView(how)
            addView(host)
            addView(secret)
            addView(note)
        }
        AlertDialog.Builder(a)
            .setTitle("Mega-Link")
            .setView(ScrollView(a).apply { addView(body) })
            .setPositiveButton("Apply and restart") { _, _ ->
                val remote = how.checkedRadioButtonId == direct.id
                if (fitted.isChecked && remote && host.text.isBlank()) {
                    Toast.makeText(a, "Give the other cabinet's address.", Toast.LENGTH_LONG).show()
                    return@setPositiveButton
                }
                Native.nativeNetworkSet(fitted.isChecked, remote, host.text.toString().trim(), secret.text.toString())
                onApplied()
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    /** The phone's IPv4 address on its current network (for a direct link from the other side). */
    private fun wifiAddress(c: Context): String? {
        val cm = c.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
        val lp = cm.getLinkProperties(cm.activeNetwork) ?: return null
        return lp.linkAddresses.map { it.address }.firstOrNull { it is Inet4Address && !it.isLoopbackAddress }?.hostAddress
    }
}
