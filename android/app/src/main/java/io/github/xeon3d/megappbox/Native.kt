package io.github.xeon3d.megappbox

import android.view.Surface

/** The emulator: libMegaPPBox.so (native/android_main.cpp). One machine per process. */
object Native {
    init {
        System.loadLibrary("MegaPPBox")
    }

    /** The I/O board's lines (src/include/86box/merit_io.h). */
    const val LINE_COIN1 = 0
    const val LINE_SETUP = 4
    const val LINE_CALIBRATE = 5

    /** Set once the machine has been built (nativeInit + nativeStart). */
    var started = false

    /** 0 = ready; 1 = the configuration could not be loaded; 2 = no ROMs. */
    @JvmStatic external fun nativeInit(dir: String): Int
    @JvmStatic external fun nativeStart()
    @JvmStatic external fun nativeStop()
    @JvmStatic external fun nativeSetSurface(surface: Surface?)
    @JvmStatic external fun nativePause(paused: Boolean)
    @JvmStatic external fun nativeIsPaused(): Boolean
    /** x, y: 0..1 across the picture. */
    @JvmStatic external fun nativeTouch(x: Float, y: Float, down: Boolean)
    @JvmStatic external fun nativePulse(line: Int)
    @JvmStatic external fun nativeHardReset()
    @JvmStatic external fun nativeSpeed(): Int
    @JvmStatic external fun nativeFrameSize(): IntArray
    @JvmStatic external fun nativeIsRunning(): Boolean
    /** { title, profile name } */
    @JvmStatic external fun nativeCabinetInfo(): Array<String>

    /* The Machine Manager.  nativeIdentify/Profiles/Keys need no machine. */
    /** { runnable "1"/"0", release, version, profile index, default key, note, evidence } */
    @JvmStatic external fun nativeIdentify(path: String): Array<String>
    /** { name, description } per profile */
    @JvmStatic external fun nativeProfiles(): Array<String>
    /** { ref, name } per built-in key */
    @JvmStatic external fun nativeKeys(): Array<String>
    /** The cabinet's pick (after nativeInit); rebuilds a running machine. */
    /* Key import (the key's release is read from the dump). */
    /** The key families the dump could be for, best first; empty if it is not a key. */
    @JvmStatic external fun nativeKeyIdentify(data: ByteArray): Array<String>
    /** The family a file name gives, or "". */
    @JvmStatic external fun nativeKeyFamilyFromName(name: String): String
    /** { prefix, releases } per family. */
    @JvmStatic external fun nativeKeyFamilies(): Array<String>
    /** The name the dump is kept under in keys/, or "". */
    @JvmStatic external fun nativeKeyFileName(data: ByteArray, prefix: String): String

    @JvmStatic external fun nativeSelect(profile: Int, image: String, key: String, title: String, modem: Boolean)

    /* Mega-Link: the running image's network card. */
    /** { has a card "1"/"0", fitted "1"/"0", "lswitch"/"rswitch", remote host, secret, card name } */
    @JvmStatic external fun nativeNetworkGet(): Array<String>
    /** Fit or remove the card, on the local switch or one remote cabinet; restarts the cabinet. */
    @JvmStatic external fun nativeNetworkSet(on: Boolean, remote: Boolean, host: String, secret: String)

    /* The modem on COM2 (TournaMAXX / MegaNET). */
    /** { fitted "1"/"0", host ("" = no line), port, sounds "1"/"0" } */
    @JvmStatic external fun nativeModemGet(): Array<String>
    /** Plug the modem in or out and set where a dial connects, at once; true when it went in or out. */
    @JvmStatic external fun nativeModemSet(on: Boolean, host: String, port: Int, sounds: Boolean): Boolean
}
