// MegaPPBox for Android: the native app around libMegaPPBox.so (scripts/build.sh).
pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}
dependencyResolutionManagement {
    repositories {
        google()
        mavenCentral()
    }
}
rootProject.name = "MegaPPBox"
include(":app")
