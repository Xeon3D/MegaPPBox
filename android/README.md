# MegaPPBox for Android

MegaPPBox, the Merit Megatouch cabinet emulator (XL and MAXX, on 86Box), as a native
Android app. It runs untouched Megatouch disk and CD images on an arm64 phone or tablet,
using the emulator's arm64 dynamic recompiler. It is built from this repository, with the
same emulator as the Windows, Linux and macOS versions, and released with them.

## Installing

Download `MegaPPBox-<version>-android.apk` from the
[Releases](https://github.com/Xeon3D/MegaPPBox/releases) and open it on the device (allow
installing from your browser or file manager when asked). It needs a 64-bit ARM device with
Android 9 or later. Samsung's **Auto Blocker** (Settings > Security and privacy) blocks app
installs from outside the stores: turn it off to install, and back on afterwards if you like.

Then add your images and keys in the Machine Manager:

- **Import** copies a Megatouch disk (`.img`) or CD (`.iso`) image into the app's folder.
  Tap an image to run it; it is identified from its contents, and runs with its release's
  hardware profile and key. A long press deletes it.
- **Keys** lists the security keys installed, with the releases each one runs, and imports
  more. No keys are included: each release needs a dump of its key (a 264-byte keyflasher
  DS1991 dump or a 192-byte DS1205 MultiKey). The release a dump is for is read from the
  dump itself.

In the cabinet, the bottom bar has **Credit**, **Setup** (the operator menu), **Manager**
(back to the Machine Manager), **Fullscreen** and **More**: Calibrate, Mega-Link, Modem,
Pause, Reset, About and Exit.

- **Mega-Link** (MAXX): the cabinet's network card. By default every MAXX cabinet is on the
  local switch with the link name `megatouch`, so cabinets on the same Wi-Fi find each
  other, including desktop MegaPPBox cabinets on Tools > Network settings > Local Switch
  with the same link name. Networks that drop multicast can link two cabinets directly by
  address instead.
- **Modem**: the ActionTec modem on COM2, on by default from MAXX Diamond on. A dial
  connects to a TCP host instead of a telephone line, such as a
  [TournaMAXX-Revival](https://github.com/Xeon3D/TournaMAXX-Revival) server.

Speed depends on the device's single-core performance: a Galaxy S25 Ultra runs at full
speed, a mid-range tablet (Galaxy Tab S10 Lite) does not.

## What is here

| Path | What |
|---|---|
| `native/` | the Android platform layer, with no Qt: platform functions, the JNI bridge (emulation thread, frames into a `SurfaceView`, touch, the I/O board's lines), and image identification. The top CMake builds it in place of the Qt front end when the target is Android (`if(ANDROID)`), making the emulator a shared library, `libMegaPPBox.so` |
| `app/` | the app, in Kotlin with Android framework widgets: the Machine Manager, the key list, the cabinet, Mega-Link and the modem |
| `scripts/` | toolchain, libraries, build (Linux or WSL), deploy and the Windows Android emulator (PowerShell and adb) |
| `docs/android-feasibility.md` | the study that led here |

## Building

On Linux or in WSL (Ubuntu), everything user-local under `~/mpb-android`:

```sh
git clone https://github.com/Xeon3D/MegaPPBox.git
cd MegaPPBox
bash android/scripts/setup-toolchain.sh jdk
bash android/scripts/setup-toolchain.sh sdk
bash android/scripts/setup-toolchain.sh pkgs   # accepts the Android SDK licences
bash android/scripts/build-deps.sh             # zstd, libpng, FreeType, libsndfile, OpenAL Soft
bash android/scripts/build.sh                  # -> android/out/MegaPPBox-debug.apk
bash android/scripts/build.sh release          # -> android/out/MegaPPBox-<version>-android.apk
```

The version is MegaPPBox's (`MEGAPPBOX_VERSION` in `CMakeLists.txt`). The release build is
signed with a key kept outside the repository (`~/mpb-android/release.keystore` and
`release-signing.properties`; on GitHub, the release workflow takes them from the
repository's secrets). A debug build and a release build have different signatures, so one
cannot be installed over the other: uninstalling first deletes the app's folder and the
images in it.

Then, on Windows, with a phone connected (USB debugging on):

```powershell
pwsh android\scripts\deploy.ps1 -Image "D:\Megatouch\XL Platinum (V1.02).img"
```

Images live in the app's folder, `Android/data/io.github.xeon3d.megappbox/files`, and keys
in its `keys` folder. Put them there with `deploy.ps1 -Image` or the Machine Manager. The
emulator opens images with plain file calls, which Android only allows there. Images can
also be tried in the Windows Android emulator with `android\scripts\test-emulator.ps1`.

## Status

Tested on a Galaxy S25 Ultra: XL Platinum at full speed with touch and sound, and Mega-Link
over Wi-Fi between the phone and a desktop MegaPPBox cabinet. Other releases are less
tried. XL Double Platinum stops at ROM-DOS, as on the desktop (open).
