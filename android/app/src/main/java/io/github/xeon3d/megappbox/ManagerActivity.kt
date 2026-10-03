package io.github.xeon3d.megappbox

import android.app.Activity
import android.app.AlertDialog
import android.content.Intent
import android.graphics.Color
import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.provider.OpenableColumns
import android.view.MenuItem
import android.view.View
import android.view.ViewGroup
import android.view.WindowInsets
import android.widget.AdapterView
import android.widget.BaseAdapter
import android.widget.ListView
import android.widget.ProgressBar
import android.widget.TextView
import android.widget.Toast
import android.widget.Toolbar
import java.io.File
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean

/**
 * The Machine Manager: the Megatouch images in the app's own folder (where the emulator
 * can open them), identified from their contents.  A tap runs one, with the profile and
 * key its identification calls for; a long press deletes it.  Import copies an image in
 * from anywhere the system file picker reaches.
 */
class ManagerActivity : Activity() {
    /** One image and what it was identified as. */
    private class Entry(val file: File, id: Array<String>) {
        val runnable = id[0] == "1"
        val release = id[1]
        val version = id[2]
        val profile = id[3].toIntOrNull() ?: -1
        val key = id[4]
        val note = id[5]
        val modem = id.getOrNull(7) == "1"
        val keyFamily = id.getOrNull(8) ?: ""
        val title get() = if (version.isEmpty()) release else "$release $version"
    }

