package io.github.xeon3d.megappbox

import android.content.Context
import java.io.File

/**
 * The ROM set ships as assets/roms; the emulator opens ROMs with fopen(), so they are
 * unpacked into the app's files directory - again whenever the app is updated.
 */
object Roms {
    fun unpack(context: Context, dest: File) {
        val info = context.packageManager.getPackageInfo(context.packageName, 0)
        val stamp = "${info.longVersionCode}:${info.lastUpdateTime}"
        val stampFile = File(dest, ".unpacked")
        if (stampFile.isFile && stampFile.readText() == stamp) return

        copyTree(context, "roms", dest)
        stampFile.writeText(stamp)
    }

    private fun copyTree(context: Context, asset: String, dest: File) {
        val children = context.assets.list(asset) ?: emptyArray()
        if (children.isEmpty()) {
            dest.parentFile?.mkdirs()
            context.assets.open(asset).use { input -> dest.outputStream().use { input.copyTo(it) } }
            return
        }
        dest.mkdirs()
        for (child in children) copyTree(context, "$asset/$child", File(dest, child))
    }
}
