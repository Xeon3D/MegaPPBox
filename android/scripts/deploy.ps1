<#
    MegaPPBox for Android - put the app, and optionally images, on a phone over adb.

        pwsh android\scripts\deploy.ps1 [-Image <disk.img>[,<more.img>...]] [-NoStart] [-Log]

    Installs android\out\MegaPPBox-debug.apk (android/scripts/build.sh, in WSL), copies any -Image
    files into the app's own folder under their own names (the Machine Manager lists every
    .img / .iso there), and opens the Machine Manager.  adb only reads the images you name.
    -Log follows the app's log afterwards (Ctrl+C to stop).  Exactly one device: a phone
    with USB debugging on, or the emulator (test-emulator.ps1).
#>
param([string[]]$Image, [switch]$NoStart, [switch]$Log)
$ErrorActionPreference = "Stop"
# adb is the one on PATH (C:\Program Files\platform-tools): one adb for every script,
# as two adb versions restart each other's server.
$pkg     = "io.github.xeon3d.megappbox"
$manager = "$pkg/.ManagerActivity"
$dir     = "/storage/emulated/0/Android/data/$pkg/files"

$dev = @(& adb devices | Select-Object -Skip 1 | Where-Object { $_ -match "\tdevice$" })
if ($dev.Count -ne 1) { throw "need exactly one device with USB debugging on (adb devices shows $($dev.Count))" }

& adb install -r (Join-Path $PSScriptRoot "..\out\MegaPPBox-debug.apk")
if ($Image) {
    # Let the app make its own files directory before anything is pushed there.
    & adb shell am start -W -n $manager | Out-Null
    Start-Sleep -Seconds 3
    & adb shell am force-stop $pkg
    foreach ($i in $Image) { & adb push $i "$dir/$(Split-Path $i -Leaf)" }
}
if (-not $NoStart) {
    & adb shell am force-stop $pkg
    & adb logcat -c
    & adb shell am start -n $manager
}
if ($Log) {
    Start-Sleep -Seconds 2
    & adb logcat --pid=((& adb shell pidof $pkg).Trim())
}