    private val work = Executors.newSingleThreadExecutor()
    private val ui = Handler(Looper.getMainLooper())
    private lateinit var dir: File
    private lateinit var list: ListView
    private lateinit var empty: TextView
    private var entries = listOf<Entry>()
    private lateinit var profileNames: List<String>

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_manager)
        dir = getExternalFilesDir(null) ?: filesDir

        val toolbar = findViewById<Toolbar>(R.id.toolbar)
        list = findViewById(R.id.list)
        empty = findViewById(R.id.empty)
        list.emptyView = empty

        // Edge to edge (Android 15+): keep clear of the system bars and the cut-out.
        findViewById<View>(R.id.root).setOnApplyWindowInsetsListener { _, insets ->
            val bars = insets.getInsets(WindowInsets.Type.systemBars() or WindowInsets.Type.displayCutout())
            toolbar.setPadding(bars.left, bars.top, bars.right, 0)
            list.setPadding(bars.left, 0, bars.right, bars.bottom)
            list.clipToPadding = false
            insets
        }

        toolbar.subtitle = dir.path
        toolbar.menu.add(0, 1, 0, R.string.import_image).apply {
            setIcon(android.R.drawable.ic_menu_add)
            setShowAsAction(MenuItem.SHOW_AS_ACTION_ALWAYS or MenuItem.SHOW_AS_ACTION_WITH_TEXT)
        }
        toolbar.menu.add(0, 4, 1, R.string.keys).setShowAsAction(MenuItem.SHOW_AS_ACTION_NEVER)
        toolbar.menu.add(0, 2, 1, R.string.rescan).setShowAsAction(MenuItem.SHOW_AS_ACTION_NEVER)
        toolbar.menu.add(0, 3, 2, R.string.about).setShowAsAction(MenuItem.SHOW_AS_ACTION_NEVER)
        toolbar.setOnMenuItemClickListener {
            when (it.itemId) {
                1 -> pickImport(REQ_IMPORT)
                4 -> showKeys()
                2 -> scan()
                3 -> About.show(this)
            }
            true
        }

        val p = Native.nativeProfiles()
        profileNames = (p.indices step 2).map { p[it] }

        list.onItemClickListener = AdapterView.OnItemClickListener { _, _, pos, _ -> runImage(entries[pos]) }
        list.onItemLongClickListener = AdapterView.OnItemLongClickListener { _, _, pos, _ ->
            confirmDelete(entries[pos])
            true
        }

        scan()
    }

    override fun onDestroy() {
        work.shutdownNow()
        super.onDestroy()
    }

    private fun dp(v: Int) = (v * resources.displayMetrics.density).toInt()

    /** Identify every .img / .iso in the folder (off the UI thread: it reads the images). */
    private fun scan() {
        empty.text = "Looking at the images…"
        work.execute {
            val files = (dir.listFiles() ?: emptyArray())
                .filter { it.isFile && (it.name.endsWith(".img", true) || it.name.endsWith(".iso", true)) }
            val found = files.map { Entry(it, Native.nativeIdentify(it.path)) }
                .sortedWith(compareBy({ !it.runnable }, { it.release }, { it.version }))
            ui.post {
                entries = found
                empty.setText(R.string.no_images)
                list.adapter = Rows()
            }
        }
    }

    /** Run it as it was identified: its profile, and the built-in key its release uses. */
    private fun runImage(e: Entry) {
        if (!e.runnable) {
            Toast.makeText(this, e.note.ifEmpty { "This image cannot run here." }, Toast.LENGTH_LONG).show()
            return
        }
        if (e.key.isEmpty() && e.keyFamily.isNotEmpty()) {
            AlertDialog.Builder(this)
                .setTitle(e.title)
                .setMessage("There is no ${e.keyFamily} key for this release yet, and it will stop at its key check. " +
                    "Import a dump of one with Import key.")
                .setPositiveButton(R.string.import_key) { _, _ -> pickImport(REQ_IMPORT_KEY) }
                .setNeutralButton("Run anyway") { _, _ -> start(e) }
                .setNegativeButton(android.R.string.cancel, null)
                .show()
            return
        }
        start(e)
    }

    private fun start(e: Entry) {
        startActivity(
            Intent(this, CabinetActivity::class.java)
                .putExtra(CabinetActivity.EXTRA_IMAGE, e.file.path)
                .putExtra(CabinetActivity.EXTRA_PROFILE, e.profile)
                .putExtra(CabinetActivity.EXTRA_KEY, e.key)
                .putExtra(CabinetActivity.EXTRA_TITLE, e.title)
                .putExtra(CabinetActivity.EXTRA_MODEM, e.modem)
        )
        finish()
    }

    /* ---- import: images copied into the folder, key dumps into its keys/ ---- */

    private fun pickImport(req: Int) {
        startActivityForResult(
            Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*")
                .putExtra(Intent.EXTRA_ALLOW_MULTIPLE, req == REQ_IMPORT_KEY),
            req,
        )
    }

    @Deprecated("framework Activity API")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if ((resultCode != RESULT_OK) || (data == null)) return
        val uris = data.clipData?.let { c -> (0 until c.itemCount).map { c.getItemAt(it).uri } } ?: listOfNotNull(data.data)
        // Either picker takes either: a key dump is told by its size.
        val keys = uris.filter { isKeyDump(nameAndSize(it).second) }
        importKeys(keys)
        uris.filter { it !in keys }.firstOrNull()?.let { importImage(it) }
    }

    private fun nameAndSize(uri: Uri): Pair<String, Long> {
        var name = "imported.img"
        var size = -1L
        contentResolver.query(uri, null, null, null, null)?.use { c ->
            if (c.moveToFirst()) {
                c.getColumnIndex(OpenableColumns.DISPLAY_NAME).takeIf { it >= 0 }?.let { name = c.getString(it) ?: name }
                c.getColumnIndex(OpenableColumns.SIZE).takeIf { it >= 0 }?.let { if (!c.isNull(it)) size = c.getLong(it) }
            }
        }
        return name.replace('/', '_') to size
    }

    /** A keyflasher DS1991 dump (264 bytes) or a DS1205 MultiKey (192). */
    private fun isKeyDump(size: Long) = (size == 264L) || (size == 192L)

    /** Copy key dumps into keys/, named for the release each is for (read from the dump). */
    private fun importKeys(uris: List<Uri>) {
        if (uris.isEmpty()) return
        val uri = uris.first()
        val rest = uris.drop(1)
        val name = nameAndSize(uri).first
        val data = try {
            contentResolver.openInputStream(uri)!!.use { it.readBytes() }
        } catch (e: Exception) {
            null
        }
        if ((data == null) || !isKeyDump(data.size.toLong())) {
            Toast.makeText(this, "$name is not a key dump (264 or 192 bytes).", Toast.LENGTH_LONG).show()
            return importKeys(rest)
        }
        val cand = Native.nativeKeyIdentify(data).toList()
        val named = Native.nativeKeyFamilyFromName(name)
        val sure = when {
            named in cand -> named
            cand.size == 1 -> cand[0]
            else -> null
        }
        if (sure != null) {
            saveKey(data, sure, name)
            return importKeys(rest)
        }
        // Two releases share the format (MAXX and XL 6000), or this build cannot read it: ask.
        val fams = Native.nativeKeyFamilies().toList().chunked(2).associate { it[0] to it[1] }
        val choices = cand.ifEmpty { fams.keys.toList() }
        AlertDialog.Builder(this)
            .setTitle(if (cand.isEmpty()) "$name: which release is it for?" else "$name fits more than one release")
            .setItems(choices.map { "${fams[it] ?: it} ($it)" }.toTypedArray()) { _, i ->
                saveKey(data, choices[i], name)
                importKeys(rest)
            }
            .setNegativeButton(android.R.string.cancel) { _, _ -> importKeys(rest) }
            .show()
    }

    /** The keys installed: the releases each runs, which key it is; Import, and Delete on a tap. */
    private fun showKeys() {
        val fams = Native.nativeKeyFamilies().toList().chunked(2).associate { it[0] to it[1] }
        val keys = (File(dir, "keys").listFiles() ?: emptyArray())
            .filter { it.isFile && isKeyDump(it.length()) }
            .sortedBy { it.name.lowercase() }
        val rows = keys.map { f ->
            val fam = Native.nativeKeyFamilyFromName(f.name)
            val which = (if (fam.isEmpty()) f.name else f.name.substring(fam.length + 1))
                .removePrefix("full_").removePrefix("multikey_")
            val type = if (f.length() == 192L) "DS1205 MultiKey" else "DS1991"
            "${fams[fam] ?: "(unknown release)"}\n$which  ·  $type"
        }
        val b = AlertDialog.Builder(this)
            .setTitle(R.string.keys)
            .setPositiveButton(R.string.import_key) { _, _ -> pickImport(REQ_IMPORT_KEY) }
            .setNegativeButton(android.R.string.ok, null)
        if (keys.isEmpty())
            b.setMessage("No keys yet. Each release needs a dump of its security key: a 264-byte " +
                "keyflasher DS1991 dump or a 192-byte DS1205 MultiKey. Import key copies one in; " +
                "the release it is for is read from the dump.")
        else
            b.setItems(rows.toTypedArray()) { _, i ->
                AlertDialog.Builder(this)
                    .setTitle(R.string.delete)
                    .setMessage("Delete the key ${keys[i].name}?")
                    .setPositiveButton(R.string.delete) { _, _ ->
                        keys[i].delete()
                        scan()
                        showKeys()
                    }
                    .setNegativeButton(android.R.string.cancel) { _, _ -> showKeys() }
                    .show()
            }
        b.show()
    }

    private fun saveKey(data: ByteArray, family: String, from: String) {
        val file = Native.nativeKeyFileName(data, family)
        if (file.isEmpty()) return
        val keys = File(dir, "keys").apply { mkdirs() }
        File(keys, file).writeBytes(data)
        val releases = Native.nativeKeyFamilies().toList().chunked(2).firstOrNull { it[0] == family }?.get(1) ?: family
        Toast.makeText(this, "$from: the key for $releases", Toast.LENGTH_LONG).show()
        scan()
    }

    private fun importImage(uri: Uri) {
        val (name, size) = nameAndSize(uri)
        val dest = File(dir, name)
        if (dest.exists()) {
            AlertDialog.Builder(this)
                .setTitle(R.string.import_image)
                .setMessage("$name is already here. Replace it?")
                .setPositiveButton(android.R.string.ok) { _, _ -> copyIn(uri, dest, size) }
                .setNegativeButton(android.R.string.cancel, null)
                .show()
        } else copyIn(uri, dest, size)
    }

    private fun copyIn(uri: Uri, dest: File, size: Long) {
        val bar = ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal).apply {
            max = 1000
            isIndeterminate = size <= 0
            setPadding(dp(24), dp(16), dp(24), dp(8))
        }
        val cancelled = AtomicBoolean(false)
        val dialog = AlertDialog.Builder(this)
            .setTitle("Copying ${dest.name}")
            .setView(bar)
            .setNegativeButton(android.R.string.cancel) { _, _ -> cancelled.set(true) }
            .setCancelable(false)
            .show()
        val part = File(dest.path + ".part")
        work.execute {
            val ok = try {
                contentResolver.openInputStream(uri)!!.use { input ->
                    part.outputStream().use { out ->
                        val buf = ByteArray(4 shl 20)
                        var done = 0L
                        while (!cancelled.get()) {
                            val n = input.read(buf)
                            if (n < 0) break
                            out.write(buf, 0, n)
                            done += n
                            if (size > 0) ui.post { bar.progress = (done * 1000 / size).toInt() }
                        }
                    }
                }
                !cancelled.get()
            } catch (e: Exception) {
                false
            }
            if (ok) {
                dest.delete()
                part.renameTo(dest)
            } else part.delete()
            ui.post {
                dialog.dismiss()
                if (!ok && !cancelled.get())
                    Toast.makeText(this, "Could not copy ${dest.name} (is there room?)", Toast.LENGTH_LONG).show()
                scan()
            }
        }
    }

    private fun confirmDelete(e: Entry) {
        AlertDialog.Builder(this)
            .setTitle(R.string.delete)
            .setMessage("Delete ${e.file.name} from this app's folder?\n(Only this copy.)")
            .setPositiveButton(R.string.delete) { _, _ ->
                e.file.delete()
                scan()
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    /** The list's rows: what the image is, then its profile, file and size (or why it cannot run). */
    private inner class Rows : BaseAdapter() {
        override fun getCount() = entries.size
        override fun getItem(position: Int) = entries[position]
        override fun getItemId(position: Int) = position.toLong()
        override fun getView(position: Int, convertView: View?, parent: ViewGroup): View {
            val v = convertView ?: layoutInflater.inflate(android.R.layout.simple_list_item_2, parent, false)
            val e = entries[position]
            val t1 = v.findViewById<TextView>(android.R.id.text1)
            val t2 = v.findViewById<TextView>(android.R.id.text2)
            t1.text = e.title.ifEmpty { e.file.name }
            val bytes = e.file.length()
            val where = listOfNotNull(
                profileNames.getOrNull(e.profile),
                e.file.name,
                if (bytes >= 100_000_000) "%.1f GB".format(bytes / 1e9) else "%.0f MB".format(bytes / 1e6),
            )
            t2.text = when {
                !e.runnable -> e.note.ifEmpty { "Cannot run here" }
                e.key.isEmpty() && e.keyFamily.isNotEmpty() -> "No key yet: Import key  ·  " + where.joinToString("  ·  ")
                else -> where.joinToString("  ·  ")
            }
            t1.setTextColor(if (e.runnable) Color.WHITE else Color.GRAY)
            t2.setTextColor(if (e.runnable) Color.rgb(0xB8, 0xB8, 0xC0) else Color.GRAY)
            v.setPadding(dp(16), dp(10), dp(16), dp(10))
            return v
        }
    }

    companion object {
        private const val REQ_IMPORT = 1
        private const val REQ_IMPORT_KEY = 2
    }
}
