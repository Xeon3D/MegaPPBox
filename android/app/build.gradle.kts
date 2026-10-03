// The app: framework UI only (no AndroidX), Kotlin, arm64.  The emulator is the
// prebuilt src/main/jniLibs/arm64-v8a/libMegaPPBox.so that scripts/build.sh makes
// first; the ROM set goes in as assets/roms (unpacked on first run: Roms.kt).
import java.util.Properties

plugins {
    id("com.android.application")
}

// The toolchain folder (scripts/env.sh): ~/mpb-android, or $MPB_ANDROID on CI.
val mpbAndroid = System.getenv("MPB_ANDROID") ?: (System.getProperty("user.home") + "/mpb-android")

// The release key: release.keystore there, its passwords in release-signing.properties
// (made once, never in the repository; on CI from the repository's secrets).  Every
// update of a release install must be signed with it: keep a backup.
val releaseSigning = File("$mpbAndroid/release-signing.properties")
    .takeIf { it.isFile }
    ?.let { f -> Properties().apply { f.inputStream().use { load(it) } } }

// The version is MegaPPBox's own: MEGAPPBOX_VERSION in the top CMakeLists.txt ("1.06"
// -> versionName 1.06, versionCode 106).
val mpbVersion = Regex("MEGAPPBOX_VERSION \"([0-9]+)\\.([0-9]+)\"")
    .find(rootProject.file("../CMakeLists.txt").readText())
    ?.groupValues ?: error("MEGAPPBOX_VERSION not found in CMakeLists.txt")

android {
    namespace = "io.github.xeon3d.megappbox"
    compileSdk = 36
    ndkVersion = "27.2.12479018" // strips the native library

    // The local debug key (debug.keystore in the toolchain folder): an update must be
    // signed as the installed app is.  Without it, Android's default debug key.
    signingConfigs {
        val debugKey = File("$mpbAndroid/debug.keystore")
        if (debugKey.isFile) getByName("debug") {
            storeFile = debugKey
            storePassword = "android"
            keyAlias = "androiddebugkey"
            keyPassword = "android"
        }
        if (releaseSigning != null) {
            create("release") {
                storeFile = file(releaseSigning.getProperty("storeFile"))
                storePassword = releaseSigning.getProperty("storePassword")
                keyAlias = releaseSigning.getProperty("keyAlias")
                keyPassword = releaseSigning.getProperty("keyPassword")
            }
        }
    }

    buildTypes {
        // Not debuggable.  No R8: the JNI entry points are found by class and method
        // name, and the app is small anyway.
        getByName("release") {
            isMinifyEnabled = false
            signingConfig = signingConfigs.findByName("release")
        }
    }

    defaultConfig {
        applicationId = "io.github.xeon3d.megappbox"
        minSdk = 28
        targetSdk = 36
        versionCode = mpbVersion[1].toInt() * 100 + mpbVersion[2].toInt()
        versionName = "${mpbVersion[1]}.${mpbVersion[2]}"
        ndk { abiFilters += "arm64-v8a" }
    }

    sourceSets {
        getByName("main") {
            assets.directories.add(layout.buildDirectory.dir("generated/mpb-assets").get().asFile.path)
        }
    }
}

// The repository's roms/ as assets/roms.
val copyRoms by tasks.registering(Copy::class) {
    from(rootProject.file("../roms"))
    into(layout.buildDirectory.dir("generated/mpb-assets/roms"))
}
tasks.named("preBuild") { dependsOn(copyRoms) }
